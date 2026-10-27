#include "tools/toolregistry.hpp"

#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QWebEngineFrame>
#include <QWebEnginePage>
#include <utility>

#include "browser/tabmanager.hpp"
#include "browser/webview.hpp"

// Registers the agent file-upload tool (upload_files). Contract: only the
// exact absolute paths carried by the approved call are handed to the page;
// file contents never enter any payload, log, or the transcript. The
// chooser queue on the target WebView is one-shot: consumed by the next
// chooseFiles() and cleared on every failure path (no matching input, no
// chooser firing) or when the tab closes.

namespace {

constexpr int kMaxUploadFiles = 10;
constexpr qint64 kMaxUploadFileBytes = 100 * 1024 * 1024;
constexpr int kChooserTimeoutMs = 8000;
constexpr int kVerifyAttempts = 25;
constexpr int kVerifyDelayMs = 120;

QJsonObject toolParams(
    std::initializer_list<std::pair<const char *, const char *>> properties = {},
    std::initializer_list<const char *> required = {})
{
    QJsonObject props;
    for (const auto &[name, type] : properties)
        props.insert(name, QJsonObject{{"type", type}});
    QJsonArray req;
    for (const char *name : required)
        req.append(name);
    return QJsonObject{{"type", "object"}, {"properties", props}, {"required", req}};
}

// JSON string literal for embedding inside generated JavaScript, same
// escaping trick pagetools uses.
QString jsLiteral(const QString &text)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson()) + QLatin1String("[0]");
}

QStringList pathList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray())
        out.append(item.toString());
    return out;
}

// Returns {code, message} on rejection, {"", ""} when acceptable.
std::pair<QString, QString> validatePath(const QString &path)
{
    if (path.trimmed().isEmpty() || !QFileInfo(path).isAbsolute())
        return {QStringLiteral("invalid_path"),
                QStringLiteral("path must be absolute: '%1'").arg(path)};
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return {QStringLiteral("not_a_file"),
                QStringLiteral("not an existing file: '%1'").arg(path)};
    if (!info.isReadable())
        return {QStringLiteral("not_a_file"),
                QStringLiteral("file is not readable: '%1'").arg(path)};
    if (info.size() > kMaxUploadFileBytes)
        return {QStringLiteral("too_large"),
                QStringLiteral("file exceeds the %1 MB cap: '%2'")
                    .arg(kMaxUploadFileBytes / (1024 * 1024))
                    .arg(path)};
    return {{}, {}};
}

// Contract: script returns JSON with "found": true|false in every frame.
// Runs it main-frame-first, then children; run() fires with the first frame
// whose result has found=true (frame name attached), or with the main-frame
// result when nothing matched. Same mechanics as pagetools.
void collectFrames(const QWebEngineFrame &frame, QList<QWebEngineFrame> *out)
{
    out->append(frame);
    for (const QWebEngineFrame &child : frame.children())
        collectFrames(child, out);
}

void evaluateNextFrame(const std::shared_ptr<QList<QWebEngineFrame>> &frames, int index,
                       const QString &script, const std::shared_ptr<QVariant> &mainResult,
                       const std::function<void(const QString &, const QVariant &)> &run)
{
    if (index >= frames->size()) {
        run(QStringLiteral("main"), *mainResult);
        return;
    }
    QWebEngineFrame frame = frames->at(index);
    const bool isMain = index == 0;
    frame.runJavaScript(script, 0,
                        [frames, index, script, mainResult, run, frame, isMain](
                            const QVariant &result) mutable {
                            if (isMain)
                                *mainResult = result;
                            const QJsonObject data = QJsonDocument::fromJson(
                                result.toString().toUtf8()).object();
                            if (data.value("found").toBool()) {
                                run(frame.name().isEmpty() ? QStringLiteral("(unnamed)")
                                                           : frame.name(),
                                    result);
                                return;
                            }
                            evaluateNextFrame(frames, index + 1, script, mainResult, run);
                        });
}

void runInFirstMatchingFrame(QWebEnginePage *page, const QString &script,
                             std::function<void(const QString &, const QVariant &)> run)
{
    auto frames = std::make_shared<QList<QWebEngineFrame>>();
    collectFrames(page->mainFrame(), frames.get());
    auto mainResult = std::make_shared<QVariant>();
    evaluateNextFrame(frames, 0, script, mainResult, std::move(run));
}

// Resolves the selector to a file input: the match itself, or the file
// input behind a <label> (for= attribute or wrapped). Scrolls into view and
// clicks it with the same event sequence click_element uses.
QString clickScript(const QString &selectorLiteral)
{
    return QStringLiteral(
               "(function(){"
               "let el=null;"
               "try { el = document.querySelector(%1); } catch(e) {"
               "  return JSON.stringify({found:false, error:'bad_selector',"
               "    message:String(e)}); }"
               "if (!el) return JSON.stringify({found:false});"
               "if (el.tagName === 'LABEL') {"
               "  const target = (el.htmlFor && document.getElementById(el.htmlFor))"
               "              || el.querySelector('input[type=file]');"
               "  if (target) el = target;"
               "}"
               "if (el.tagName !== 'INPUT' || (el.type||'').toLowerCase() !== 'file')"
               "  return JSON.stringify({found:false, error:'not_file_input',"
               "    message:'selector does not match a file input or its label'});"
               "if (el.disabled) return JSON.stringify({found:false, error:'disabled',"
               "  message:'file input is disabled'});"
               "el.scrollIntoView({block:'center'});"
               "el.dispatchEvent(new MouseEvent('mousedown',{bubbles:true}));"
               "el.dispatchEvent(new MouseEvent('mouseup',{bubbles:true}));"
               "el.click();"
               "return JSON.stringify({found:true});"
               "})()")
        .arg(selectorLiteral);
}

// Reads back what landed in the input after the chooser: names and sizes
// only — the browser hands the file to the page; contents stay out of the
// conversation.
QString verifyScript(const QString &selectorLiteral)
{
    return QStringLiteral(
               "(function(){"
               "let el=null;"
               "try { el = document.querySelector(%1); } catch(e) {"
               "  return JSON.stringify({found:false, error:'bad_selector',"
               "    message:String(e)}); }"
               "if (!el) return JSON.stringify({found:false});"
               "if (el.tagName === 'LABEL') {"
               "  const target = (el.htmlFor && document.getElementById(el.htmlFor))"
               "              || el.querySelector('input[type=file]');"
               "  if (target) el = target;"
               "}"
               "if (el.tagName !== 'INPUT' || (el.type||'').toLowerCase() !== 'file')"
               "  return JSON.stringify({found:false});"
               "const files=[];"
               "for (const f of el.files) files.push({name:f.name, size:f.size});"
               "return JSON.stringify({found:true, count: el.files.length,"
               "  files: files});"
               "})()")
        .arg(selectorLiteral);
}

class UploadFilesTool final : public Tool
{
public:
    explicit UploadFilesTool(ToolRegistry *registry) : m_registry(registry) {}

    ToolSpec spec() const override
    {
        return {"upload_files",
                "Upload local files through a page's file input: the CSS selector must "
                "match the <input type=file> (or its label). Caps: 10 files, 100 MB "
                "each. Requires explicit user approval; the approval lists every path "
                "that will be sent. File contents go to the page only and are never "
                "read into the conversation.",
                toolParams({{"selector", "string"},
                            {"paths", "array"},
                            {"tab_index", "number"}},
                           {"selector", "paths"})};
    }

    Risk risk() const override { return Risk::Sensitive; }

    // Chooser wait plus verification retries can outlive the default 15 s.
    int timeoutMs() const override { return 30000; }

    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        TabManager *tabs = m_registry ? m_registry->tabs() : nullptr;
        WebView *view = nullptr;
        if (tabs) {
            if (args.contains("tab_index"))
                view = tabs->view(args.value("tab_index").toInt(-1));
            else
                view = tabs->currentView();
        }
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }

        const QStringList paths = pathList(args.value("paths"));
        if (paths.isEmpty()) {
            done({false, "invalid_path",
                  "paths must be a non-empty array of absolute file paths", {}});
            return;
        }
        if (paths.size() > kMaxUploadFiles) {
            done({false, "too_many",
                  QStringLiteral("at most %1 files can be uploaded at once")
                      .arg(kMaxUploadFiles),
                  {}});
            return;
        }
        for (const QString &path : paths) {
            const auto rejection = validatePath(path);
            if (!rejection.first.isEmpty()) {
                done({false, rejection.first, rejection.second, {}});
                return;
            }
        }

        // Queue first so chooseFiles() can answer as soon as the click
        // lands; every failure path below clears it again.
        view->setPendingUploadFiles(paths);
        const QString selector = args.value("selector").toString();
        QPointer<WebView> guard(view);
        runInFirstMatchingFrame(
            view->page(), clickScript(jsLiteral(selector)),
            [this, guard, selector, done](const QString &frame, const QVariant &result) {
                if (!guard) {
                    return;
                }
                const QJsonObject data =
                    QJsonDocument::fromJson(result.toString().toUtf8()).object();
                if (!data.value("found").toBool()) {
                    guard->clearPendingUploadFiles();
                    const QString error = data.contains("error")
                                              ? data.value("error").toString()
                                              : QStringLiteral("no_matching_input");
                    const QString message = data.contains("message")
                                                ? data.value("message").toString()
                                                : QStringLiteral(
                                                      "no file input matches in any "
                                                      "frame; the site may use a "
                                                      "custom picker");
                    done({false, error, message, {}});
                    return;
                }
                waitForChooser(guard.data(), selector, frame, done);
            });
    }

private:
    // Gives the renderer a moment to invoke chooseFiles() after the click;
    // the page raises the flag when it answered from the queue.
    void waitForChooser(WebView *view, const QString &selector, const QString &frame,
                        std::function<void(ToolResult)> done)
    {
        QPointer<WebView> guard(view);
        if (guard->uploadChooserFired()) {
            verifyFiles(guard.data(), selector, frame, kVerifyAttempts, done);
            return;
        }
        QEventLoop loop;
        QTimer deadline;
        QTimer poll;
        deadline.setSingleShot(true);
        poll.setInterval(50);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, guard]() {
            if (!guard || guard->uploadChooserFired())
                loop.quit();
        });
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        deadline.start(kChooserTimeoutMs);
        poll.start();
        loop.exec();
        poll.stop();
        if (!guard || !guard->uploadChooserFired()) {
            if (guard)
                guard->clearPendingUploadFiles();
            done({false, "no_file_chooser",
                  "no file chooser opened within the timeout; the page ignored the "
                  "click or uses a custom picker",
                  {}});
            return;
        }
        verifyFiles(guard.data(), selector, frame, kVerifyAttempts, done);
    }

    // The renderer applies the chooser result asynchronously, so poll until
    // el.files is populated (or give up with a clear error).
    void verifyFiles(WebView *view, const QString &selector, const QString &frame,
                     int attemptsLeft, std::function<void(ToolResult)> done)
    {
        if (attemptsLeft <= 0) {
            done({false, "upload_failed",
                  "the file chooser fired but the input never received the files",
                  {}});
            return;
        }
        QPointer<WebView> guard(view);
        view->page()->runJavaScript(
            verifyScript(jsLiteral(selector)), 0,
            [this, guard, selector, frame, attemptsLeft, done](const QVariant &result) {
                if (!guard)
                    return;
                const QJsonObject data =
                    QJsonDocument::fromJson(result.toString().toUtf8()).object();
                if (data.value("found").toBool() && data.value("count").toInt() > 0) {
                    QJsonObject out;
                    out.insert("uploaded", data.value("count"));
                    out.insert("files", data.value("files"));
                    if (frame != QStringLiteral("main"))
                        out.insert("frame", frame);
                    done({true, {}, {}, out});
                    return;
                }
                QTimer::singleShot(kVerifyDelayMs, [this, guard, selector, frame,
                                                    attemptsLeft, done]() {
                    if (!guard)
                        return;
                    verifyFiles(guard.data(), selector, frame, attemptsLeft - 1, done);
                });
            });
    }

    ToolRegistry *m_registry = nullptr;
};

} // namespace

void registerUploadTools(ToolRegistry *registry)
{
    if (!registry)
        return;
    registry->registerTool(new UploadFilesTool(registry));
}
