#include "browser/setupbridge.hpp"

#include <QFile>
#include <QJsonDocument>

#include "browser/webview.hpp"
#include "storage/settings.hpp"

namespace {
struct Platform {
    const char *id;
    const char *name;
    const char *loginUrl;
};

struct UseCase {
    const char *id;
    const char *title;
    const char *description;
    const char *emoji;
    std::initializer_list<Platform> platforms;
};

const UseCase kUseCases[] = {
    {"jobs", "Job hunting", "Search, apply and track applications",
     "💼",
     {{"linkedin", "LinkedIn", "https://www.linkedin.com/login"},
      {"indeed", "Indeed", "https://secure.indeed.com/account/login"},
      {"glassdoor", "Glassdoor", "https://www.glassdoor.com/profile/login_input.htm"},
      {"wuzzuf", "Wuzzuf", "https://wuzzuf.net/login"},
      {"bayt", "Bayt", "https://www.bayt.com/en/login/"},
      {"upwork", "Upwork", "https://www.upwork.com/ab/account-security/login"}}},
    {"social", "Social media", "Post, reply and monitor your accounts",
     "🌐",
     {{"x", "X (Twitter)", "https://x.com/i/flow/login"},
      {"instagram", "Instagram", "https://www.instagram.com/accounts/login/"},
      {"facebook", "Facebook", "https://www.facebook.com/login"},
      {"tiktok", "TikTok", "https://www.tiktok.com/login/phone-or-email/email"},
      {"threads", "Threads", "https://www.threads.net/login"},
      {"bluesky", "Bluesky", "https://bsky.app/"}}},
    {"mail", "Email & messaging", "Draft, triage and summarize conversations",
     "✉️",
     {{"gmail", "Gmail",
       "https://accounts.google.com/ServiceLogin?continue=https://mail.google.com"},
      {"outlook", "Outlook", "https://login.live.com/"},
      {"whatsapp", "WhatsApp Web", "https://web.whatsapp.com/"},
      {"telegram", "Telegram", "https://web.telegram.org/a/"},
      {"slack", "Slack", "https://slack.com/signin"}}},
    {"shopping", "Shopping & deals", "Compare prices and track products",
     "🛍️",
     {{"amazon", "Amazon", "https://www.amazon.com/ap/signin"},
      {"ebay", "eBay", "https://signin.ebay.com/"},
      {"aliexpress", "AliExpress", "https://login.aliexpress.com/"},
      {"noon", "Noon", "https://www.noon.com/"},
      {"jumia", "Jumia", "https://accounts.jumia.com/"}}},
    {"research", "Research & summaries", "Deep dives with sourced answers",
     "🔬",
     {{"scholar", "Google Scholar", "https://scholar.google.com/"},
      {"wikipedia", "Wikipedia", "https://en.wikipedia.org/"},
      {"arxiv", "arXiv", "https://arxiv.org/"},
      {"semanticscholar", "Semantic Scholar", "https://www.semanticscholar.org/"}}},
    {"news", "News & reading", "Daily briefings and article digests",
     "📰",
     {{"gnews", "Google News", "https://news.google.com/"},
      {"bbc", "BBC", "https://www.bbc.com/"},
      {"reuters", "Reuters", "https://www.reuters.com/"},
      {"medium", "Medium", "https://medium.com/m/signin"},
      {"substack", "Substack", "https://substack.com/sign-in"}}},
    {"travel", "Travel planning", "Compare trips and book with oversight",
     "✈️",
     {{"booking", "Booking.com", "https://account.booking.com/sign-in"},
      {"airbnb", "Airbnb", "https://www.airbnb.com/login"},
      {"expedia", "Expedia", "https://www.expedia.com/user/login"},
      {"skyscanner", "Skyscanner", "https://www.skyscanner.net/"},
      {"gflights", "Google Flights", "https://www.google.com/travel/flights"}}},
    {"learning", "Learning & courses", "Study plans and course help",
     "🎓",
     {{"coursera", "Coursera", "https://www.coursera.org/?authMode=login"},
      {"udemy", "Udemy", "https://www.udemy.com/join/login-popup/"},
      {"khan", "Khan Academy", "https://www.khanacademy.org/login"},
      {"duolingo", "Duolingo", "https://www.duolingo.com/log-in"},
      {"edx", "edX", "https://courses.edx.org/login"}}},
    {"finance", "Finance & payments", "Track portfolios and prepare payments",
     "💳",
     {{"paypal", "PayPal", "https://www.paypal.com/signin"},
      {"coinbase", "Coinbase", "https://www.coinbase.com/signin"},
      {"binance", "Binance", "https://accounts.binance.com/en/login"},
      {"tradingview", "TradingView", "https://www.tradingview.com/accounts-signin/"}}},
    {"freelance", "Freelance work", "Gigs, proposals and client updates",
     "🧑‍💻",
     {{"upwork", "Upwork", "https://www.upwork.com/ab/account-security/login"},
      {"fiverr", "Fiverr", "https://www.fiverr.com/login"},
      {"freelancer", "Freelancer", "https://www.freelancer.com/login"},
      {"toptal", "Toptal", "https://www.toptal.com/users/login"},
      {"khamsat", "Khamsat", "https://khamsat.com/login"}}},
    {"realestate", "Real estate", "Hunt listings and book viewings",
     "🏠",
     {{"zillow", "Zillow", "https://www.zillow.com/login/"},
      {"bayut", "Bayut", "https://www.bayut.com/login/"},
      {"propertyfinder", "Property Finder", "https://www.propertyfinder.ae/login"},
      {"aqarmap", "Aqarmap", "https://aqarmap.com/eg/en/login/"}}},
    {"food", "Food & groceries", "Orders, reorders and price checks",
     "🍔",
     {{"talabat", "Talabat", "https://www.talabat.com/login"},
      {"ubereats", "Uber Eats", "https://www.ubereats.com/login"},
      {"instashop", "InstaShop", "https://instashop.com/login"},
      {"carrefour", "Carrefour", "https://www.carrefour.com/"}}},
    {"entertainment", "Entertainment", "Find what to watch and listen to",
     "🎬",
     {{"netflix", "Netflix", "https://www.netflix.com/login"},
      {"spotify", "Spotify", "https://accounts.spotify.com/login"},
      {"youtube", "YouTube",
       "https://accounts.google.com/ServiceLogin?continue=https://www.youtube.com/"},
      {"shahid", "Shahid", "https://shahid.mbc.net/en/sign-in"},
      {"anghami", "Anghami", "https://www.anghami.com/login"}}},
    {"dev", "Developer work", "Repos, issues and release tasks",
     "🧩",
     {{"github", "GitHub", "https://github.com/login"},
      {"gitlab", "GitLab", "https://gitlab.com/users/sign_in"},
      {"stackoverflow", "Stack Overflow", "https://stackoverflow.com/users/login"},
      {"hackernews", "Hacker News", "https://news.ycombinator.com/login"}}},
    {"fitness", "Sports & fitness", "Scores, plans and training logs",
     "🏋️",
     {{"espn", "ESPN", "https://www.espn.com/"},
      {"fotmob", "FotMob", "https://www.fotmob.com/login"},
      {"strava", "Strava", "https://www.strava.com/login"},
      {"myfitnesspal", "MyFitnessPal", "https://www.myfitnesspal.com/account/login"}}},
    {"custom", "Something else", "Moch works on any site you open",
     "✨", {}},
};
} // namespace

QJsonArray SetupBridge::useCaseCatalog()
{
    QJsonArray out;
    for (const UseCase &useCase : kUseCases) {
        QJsonObject entry;
        entry.insert("id", useCase.id);
        entry.insert("title", useCase.title);
        entry.insert("description", useCase.description);
        entry.insert("emoji", useCase.emoji);
        QJsonArray platforms;
        for (const Platform &platform : useCase.platforms) {
            platforms.append(QJsonObject{{"id", platform.id},
                                         {"name", platform.name},
                                         {"loginUrl", platform.loginUrl}});
        }
        entry.insert("platforms", platforms);
        out.append(entry);
    }
    return out;
}

SetupBridge::SetupBridge(Settings *settings, WebView *view, QObject *parent)
    : QObject(parent), m_settings(settings), m_view(view),
      m_catalog(new ModelCatalog(this)), m_oauth(new OpenRouterAuth(this))
{
    connect(m_catalog, &ModelCatalog::modelsReady, this, [this](const QJsonArray &models) {
        emit modelsReady(models.toVariantList());
    });
    connect(m_catalog, &ModelCatalog::modelsFailed, this, [this](const QString &reason) {
        emit modelsFailed(reason);
        emit modelsReady(ModelCatalog::fallbackModels().toVariantList());
    });
    connect(m_oauth, &OpenRouterAuth::codeReceived, this, [this](const QString &code) {
        emit oauthResult(true, QStringLiteral("OpenRouter approved, exchanging key…"));
        m_oauth->exchangeCode(code);
    });
    connect(m_oauth, &OpenRouterAuth::keyReady, this, [this](const QString &key) {
        m_settings->saveApiKey(key);
        if (m_view)
            m_view->load(QUrl("moch://setup"));
        emit oauthResult(true, QStringLiteral("Signed in to OpenRouter."));
    });
    connect(m_oauth, &OpenRouterAuth::failed, this, [this](const QString &reason) {
        if (m_view)
            m_view->load(QUrl("moch://setup"));
        emit oauthResult(false, reason);
    });
}

QString SetupBridge::theme() const
{
    return m_settings->theme();
}

QVariantList SetupBridge::getUseCases() const
{
    return useCaseCatalog().toVariantList();
}

QVariantList SetupBridge::getSelectedUseCases() const
{
    const QVariant stored = m_settings->get("use_cases");
    return stored.isValid() ? stored.toList() : QVariantList();
}

void SetupBridge::setSelectedUseCases(const QVariantList &ids)
{
    m_settings->set("use_cases", ids);
}

QVariantMap SetupBridge::getPlatformStates() const
{
    QVariantMap out;
    const QVariant stored = m_settings->get("platforms");
    const QVariantMap map = stored.isValid() ? stored.toMap() : QVariantMap();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QVariantMap entry = it.value().toMap();
        out.insert(it.key(), entry.value("signed_in").toBool());
    }
    return out;
}

void SetupBridge::setPlatformSignedIn(const QString &platformId, bool signedIn)
{
    QVariantMap stored = m_settings->get("platforms").toMap();
    QVariantMap entry = stored.value(platformId).toMap();
    entry.insert("signed_in", signedIn);
    stored.insert(platformId, entry);
    m_settings->set("platforms", stored);
}

void SetupBridge::requestSignIn(const QString &platformId)
{
    for (const UseCase &useCase : kUseCases) {
        for (const Platform &platform : useCase.platforms) {
            if (platformId == platform.id && m_view) {
                m_view->load(QUrl(QString::fromLatin1(platform.loginUrl)));
                return;
            }
        }
    }
}

void SetupBridge::fetchModels()
{
    m_catalog->fetchModels();
}

void SetupBridge::saveAiConfig(const QString &mode, const QString &baseUrl,
                               const QString &model)
{
    m_settings->set("ai.provider", mode);
    m_settings->set("ai.base_url", baseUrl);
    m_settings->set("ai.model", model);
}

void SetupBridge::saveApiKey(const QString &key)
{
    if (!key.trimmed().isEmpty())
        m_settings->saveApiKey(key.trimmed());
}

QString SetupBridge::getAiSummary() const
{
    return QJsonDocument(QJsonObject{{"model", m_settings->get("ai.model").toString()},
                                     {"hasKey", !m_settings->apiKey().isEmpty()}})
        .toJson(QJsonDocument::Compact);
}

void SetupBridge::startOpenRouterOAuth()
{
    if (!m_oauth->start()) {
        emit oauthResult(false, QStringLiteral("could not start local sign-in listener"));
        return;
    }
    emit oauthStarted(m_oauth->authUrl());
    if (m_view)
        m_view->load(QUrl(m_oauth->authUrl()));
}

void SetupBridge::finish()
{
    m_settings->set("setup_complete", true);
    emit finished();
}
