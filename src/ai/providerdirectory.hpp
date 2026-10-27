#pragma once

#include <QList>
#include <QString>

#include "storage/settings.hpp"

// Static directory of native OpenAI-compatible providers Moch reaches
// directly. The OpenRouter catalog stays discovery-only: it supplies model
// names for the setup wizard, but a native provider's requests go to that
// provider's own endpoint with the provider's own key.
//
// Provider keys live in the Security::SecretBox vault under the name
// settingsKeyFor(id) (e.g. "provider-key:zai"); they are never written to
// settings.json. The vault is bound through weak symbols (mochVault*) that
// providerdirectory.cpp defines: test targets which compile
// openaicompatprovider.cpp or setupbridge.cpp without
// security/secretbox.cpp keep linking (the weak symbols stay null and the
// helpers degrade to the legacy key file), while the moch target — once
// providerdirectory.cpp is wired into CMake — gets real vault storage and
// automatic transcript redaction via SecretBox::allValues().
namespace ProviderDirectory {

struct NativeProvider
{
    QString id;
    QString name;
    QString baseUrl;      // standard pay-per-token endpoint
    QString baseUrlCoding; // coding-plan endpoint ("zai" only, else empty)
    QString keyLabel;     // e.g. "Z.AI API key"; empty when no key is needed
    QString docsUrl;      // where to obtain the key
    bool needsKey = true;
};

// Weak bindings to Security::SecretBox. Defined in providerdirectory.cpp;
// unresolved weak symbols evaluate to null on ELF and callers fall back to
// the legacy key file so no key is ever lost.
#if defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))
#define MOCH_VAULT_WEAK __attribute__((weak))
#else
#define MOCH_VAULT_WEAK
#endif
bool mochVaultHas(const QString &name) MOCH_VAULT_WEAK;
QString mochVaultValue(const QString &name) MOCH_VAULT_WEAK;
void mochVaultPut(const QString &name, const QString &value) MOCH_VAULT_WEAK;
#undef MOCH_VAULT_WEAK

inline QList<NativeProvider> all()
{
    return {
        {QStringLiteral("zai"), QStringLiteral("Z.ai"),
         QStringLiteral("https://api.z.ai/api/paas/v4"),
         QStringLiteral("https://api.z.ai/api/coding/paas/v4"),
         QStringLiteral("Z.AI API key"),
         QStringLiteral("https://z.ai/manage-api-key/apikey"), true},
        {QStringLiteral("openai"), QStringLiteral("OpenAI"),
         QStringLiteral("https://api.openai.com/v1"), QString(),
         QStringLiteral("OpenAI API key"),
         QStringLiteral("https://platform.openai.com/api-keys"), true},
        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek"),
         QStringLiteral("https://api.deepseek.com"), QString(),
         QStringLiteral("DeepSeek API key"),
         QStringLiteral("https://platform.deepseek.com/api_keys"), true},
        {QStringLiteral("groq"), QStringLiteral("Groq"),
         QStringLiteral("https://api.groq.com/openai/v1"), QString(),
         QStringLiteral("Groq API key"),
         QStringLiteral("https://console.groq.com/keys"), true},
        {QStringLiteral("mistral"), QStringLiteral("Mistral AI"),
         QStringLiteral("https://api.mistral.ai/v1"), QString(),
         QStringLiteral("Mistral API key"),
         QStringLiteral("https://console.mistral.ai/api-keys"), true},
        {QStringLiteral("together"), QStringLiteral("Together AI"),
         QStringLiteral("https://api.together.xyz/v1"), QString(),
         QStringLiteral("Together API key"),
         QStringLiteral("https://api.together.xyz/settings/api-keys"), true},
        {QStringLiteral("xai"), QStringLiteral("xAI"),
         QStringLiteral("https://api.x.ai/v1"), QString(),
         QStringLiteral("xAI API key"),
         QStringLiteral("https://console.x.ai"), true},
        {QStringLiteral("ollama"), QStringLiteral("Ollama (local)"),
         QStringLiteral("http://localhost:11434/v1"), QString(), QString(),
         QStringLiteral("https://ollama.com"), false},
        {QStringLiteral("lmstudio"), QStringLiteral("LM Studio (local)"),
         QStringLiteral("http://localhost:1234/v1"), QString(), QString(),
         QStringLiteral("https://lmstudio.ai"), false},
    };
}

// Directory entry by id; unknown ids return an empty id.
inline NativeProvider byId(const QString &id)
{
    for (const NativeProvider &provider : all()) {
        if (provider.id == id)
            return provider;
    }
    return NativeProvider();
}

// Maps the slug before "/" in an OpenRouter catalog model id ("z-ai" from
// "z-ai/glm-4.6") to the native provider. Slugs without an
// OpenAI-compatible native endpoint (anthropic, google, meta-llama, …)
// return an empty id: those stay OpenRouter-routed.
inline NativeProvider forSlug(const QString &openrouterSlug)
{
    QString id;
    if (openrouterSlug == QLatin1String("z-ai"))
        id = QStringLiteral("zai");
    else if (openrouterSlug == QLatin1String("mistralai"))
        id = QStringLiteral("mistral");
    else if (openrouterSlug == QLatin1String("openai")
             || openrouterSlug == QLatin1String("deepseek")
             || openrouterSlug == QLatin1String("groq")
             || openrouterSlug == QLatin1String("together")
             || openrouterSlug == QLatin1String("x-ai"))
        id = openrouterSlug == QLatin1String("x-ai") ? QStringLiteral("xai")
                                                     : openrouterSlug;
    return byId(id);
}

// Endpoint for a provider: "zai" returns the coding-plan endpoint when
// codingPlan is set, else the general API; others have one endpoint.
inline QString defaultBaseUrl(const QString &id, bool codingPlan)
{
    const NativeProvider provider = byId(id);
    if (!provider.baseUrlCoding.isEmpty() && codingPlan)
        return provider.baseUrlCoding;
    return provider.baseUrl;
}

// SecretBox vault name for a provider's key.
inline QString settingsKeyFor(const QString &providerId)
{
    return QStringLiteral("provider-key:") + providerId;
}

// Removes the OpenRouter catalog slug prefix ("z-ai/glm-4.6" → "glm-4.6");
// ids without a slash are returned unchanged. Native providers use bare
// model ids; OpenRouter keeps the full slug.
inline QString stripSlugPrefix(const QString &modelId)
{
    const int slash = modelId.indexOf('/');
    return slash > 0 ? modelId.mid(slash + 1) : modelId;
}

// Base URLs that count as "not explicitly chosen": the settings default and
// the old wizard default. Requests then use the directory endpoint for the
// selected provider instead of a stale default.
inline bool isGenericBaseUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    return trimmed.isEmpty()
           || trimmed == QLatin1String("https://api.openai.com/v1")
           || trimmed == QLatin1String("https://openrouter.ai/api/v1");
}

// Key for a provider: SecretBox vault first, then the legacy chain
// (ai.api_key_env environment variable, else the 0600 key file).
inline QString providerKey(Settings *settings, const QString &providerId)
{
    if (mochVaultHas) {
        const QString name = settingsKeyFor(providerId);
        if (mochVaultHas(name))
            return mochVaultValue(name);
    }
    return settings->apiKey();
}

inline bool hasProviderKey(Settings *settings, const QString &providerId)
{
    return !providerKey(settings, providerId).isEmpty();
}

// Stores a provider key in the SecretBox vault; when the vault binding is
// unavailable (targets not linked with secretbox.cpp) it falls back to the
// legacy key file so a saved key is never dropped. Empty keys are ignored.
inline void saveProviderKey(Settings *settings, const QString &providerId,
                            const QString &key)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty())
        return;
    if (mochVaultPut) {
        mochVaultPut(settingsKeyFor(providerId), trimmed);
        return;
    }
    settings->saveApiKey(trimmed);
}

} // namespace ProviderDirectory
