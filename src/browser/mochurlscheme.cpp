#include "browser/mochurlscheme.hpp"

#include <QBuffer>
#include <QFile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineProfile>
#include <QWebEngineUrlScheme>

void MochUrlScheme::registerScheme()
{
    QWebEngineUrlScheme scheme("moch");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
    QWebEngineUrlScheme::registerScheme(scheme);
}

MochUrlScheme::MochUrlScheme(QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
{
}

void MochUrlScheme::requestStarted(QWebEngineUrlRequestJob *job)
{
    const QUrl url = job->requestUrl();
    if (url.host() == "newtab" || url.host() == "setup") {
        QBuffer *buffer = new QBuffer(job);
        buffer->setData(url.host() == "setup" ? setupHtml() : newTabHtml());
        connect(buffer, &QIODevice::aboutToClose, buffer, &QObject::deleteLater);
        buffer->open(QIODevice::ReadOnly);
        job->reply("text/html", buffer);
        return;
    }
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}

QByteArray MochUrlScheme::newTabHtml() const
{
    QFile page(":/pages/newtab.html");
    if (!page.open(QIODevice::ReadOnly)) {
        qWarning("moch: cannot load new-tab page resource");
        return QByteArrayLiteral("<html><body><h1>Moch</h1></body></html>");
    }
    return page.readAll();
}

QByteArray MochUrlScheme::setupHtml() const
{
    QFile page(":/pages/setup.html");
    if (!page.open(QIODevice::ReadOnly)) {
        qWarning("moch: cannot load setup page resource");
        return QByteArrayLiteral("<html><body><h1>Moch setup</h1></body></html>");
    }
    return page.readAll();
}
