#pragma once

#include <QDialog>

#include "security/secretbox.hpp"

class QListWidget;
class QPushButton;

// Manage stored secrets: list, add, replace, remove. The UI shows names
// only — values are write-only from the user's perspective once saved,
// and are never displayed back (they can be replaced, not revealed).
class SecretBoxDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SecretBoxDialog(Security::SecretBox *box, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void refresh();
    void addSecret();
    void replaceSelected();
    void removeSelected();
    void updateButtons();

private:
    QString selectedName() const;

    Security::SecretBox *m_box = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_replace = nullptr;
    QPushButton *m_remove = nullptr;
};
