#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include "ai/modelcatalog.hpp"
#include "browser/openrouterauth.hpp"

class BookmarkStore;
class HistoryStore;
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

    // Stores used by the browser-import section; optional (the wizard
    // works without them, the import section then stays unavailable).
    void setStores(HistoryStore *history, BookmarkStore *bookmarks);

    Q_INVOKABLE QString theme() const;
    // QVariantList/Map cross the WebChannel as real JS arrays/objects;
    // QJsonArray would arrive as a non-iterable handle.
    Q_INVOKABLE QVariantList getUseCases() const;
    Q_INVOKABLE QVariantList getSelectedUseCases() const;
    Q_INVOKABLE void setSelectedUseCases(const QVariantList &ids);
    Q_INVOKABLE QVariantMap getPlatformStates() const;
    Q_INVOKABLE void setPlatformSignedIn(const QString &platformId, bool signedIn);
    Q_INVOKABLE void requestSignIn(const QString &platformId);

    // Import section: detectable other browsers with entry counts, and a
    // one-shot import returning {ok, bookmarks, history, duplicates,
    // warnings, error}.
    Q_INVOKABLE QVariantList getImportBrowsers() const;
    Q_INVOKABLE QVariantMap importBrowser(const QString &browserId);

    Q_INVOKABLE void fetchModels();
    Q_INVOKABLE void saveAiConfig(const QString &mode, const QString &baseUrl,
                                  const QString &model);
    Q_INVOKABLE void saveApiKey(const QString &key);
    Q_INVOKABLE QString getAiSummary() const;
    Q_INVOKABLE void startOpenRouterOAuth();
    Q_INVOKABLE void finish();

    static QJsonArray useCaseCatalog();

signals:
    void modelsReady(const QVariantList &models);
    void modelsFailed(const QString &reason);
    void oauthStarted(const QString &authUrl);
    void oauthResult(bool ok, const QString &message);
    void finished();

private:
    Settings *m_settings = nullptr;
    QPointer<WebView> m_view;
    ModelCatalog *m_catalog = nullptr;
    OpenRouterAuth *m_oauth = nullptr;
    HistoryStore *m_history = nullptr;
    BookmarkStore *m_bookmarks = nullptr;
};
