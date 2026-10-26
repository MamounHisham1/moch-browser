#include "tools/pagetools.hpp"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QWebEnginePage>

#include "browser/tabmanager.hpp"
#include "browser/urlresolver.hpp"
#include "browser/webview.hpp"
#include "security/contentsanitizer.hpp"

namespace {

constexpr int kMaxContentChars = 24000;
constexpr int kMaxLinks = 200;
constexpr int kMaxElements = 100;
constexpr int kMaxScreenshotBytes = 3 * 1024 * 1024;

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

QString objectToString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
}

QString runJsSync(QWebEnginePage *page, const QString &source)
{
    QString out;
    QEventLoop loop;
    QTimer::singleShot(12000, &loop, &QEventLoop::quit);
    page->runJavaScript(source, 0, [&out, &loop](const QVariant &result) {
        out = result.toString();
        loop.quit();
    });
    loop.exec();
    return out;
}

QJsonObject toolError(const char *code, const QString &message)
{
    return QJsonObject{{"error", code}, {"message", message}};
}

class PageToolBase : public Tool
{
public:
    explicit PageToolBase(ToolRegistry *registry) : m_registry(registry) {}

protected:
    WebView *resolveTab(const QJsonObject &args) const
    {
        TabManager *tabs = tabManager();
        if (!tabs)
            return nullptr;
        if (args.contains("tab_index")) {
            const int index = args.value("tab_index").toInt(-1);
            return tabs->view(index);
        }
        return tabs->currentView();
    }

    TabManager *tabManager() const
    {
        return m_registry ? m_registry->tabs() : nullptr;
    }

    ToolRegistry *m_registry = nullptr;
};

class GetCurrentPageTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"get_current_page",
                "Get the URL, title, and load state of a browser tab.",
                toolParams({{"tab_index", "number"}}, {})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        QJsonObject data;
        data.insert("url", view->url().toString());
        data.insert("title", view->title());
        data.insert("loading", view->isLoading());
        done({true, {}, {}, data});
    }
};

class GetPageContentTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"get_page_content",
                "Extract the readable text of a page for analysis. Content is truncated "
                "to a bounded length.",
                toolParams({{"tab_index", "number"}, {"max_chars", "number"}}, {})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        QWebEnginePage *page = view->page();
        const int maxChars = args.value("max_chars").toInt(kMaxContentChars);
        page->toHtml([this, page, maxChars, done](const QString &html) {
            const Security::SanitizeResult sanitized = Security::sanitize(html, page->url().toString(), maxChars);
            QJsonObject data;
            data.insert("url", page->url().toString());
            data.insert("title", page->title());
            data.insert("content", sanitized.text);
            data.insert("truncated", sanitized.truncated);
            done({true, {}, {}, data});
        });
    }
};

class GetPageLinksTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"get_page_links",
                QString("List links on the page (capped at %1). Each entry has text, href "
                        "and the resolved absolute URL.")
                    .arg(kMaxLinks),
                toolParams({{"tab_index", "number"}}, {})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString script = QStringLiteral(
            "(function(){"
            "const out=[];"
            "for (const a of document.querySelectorAll('a[href]')) {"
            "  const text=(a.innerText||'').trim().slice(0,120);"
            "  if (!text && !a.hasAttribute('aria-label')) continue;"
            "  out.push({text: text || a.getAttribute('aria-label'), href: a.href});"
            "  if (out.length >= %1) break;"
            "}"
            "return JSON.stringify({links: out, total: document.querySelectorAll('a[href]').length});"
            "})()");
        view->page()->runJavaScript(script.arg(kMaxLinks), 0,
                                    [done](const QVariant &result) {
                                        const QJsonObject data =
                                            QJsonDocument::fromJson(result.toString().toUtf8()).object();
                                        done({true, {}, {}, data});
                                    });
    }
};

class FindElementsTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"find_elements",
                QString("Find interactive elements by CSS selector. Returns up to %1 "
                        "matches with index, tag, text and input metadata.")
                    .arg(kMaxElements),
                toolParams({{"selector", "string"}, {"tab_index", "number"}}, {"selector"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString selector = args.value("selector").toString();
        const QString script = QStringLiteral(
            "(function(){"
            "let els=[];"
            "try { els = Array.from(document.querySelectorAll(SELECTOR)); }"
            "catch(e) { return JSON.stringify({error:'bad_selector', message:String(e)}); }"
            "const out=[];"
            "els.slice(0,%1).forEach((el,i)=>{"
            "  const r=el.getBoundingClientRect();"
            "  out.push({index:i, tag:el.tagName.toLowerCase(),"
            "    text:(el.innerText||el.value||el.getAttribute('aria-label')||'').trim().slice(0,100),"
            "    type: el.tagName==='INPUT' ? (el.type||'text') : el.tagName.toLowerCase(),"
            "    value: (el.value||'').toString().slice(0,100),"
            "    placeholder: el.placeholder||'', disabled: !!el.disabled,"
            "    rect: {x:Math.round(r.x), y:Math.round(r.y), w:Math.round(r.width), h:Math.round(r.height)}});"
            "});"
            "return JSON.stringify({elements: out, total: els.length});"
            "})()");
        QString source = script.arg(kMaxElements);
        source.replace(QStringLiteral("SELECTOR"),
                       QString::fromUtf8(QJsonDocument(QJsonArray{selector}).toJson())
                           + QLatin1String("[0]"));
        view->page()->runJavaScript(source, 0, [done](const QVariant &result) {
            const QJsonObject data = QJsonDocument::fromJson(result.toString().toUtf8()).object();
            if (data.contains("error")) {
                done({false, data.value("error").toString(),
                      data.value("message").toString(), {}});
                return;
            }
            done({true, {}, {}, data});
        });
    }
};

class ClickElementTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"click_element",
                "Click an element matching the CSS selector (first match). Confirm with "
                "the user before clicking anything that submits, purchases or posts.",
                toolParams({{"selector", "string"}, {"tab_index", "number"}}, {"selector"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString selector = args.value("selector").toString();
        const QString script = QStringLiteral(
            "(function(){"
            "let el=null;"
            "try { el = document.querySelector(%1); } catch(e) {"
            "  return JSON.stringify({error:'bad_selector', message:String(e)}); }"
            "if (!el) return JSON.stringify({error:'not_found', message:'no element matches'});"
            "el.scrollIntoView({block:'center'});"
            "el.dispatchEvent(new MouseEvent('mousedown',{bubbles:true}));"
            "el.dispatchEvent(new MouseEvent('mouseup',{bubbles:true}));"
            "el.click();"
            "return JSON.stringify({clicked:{tag:el.tagName.toLowerCase(),"
            "  text:(el.innerText||el.value||'').trim().slice(0,100)}});"
            "})()");
        const QString literal = QString(QJsonDocument(QJsonArray{selector}).toJson())
                                + QLatin1String("[0]");
        view->page()->runJavaScript(script.arg(literal), 0,
                                    [done](const QVariant &result) {
                                        const QJsonObject data = QJsonDocument::fromJson(
                                            result.toString().toUtf8()).object();
                                        if (data.contains("error")) {
                                            done({false, data.value("error").toString(),
                                                  data.value("message").toString(), {}});
                                            return;
                                        }
                                        done({true, {}, {}, data});
                                    });
    }
};

class FillInputTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"fill_input",
                "Set the value of an input or textarea matching the CSS selector "
                "(first match). Never use for passwords or payment data unless the user "
                "explicitly approved.",
                toolParams({{"selector", "string"},
                            {"value", "string"},
                            {"tab_index", "number"}},
                           {"selector", "value"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString selector = args.value("selector").toString();
        const QString value = args.value("value").toString();
        const QString script = QStringLiteral(
            "(function(){"
            "let el=null;"
            "try { el = document.querySelector(%1); } catch(e) {"
            "  return JSON.stringify({error:'bad_selector', message:String(e)}); }"
            "if (!el) return JSON.stringify({error:'not_found', message:'no element matches'});"
            "if (el.disabled) return JSON.stringify({error:'disabled', message:'element is disabled'});"
            "if (el.readOnly) return JSON.stringify({error:'readonly', message:'element is read-only'});"
            "el.focus();"
            "el.value = %2;"
            "el.dispatchEvent(new Event('input',{bubbles:true}));"
            "el.dispatchEvent(new Event('change',{bubbles:true}));"
            "return JSON.stringify({filled:{tag:el.tagName.toLowerCase(),"
            "  length: String(el.value).length}});"
            "})()");
        const QString selectorLiteral = QString(QJsonDocument(QJsonArray{selector}).toJson())
                                        + QLatin1String("[0]");
        const QString valueLiteral = QString(QJsonDocument(QJsonArray{value}).toJson())
                                     + QLatin1String("[0]");
        view->page()->runJavaScript(script.arg(selectorLiteral, valueLiteral), 0,
                                    [done](const QVariant &result) {
                                        const QJsonObject data = QJsonDocument::fromJson(
                                            result.toString().toUtf8()).object();
                                        if (data.contains("error")) {
                                            done({false, data.value("error").toString(),
                                                  data.value("message").toString(), {}});
                                            return;
                                        }
                                        done({true, {}, {}, data});
                                    });
    }
};

class SelectOptionTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"select_option",
                "Select an option (by visible text or value) in a <select> matching the "
                "CSS selector.",
                toolParams({{"selector", "string"},
                            {"value", "string"},
                            {"tab_index", "number"}},
                           {"selector", "value"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString selector = args.value("selector").toString();
        const QString value = args.value("value").toString();
        const QString script = QStringLiteral(
            "(function(){"
            "let el=null;"
            "try { el = document.querySelector(%1); } catch(e) {"
            "  return JSON.stringify({error:'bad_selector', message:String(e)}); }"
            "if (!el || el.tagName !== 'SELECT')"
            "  return JSON.stringify({error:'not_found', message:'no <select> matches'});"
            "const want=%2;"
            "let matched=false;"
            "for (const opt of el.options) {"
            "  if (opt.value===want || opt.text.trim()===want) { el.value=opt.value; matched=true; break; }"
            "}"
            "if (!matched) return JSON.stringify({error:'no_option', message:'option not found'});"
            "el.dispatchEvent(new Event('change',{bubbles:true}));"
            "return JSON.stringify({selected: el.selectedOptions[0].text.trim()});"
            "})()");
        const QString selectorLiteral = QString(QJsonDocument(QJsonArray{selector}).toJson())
                                        + QLatin1String("[0]");
        const QString valueLiteral = QString(QJsonDocument(QJsonArray{value}).toJson())
                                     + QLatin1String("[0]");
        view->page()->runJavaScript(script.arg(selectorLiteral, valueLiteral), 0,
                                    [done](const QVariant &result) {
                                        const QJsonObject data = QJsonDocument::fromJson(
                                            result.toString().toUtf8()).object();
                                        if (data.contains("error")) {
                                            done({false, data.value("error").toString(),
                                                  data.value("message").toString(), {}});
                                            return;
                                        }
                                        done({true, {}, {}, data});
                                    });
    }
};

class ScrollPageTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"scroll_page",
                "Scroll the page. direction: 'up' | 'down' (default down); amount: pixels "
                "(default 600).",
                toolParams({{"direction", "string"}, {"amount", "number"}, {"tab_index", "number"}},
                           {})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const bool up = args.value("direction").toString() == "up";
        const int amount = args.value("amount").toInt(600);
        const QString script = QStringLiteral(
            "(function(){"
            "window.scrollBy({top: %1 * (%2 ? -1 : 1)});"
            "return JSON.stringify({scrollY: Math.round(window.scrollY),"
            "  max: document.body.scrollHeight - window.innerHeight});"
            "})()")
                                   .arg(amount)
                                   .arg(up ? "true" : "false");
        view->page()->runJavaScript(script, 0, [done](const QVariant &result) {
            done({true, {}, {},
                 QJsonDocument::fromJson(result.toString().toUtf8()).object()});
        });
    }
};

class NavigateToUrlTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"navigate_to_url",
                "Navigate a tab and wait for the load to settle. Accepts a full URL, a "
                "domain (noon.com), or a well-known site name (youtube, gmail, whatsapp, "
                "noon). Phrases are searched on the web.",
                toolParams({{"url", "string"}, {"tab_index", "number"}}, {"url"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        const QUrl url =
            UrlResolver::resolveAgentTarget(args.value("url").toString(), "duckduckgo");
        if (url.scheme() != "http" && url.scheme() != "https") {
            done({false, "bad_scheme",
                  "only web destinations are allowed; pass a URL, domain, or site name", {}});
            return;
        }
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const auto fired = std::make_shared<bool>(false);
        const auto connPtr = std::make_shared<QMetaObject::Connection>();
        *connPtr = view->connect(
            view, &QWebEngineView::loadFinished, view,
            [done, view, url, fired, connPtr](bool ok) {
                *fired = true;
                if (*connPtr) {
                    QObject::disconnect(*connPtr);
                    *connPtr = {};
                }
                QJsonObject data;
                data.insert("url", view->url().isEmpty() ? url.toString() : view->url().toString());
                data.insert("title", view->title());
                data.insert("ok", ok);
                done({ok, ok ? QString() : "load_failed",
                      ok ? QString() : "page failed to load", data});
            },
            static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));
        view->load(url);
        QTimer::singleShot(20000, view, [fired, connPtr, done, view]() {
            if (*fired)
                return;
            *fired = true;
            if (*connPtr) {
                QObject::disconnect(*connPtr);
                *connPtr = {};
            }
            QJsonObject data;
            data.insert("url", view->url().toString());
            done({false, "timeout", "navigation did not finish in 20s", data});
        });
    }
};

class SwitchTabTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"switch_tab", "Make a tab the active tab.",
                toolParams({{"tab_index", "number"}}, {"tab_index"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        TabManager *tabs = tabManager();
        if (!tabs) {
            done({false, "no_tabs", "tab manager unavailable", {}});
            return;
        }
        const int index = args.value("tab_index").toInt(-1);
        if (!tabs->view(index)) {
            done({false, "no_tab", QString("no tab at index %1").arg(index), {}});
            return;
        }
        tabs->setCurrentIndex(index);
        done({true, {}, {}, QJsonObject{{"index", index},
                                        {"url", tabs->view(index)->url().toString()}}});
    }
};

class CreateTabTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"create_tab", "Open a new tab, optionally at a URL, and make it active.",
                toolParams({{"url", "string"}}, {})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        TabManager *tabs = tabManager();
        if (!tabs) {
            done({false, "no_tabs", "tab manager unavailable", {}});
            return;
        }
        const QString urlText = args.value("url").toString();
        const QUrl url = urlText.isEmpty() ? QUrl("moch://newtab") : QUrl(urlText);
        const int index = tabs->addTab(url);
        done({true, {}, {}, QJsonObject{{"index", index}, {"url", url.toString()}}});
    }
};

class CloseTabTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"close_tab", "Close a tab by index.",
                toolParams({{"tab_index", "number"}}, {"tab_index"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        TabManager *tabs = tabManager();
        if (!tabs) {
            done({false, "no_tabs", "tab manager unavailable", {}});
            return;
        }
        const int index = args.value("tab_index").toInt(-1);
        if (!tabs->view(index)) {
            done({false, "no_tab", QString("no tab at index %1").arg(index), {}});
            return;
        }
        tabs->closeTab(index);
        done({true, {}, {}, QJsonObject{{"closed", index}}});
    }
};

class TakeScreenshotTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"take_screenshot",
                "Capture the visible tab as a screenshot (PNG, base64, size-capped).",
                toolParams({{"tab_index", "number"}}, {})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        view->grab().save("/dev/null");
        const QImage image = view->grab().toImage();
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        if (png.size() > kMaxScreenshotBytes) {
            QImage scaled = image.scaled(1024, 4096, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
            png.clear();
            QBuffer scaledBuffer(&png);
            scaledBuffer.open(QIODevice::WriteOnly);
            scaled.save(&scaledBuffer, "PNG");
        }
        QJsonObject data;
        data.insert("format", "png");
        data.insert("base64", QString::fromLatin1(png.toBase64()));
        data.insert("bytes", png.size());
        done({true, {}, {}, data});
    }
};

class WaitForElementTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    int timeoutMs() const override { return 12000; }
    ToolSpec spec() const override
    {
        return {"wait_for_element",
                "Wait until an element matching the CSS selector exists (up to 10s).",
                toolParams({{"selector", "string"}, {"tab_index", "number"}}, {"selector"})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString selector = args.value("selector").toString();
        const QString literal = QString(QJsonDocument(QJsonArray{selector}).toJson())
                                + QLatin1String("[0]");
        const QString script = QStringLiteral(
            "(function(){"
            "try { return JSON.stringify({found: !!document.querySelector(%1)}); }"
            "catch(e) { return JSON.stringify({error:'bad_selector', message:String(e)}); }"
            "})()");

        auto *poller = new QObject(view);
        auto *timer = new QTimer(poller);
        const auto deadline = std::make_shared<QElapsedTimer>();
        deadline->start();
        QObject::connect(timer, &QTimer::timeout, poller, [poller, timer, deadline, view, script, literal, done] {
            view->page()->runJavaScript(script.arg(literal), 0,
                                        [poller, timer, deadline, done](const QVariant &result) {
                                            const QJsonObject data = QJsonDocument::fromJson(
                                                result.toString().toUtf8()).object();
                                            const auto finish = [poller, timer, deadline, done](const ToolResult &r) {
                                                timer->stop();
                                                poller->deleteLater();
                                                done(r);
                                            };
                                            if (data.contains("error")) {
                                                finish({false, data.value("error").toString(),
                                                        data.value("message").toString(), {}});
                                                return;
                                            }
                                            if (data.value("found").toBool()) {
                                                finish({true, {}, {}, QJsonObject{{"found", true}}});
                                                return;
                                            }
                                            if (deadline->elapsed() > 10000) {
                                                finish({false, "not_found",
                                                        "element did not appear within 10s", {}});
                                            }
                                        });
        });
        timer->start(250);
    }
};

class ExtractStructuredDataTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"extract_structured_data",
                "Extract page metadata as structured JSON: title, description, canonical "
                "URL, OpenGraph fields and JSON-LD blocks.",
                toolParams({{"tab_index", "number"}}, {})};
    }
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override
    {
        WebView *view = resolveTab(args);
        if (!view) {
            done({false, "no_tab", "no such tab", {}});
            return;
        }
        const QString script = QStringLiteral(
            "(function(){"
            "const out={};"
            "out.title=document.title||'';"
            "const meta=(n)=>{const el=document.querySelector(n); return el?el.content||'':'';};"
            "out.description=meta('meta[name=\"description\"]');"
            "out.og_title=meta('meta[property=\"og:title\"]');"
            "out.og_image=meta('meta[property=\"og:image\"]');"
            "const canonical=document.querySelector('link[rel=\"canonical\"]');"
            "out.canonical=canonical?canonical.href:'';"
            "out.jsonld=Array.from(document.querySelectorAll('script[type=\"application/ld+json\"]'))"
            "  .map(s=>s.textContent.slice(0,4000));"
            "return JSON.stringify(out);"
            "})()");
        view->page()->runJavaScript(script, 0, [done](const QVariant &result) {
            const QJsonObject data = QJsonDocument::fromJson(result.toString().toUtf8()).object();
            done({true, {}, {}, data});
        });
    }
};

class GetTabListTool final : public PageToolBase
{
public:
    using PageToolBase::PageToolBase;
    ToolSpec spec() const override
    {
        return {"get_tab_list", "List open tabs with index, title and URL.",
                toolParams()};
    }
    void execute(const QJsonObject &, std::function<void(ToolResult)> done) override
    {
        TabManager *tabs = tabManager();
        if (!tabs) {
            done({false, "no_tabs", "tab manager unavailable", {}});
            return;
        }
        QJsonArray list;
        for (int i = 0; i < tabs->count(); ++i) {
            const TabInfo info = tabs->info(i);
            QJsonObject item;
            item.insert("index", i);
            item.insert("title", info.title);
            item.insert("url", info.url.toString());
            item.insert("active", i == tabs->currentIndex());
            list.append(item);
        }
        done({true, {}, {}, QJsonObject{{"tabs", list}}});
    }
};

} // namespace

void registerBuiltinTools(ToolRegistry *registry)
{
    registry->registerTool(new GetCurrentPageTool(registry));
    registry->registerTool(new GetPageContentTool(registry));
    registry->registerTool(new GetPageLinksTool(registry));
    registry->registerTool(new FindElementsTool(registry));
    registry->registerTool(new ClickElementTool(registry));
    registry->registerTool(new FillInputTool(registry));
    registry->registerTool(new SelectOptionTool(registry));
    registry->registerTool(new ScrollPageTool(registry));
    registry->registerTool(new NavigateToUrlTool(registry));
    registry->registerTool(new SwitchTabTool(registry));
    registry->registerTool(new CreateTabTool(registry));
    registry->registerTool(new CloseTabTool(registry));
    registry->registerTool(new TakeScreenshotTool(registry));
    registry->registerTool(new WaitForElementTool(registry));
    registry->registerTool(new ExtractStructuredDataTool(registry));
    registry->registerTool(new GetTabListTool(registry));
}
