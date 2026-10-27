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
    // providerId is a ProviderDirectory id ("zai", "openai", …),
    // "openrouter", or "openai-compat" for legacy manual wiring (explicit
    // base URL, raw model id). Native ids strip the catalog slug prefix
    // from the model and leave the base URL for request-time resolution;
    // a non-empty baseUrl overrides the directory default.
    Q_INVOKABLE void saveAiConfig(const QString &providerId, const QString &baseUrl,
                                  const QString &model);
    // Stores the key in the SecretBox vault under
    // ProviderDirectory::settingsKeyFor(providerId).
    Q_INVOKABLE void saveProviderKey(const QString &providerId, const QString &key);
    Q_INVOKABLE bool hasProviderKey(const QString &providerId) const;
    // Legacy global key (env / 0600 key file chain), still used by the
    // OpenRouter OAuth flow and custom manual wiring.
    Q_INVOKABLE void saveApiKey(const QString &key);
    Q_INVOKABLE void setCodingPlan(bool codingPlan);
    // Detects the native provider for an OpenRouter catalog model id
    // ("z-ai/glm-4.6"): JSON {model, slug, native, provider, name,
    // keyLabel, docsUrl, needsKey, codingPlan}. Non-native slugs report
    // native=false (served via OpenRouter).
    Q_INVOKABLE QString resolveModelProvider(const QString &modelId) const;
    // JSON map of catalog slug → native provider id ("" when the slug has
    // no native endpoint and stays OpenRouter-routed).
    Q_INVOKABLE QString getSlugMap() const;
    Q_INVOKABLE QString getAiSummary() const;
    Q_INVOKABLE void startOpenRouterOAuth();
    // Opens a URL (e.g. a provider key console) in this view, same as the
    // platform sign-in flow; the wizard state persists in Settings.
    Q_INVOKABLE void openExternal(const QString &url);
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
