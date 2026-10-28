#include "browser/scrollbarstyle.hpp"

#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

namespace {

constexpr auto kScriptName = "moch-scrollbars";
constexpr auto kStyleId = "__moch_scrollbars";

// The style element is attached as soon as the document has a head. The
// script runs at DocumentCreation, which on some pages is before <head>
// exists, so a mutation observer waits for the first child and gives up at
// DOMContentLoaded (documents without a head still get the style).
QString scriptSource(const QString &css)
{
    const QString literal = QStringLiteral("\"%1\"")
                                     .arg(QString(css)
                                                  .replace(QLatin1Char('\\'), QLatin1String("\\\\"))
                                                  .replace(QLatin1Char('"'), QLatin1String("\\\""))
                                                  .replace(QLatin1Char('\n'), QLatin1String("\\n")));
    return QStringLiteral(R"JS(
(function () {
    var CSS = %1;
    var ID = "%2";
    function apply() {
        var style = document.getElementById(ID);
        if (style) {
            style.textContent = CSS;
            return true;
        }
        var parent = document.head || document.documentElement;
        if (!parent)
            return false;
        style = document.createElement("style");
        style.id = ID;
        style.type = "text/css";
        style.textContent = CSS;
        parent.appendChild(style);
        return true;
    }
    if (apply())
        return;
    var observer = new MutationObserver(function () {
        if (apply())
            observer.disconnect();
    });
    observer.observe(document, { childList: true, subtree: true });
    document.addEventListener("DOMContentLoaded", function () {
        apply();
        observer.disconnect();
    });
})();
)JS")
            .arg(literal, QLatin1String(kStyleId));
}

QString updateSource(const QString &css)
{
    const QString literal = QStringLiteral("\"%1\"")
                                     .arg(QString(css)
                                                  .replace(QLatin1Char('\\'), QLatin1String("\\\\"))
                                                  .replace(QLatin1Char('"'), QLatin1String("\\\""))
                                                  .replace(QLatin1Char('\n'), QLatin1String("\\n")));
    return QStringLiteral("(function(){var s=document.getElementById(\"%1\");"
                          "if(s)s.textContent=%2;})();")
            .arg(QLatin1String(kStyleId), literal);
}

} // namespace

QString ScrollbarStyle::css(const QString &theme)
{
    const bool dark = theme != QLatin1String("light");
    const QString accent = dark ? QStringLiteral("#5b8def") : QStringLiteral("#3d6ea5");

    // The thumb is drawn from currentColor, so it adapts to the page it sits
    // on instead of assuming the app theme matches the site. The explicit
    // rgba is the fallback for renderers without color-mix().
    return QStringLiteral(R"CSS(
::-webkit-scrollbar {
    width: 12px;
    height: 12px;
    background: transparent;
}
::-webkit-scrollbar-track,
::-webkit-scrollbar-corner {
    background: transparent;
}
::-webkit-scrollbar-thumb {
    min-width: 40px;
    min-height: 40px;
    border: 3px solid transparent;
    border-radius: 12px;
    background-color: rgba(140, 146, 160, 0.42);
    background-color: color-mix(in srgb, currentColor 32%, transparent);
    background-clip: content-box;
}
::-webkit-scrollbar-thumb:hover {
    border-width: 2px;
    background-color: rgba(140, 146, 160, 0.66);
    background-color: color-mix(in srgb, currentColor 50%, transparent);
}
::-webkit-scrollbar-thumb:active {
    border-width: 2px;
    background-color: %1;
}
::-webkit-scrollbar-button {
    display: none;
    width: 0;
    height: 0;
}
)CSS").arg(accent);
}

void ScrollbarStyle::install(QWebEnginePage *page, const QString &theme)
{
    if (!page)
        return;

    const QString stylesheet = css(theme);
    QWebEngineScriptCollection &scripts = page->scripts();

    // Replace rather than add: a theme switch must not stack scripts, and the
    // set of open pages is small enough that a scan is free.
    const QList<QWebEngineScript> existing = scripts.toList();
    for (const QWebEngineScript &script : existing) {
        if (script.name() == QLatin1String(kScriptName))
            scripts.remove(script);
    }

    QWebEngineScript script;
    script.setName(QLatin1String(kScriptName));
    script.setSourceCode(scriptSource(stylesheet));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    scripts.insert(script);

    // The document already on screen keeps the old style element; rewrite it
    // in place so toggling the theme does not need a reload.
    page->runJavaScript(updateSource(stylesheet));
}
