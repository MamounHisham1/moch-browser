#include "ai/providerregistry.hpp"

#include "ai/openaicompatprovider.hpp"
#include "storage/settings.hpp"

QHash<Settings *, LlmProvider *> &ProviderRegistry::cache()
{
    static QHash<Settings *, LlmProvider *> cache;
    return cache;
}

LlmProvider *ProviderRegistry::providerFor(Settings *settings)
{
    const auto it = cache().constFind(settings);
    if (it != cache().constEnd())
        return it.value();

    const QString providerId = settings->get("ai.provider", "openai-compat").toString();
    LlmProvider *provider = nullptr;
    if (providerId == "openai-compat" || !provider)
        provider = new OpenAiCompatProvider(settings, settings);
    cache().insert(settings, provider);
    return provider;
}
