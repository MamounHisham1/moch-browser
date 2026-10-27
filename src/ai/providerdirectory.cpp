#include "ai/providerdirectory.hpp"

#include "security/secretbox.hpp"

// Strong definitions for the weak vault bindings declared in
// ai/providerdirectory.hpp. Only targets that list this file in CMake get
// them: the moch target (add "ai/providerdirectory.cpp" to
// src/CMakeLists.txt) and any test target that also compiles
// security/secretbox.cpp. Everywhere else the weak symbols stay null and
// callers degrade to the legacy key file.
namespace ProviderDirectory {

bool mochVaultHas(const QString &name)
{
    const Security::SecretBox box;
    return box.has(name);
}

QString mochVaultValue(const QString &name)
{
    const Security::SecretBox box;
    return box.value(name);
}

void mochVaultPut(const QString &name, const QString &value)
{
    Security::SecretBox box;
    box.put(name, value);
}

} // namespace ProviderDirectory
