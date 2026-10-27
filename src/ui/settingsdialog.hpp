#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class Settings;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(Settings *settings, QWidget *parent = nullptr);

signals:
    void themeChanged(const QString &theme);

private:
    void load();
    void save();
    QString currentProviderId() const;
    // Adjusts base-URL availability, key placeholder and saved-key state
    // for the selected provider. Existing key values are never shown —
    // only a "key saved" indicator.
    void refreshAiPane();

    Settings *m_settings = nullptr;
    QComboBox *m_theme = nullptr;
    QComboBox *m_searchEngine = nullptr;
    QComboBox *m_startup = nullptr;
    QComboBox *m_provider = nullptr;
    QCheckBox *m_codingPlan = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QLineEdit *m_model = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLabel *m_keyState = nullptr;
    QComboBox *m_permission = nullptr;
    QSpinBox *m_maxTaskSeconds = nullptr;
    QSpinBox *m_maxToolCalls = nullptr;
    QCheckBox *m_confirmDownloads = nullptr;
    QCheckBox *m_doNotTrack = nullptr;
    QCheckBox *m_sendPageContent = nullptr;
};
