#include "browser/mochurlscheme.hpp"

#include <QBuffer>
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
    if (url.host() == "newtab") {
        QBuffer *buffer = new QBuffer(job);
        buffer->setData(newTabHtml());
        connect(buffer, &QIODevice::aboutToClose, buffer, &QObject::deleteLater);
        buffer->open(QIODevice::ReadOnly);
        job->reply("text/html", buffer);
        return;
    }
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}

QByteArray MochUrlScheme::newTabHtml() const
{
    return QByteArrayLiteral(
        "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
        "body{background:#141517;color:#e8e8ea;font-family:system-ui,sans-serif;"
        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
        "h1{font-weight:500;font-size:22px}"
        "</style></head><body><h1>Moch</h1></body></html>");
}
