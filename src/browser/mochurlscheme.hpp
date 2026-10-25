#pragma once

#include <QWebEngineUrlSchemeHandler>

class MochUrlScheme : public QWebEngineUrlSchemeHandler
{
    Q_OBJECT
public:
    static void registerScheme();
    explicit MochUrlScheme(QObject *parent = nullptr);
    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    QByteArray newTabHtml() const;
};
