#include "ui/settingsdialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ai/providerdirectory.hpp"
#include "browser/urlresolver.hpp"
#include "security/secretbox.hpp"
#include "storage/settings.hpp"
#include "ui/secretboxdialog.hpp"

SettingsDialog::SettingsDialog(Settings *settings, QWidget *parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(tr("Moch Settings"));
    setModal(true);
    resize(460, 420);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_theme = new QComboBox();
    m_theme->addItem(tr("Dark"), "dark");
    m_theme->addItem(tr("Light"), "light");
    form->addRow(tr("Theme"), m_theme);

    m_searchEngine = new QComboBox();
    for (const QString &key : UrlResolver::searchEngineKeys())
        m_searchEngine->addItem(UrlResolver::searchEngineName(key), key);
    form->addRow(tr("Search engine"), m_searchEngine);

    m_startup = new QComboBox();
    m_startup->addItem(tr("Open new tab"), "new_tab");
    m_startup->addItem(tr("Continue where you left off (new tab)"), "continue");
    form->addRow(tr("On startup"), m_startup);

    m_provider = new QComboBox();
    for (const ProviderDirectory::NativeProvider &provider : ProviderDirectory::all())
        m_provider->addItem(provider.name, provider.id);
    m_provider->addItem(tr("OpenRouter"), QStringLiteral("openrouter"));
    m_provider->addItem(tr("Custom (manual base URL)"), QStringLiteral("openai-compat"));
    form->addRow(tr("AI provider"), m_provider);

    m_codingPlan = new QCheckBox(tr("Use the GLM Coding Plan endpoint (works with a Z.ai "
                                    "coding subscription; untick for the standard API)"));
    form->addRow(QString(), m_codingPlan);

    m_baseUrl = new QLineEdit();
    form->addRow(tr("AI base URL"), m_baseUrl);
    m_model = new QLineEdit();
    form->addRow(tr("Model"), m_model);
    m_apiKey = new QLineEdit();
    m_apiKey->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Provider API key"), m_apiKey);
    m_keyState = new QLabel();
    form->addRow(QString(), m_keyState);
    connect(m_provider, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshAiPane(); });

    auto *secretBox = new Security::SecretBox(this);
    auto *secretBoxButton = new QPushButton(tr("Secret Box…"));
    secretBoxButton->setToolTip(tr("Manage the named secrets the agent can fill into "
                                   "pages with your approval"));
    connect(secretBoxButton, &QPushButton::clicked, this, [this, secretBox] {
        SecretBoxDialog dialog(secretBox, this);
        dialog.exec();
    });
    form->addRow(QString(), secretBoxButton);

    m_permission = new QComboBox();
    m_permission->addItem(tr("Read only — no page actions"), "readonly");
    m_permission->addItem(tr("Cautious — ask before sensitive actions"), "cautious");
    m_permission->addItem(tr("Autonomous — ask only for purchases"), "autonomous");
    form->addRow(tr("Agent permission level"), m_permission);

    m_maxTaskSeconds = new QSpinBox();
    m_maxTaskSeconds->setRange(0, 36000);
    m_maxTaskSeconds->setSuffix(" s");
    m_maxTaskSeconds->setSpecialValueText(tr("No limit"));
    m_maxTaskSeconds->setSingleStep(30);
    form->addRow(tr("Max task duration"), m_maxTaskSeconds);
    m_maxToolCalls = new QSpinBox();
    m_maxToolCalls->setRange(5, 500);
    form->addRow(tr("Max tool calls per task"), m_maxToolCalls);

    m_confirmDownloads = new QCheckBox(tr("Confirm before downloading files"));
    form->addRow(QString(), m_confirmDownloads);
    m_doNotTrack = new QCheckBox(tr("Send Do Not Track header"));
    form->addRow(QString(), m_doNotTrack);
    m_sendPageContent = new QCheckBox(tr("Allow sending page content to the AI provider"));
    form->addRow(QString(), m_sendPageContent);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    load();
}

void SettingsDialog::load()
{
    m_theme->setCurrentIndex(m_theme->findData(m_settings->theme()));
    m_searchEngine->setCurrentIndex(
        m_searchEngine->findData(m_settings->searchEngine()));
    m_startup->setCurrentIndex(
        m_startup->findData(m_settings->get("startup_behavior", "new_tab").toString()));
    const QString providerId = m_settings->get("ai.provider").toString();
    const int providerIndex = m_provider->findData(
        m_provider->findData(providerId) < 0 ? QStringLiteral("openai-compat") : providerId);
    m_provider->setCurrentIndex(providerIndex < 0 ? m_provider->count() - 1 : providerIndex);
    m_codingPlan->setChecked(m_settings->get("ai.coding_plan", true).toBool());
    const QString storedBaseUrl = m_settings->get("ai.base_url").toString();
    m_baseUrl->setText(ProviderDirectory::isGenericBaseUrl(storedBaseUrl)
                           ? QString()
                           : storedBaseUrl);
    m_model->setText(m_settings->get("ai.model").toString());
    refreshAiPane();
    m_permission->setCurrentIndex(m_permission->findData(
        m_settings->get("agent.permission_level", "cautious").toString()));
    m_maxTaskSeconds->setValue(m_settings->get("agent.max_task_seconds", 0).toInt());
    m_maxToolCalls->setValue(m_settings->get("agent.max_tool_calls", 80).toInt());
    m_confirmDownloads->setChecked(
        m_settings->get("agent.confirm_downloads", true).toBool());
    m_doNotTrack->setChecked(m_settings->get("privacy.do_not_track", true).toBool());
    m_sendPageContent->setChecked(
        m_settings->get("privacy.send_page_content", true).toBool());
}

QString SettingsDialog::currentProviderId() const
{
    return m_provider->currentData().toString();
}

void SettingsDialog::refreshAiPane()
{
    const QString providerId = currentProviderId();
    const bool custom = providerId == QLatin1String("openai-compat");
    const ProviderDirectory::NativeProvider provider =
        custom ? ProviderDirectory::NativeProvider() : ProviderDirectory::byId(providerId);
    const bool known = custom || !provider.id.isEmpty();

    m_codingPlan->setVisible(providerId == QLatin1String("zai"));
    m_baseUrl->setEnabled(custom);
    m_baseUrl->setPlaceholderText(
        custom
            ? tr("https://your-endpoint.example/v1")
            : tr("automatic — %1").arg(known
                                           ? ProviderDirectory::defaultBaseUrl(
                                               providerId,
                                               m_settings->get("ai.coding_plan", true)
                                                   .toBool())
                                           : QStringLiteral("https://openrouter.ai/api/v1")));
    if (custom) {
        m_apiKey->setPlaceholderText(
            tr("Stored outside settings; empty keeps existing key"));
    } else if (provider.id.isEmpty()) {
        m_apiKey->setPlaceholderText(tr("OpenRouter key (sk-or-…); empty keeps existing"));
    } else if (provider.needsKey) {
        m_apiKey->setPlaceholderText(tr("%1; empty keeps existing key")
                                         .arg(provider.keyLabel));
    } else {
        m_apiKey->setPlaceholderText(tr("Not needed — this provider requires no key"));
    }

    // Saved-key state only; the stored value itself is never displayed.
    if (known && ProviderDirectory::hasProviderKey(m_settings, providerId)) {
        m_keyState->setText(tr("✔ Key saved in the Secret Box (%1)")
                                .arg(ProviderDirectory::settingsKeyFor(providerId)));
        m_keyState->setStyleSheet("color: #6fbf73;");
    } else if (known && !provider.id.isEmpty() && !provider.needsKey) {
        m_keyState->clear();
    } else {
        m_keyState->setText(tr("No key saved yet"));
        m_keyState->setStyleSheet("color: #9a9da6;");
    }
}

void SettingsDialog::save()
{
    const QString theme = m_theme->currentData().toString();
    const bool themeDidChange = theme != m_settings->theme();
    m_settings->set("theme", theme);
    m_settings->set("search_engine", m_searchEngine->currentData().toString());
    m_settings->set("startup_behavior", m_startup->currentData().toString());

    const QString providerId = currentProviderId();
    const bool custom = providerId == QLatin1String("openai-compat");
    const ProviderDirectory::NativeProvider provider = ProviderDirectory::byId(providerId);
    m_settings->set("ai.provider", providerId);
    m_settings->set("ai.coding_plan", m_codingPlan->isChecked());
    // Non-custom providers resolve their endpoint from the directory
    // (coding-plan aware); an empty stored base URL keeps that automatic.
    m_settings->set("ai.base_url", custom ? m_baseUrl->text().trimmed() : QString());
    QString model = m_model->text().trimmed();
    if (!provider.id.isEmpty())
        model = ProviderDirectory::stripSlugPrefix(model);
    m_settings->set("ai.model", model);
    m_settings->set("agent.permission_level", m_permission->currentData().toString());
    m_settings->set("agent.max_task_seconds", m_maxTaskSeconds->value());
    m_settings->set("agent.max_tool_calls", m_maxToolCalls->value());
    m_settings->set("agent.confirm_downloads", m_confirmDownloads->isChecked());
    m_settings->set("privacy.do_not_track", m_doNotTrack->isChecked());
    m_settings->set("privacy.send_page_content", m_sendPageContent->isChecked());

    const QString key = m_apiKey->text().trimmed();
    if (!key.isEmpty()) {
        if (custom || provider.id.isEmpty())
            m_settings->saveApiKey(key);
        else
            ProviderDirectory::saveProviderKey(m_settings, providerId, key);
    }
    if (themeDidChange)
        emit themeChanged(theme);
    accept();
}
