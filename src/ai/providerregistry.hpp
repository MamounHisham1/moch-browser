#pragma once

#include <QHash>

#include "ai/provider.hpp"

class Settings;

class ProviderRegistry
{
public:
    static LlmProvider *providerFor(Settings *settings);

private:
    static QHash<Settings *, LlmProvider *> &cache();
};
