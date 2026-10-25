#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include "ai/modelcatalog.hpp"
#include "browser/openrouterauth.hpp"

class Settings;
class WebView;

// WebChannel bridge for the moch://setup onboarding wizard. All wizard state
// is persisted to Settings immediately so navigation away and back never
// loses progress.
class SetupBridge : public QObject
{
    Q_OBJECT
public:
    explicit SetupBridge(Settings *settings, WebView *view, QObject *parent = nullptr);

    Q_INVOKABLE QString theme() const;
    Q_INVOKABLE QJsonArray getUseCases() const;
    Q_INVOKABLE QJsonArray getSelectedUseCases() const;
    Q_INVOKABLE void setSelectedUseCases(const QJsonArray &ids);
    Q_INVOKABLE QJsonObject getPlatformStates() const;
    Q_INVOKABLE void setPlatformSignedIn(const QString &platformId, bool signedIn);
    Q_INVOKABLE void requestSignIn(const QString &platformId);

    Q_INVOKABLE void fetchModels();
    Q_INVOKABLE void saveAiConfig(const QString &mode, const QString &baseUrl,
                                  const QString &model);
    Q_INVOKABLE void saveApiKey(const QString &key);
    Q_INVOKABLE QString getAiSummary() const;
    Q_INVOKABLE void startOpenRouterOAuth();
    Q_INVOKABLE void finish();

    static QJsonArray useCaseCatalog();

signals:
    void modelsReady(const QJsonArray &models);
    void modelsFailed(const QString &reason);
    void oauthStarted(const QString &authUrl);
    void oauthResult(bool ok, const QString &message);
    void finished();

private:
    Settings *m_settings = nullptr;
    QPointer<WebView> m_view;
    ModelCatalog *m_catalog = nullptr;
    OpenRouterAuth *m_oauth = nullptr;
};
