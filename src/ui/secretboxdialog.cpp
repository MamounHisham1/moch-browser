#include "ui/secretboxdialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Values are never displayed after being saved; the prompts below collect
// them write-only and never log them.
QString stripNewlines(QString value)
{
    value.remove(QLatin1Char('\r'));
    value.remove(QLatin1Char('\n'));
    return value;
}

} // namespace

SecretBoxDialog::SecretBoxDialog(Security::SecretBox *box, QWidget *parent)
    : QDialog(parent)
    , m_box(box)
{
    setWindowTitle(tr("Secret Box"));
    resize(460, 340);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr(
        "Secrets are referenced by name. The AI picks a name and the browser fills the "
        "value into the page itself after you approve — the value is never shown to "
        "the AI.")));

    m_list = new QListWidget(this);
    layout->addWidget(m_list);

    auto *buttons = new QHBoxLayout();
    auto *add = new QPushButton(tr("Add…"), this);
    m_replace = new QPushButton(tr("Replace…"), this);
    m_remove = new QPushButton(tr("Remove"), this);
    buttons->addWidget(add);
    buttons->addWidget(m_replace);
    buttons->addWidget(m_remove);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);

    connect(add, &QPushButton::clicked, this, &SecretBoxDialog::addSecret);
    connect(m_replace, &QPushButton::clicked, this, &SecretBoxDialog::replaceSelected);
    connect(m_remove, &QPushButton::clicked, this, &SecretBoxDialog::removeSelected);
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &SecretBoxDialog::updateButtons);

    updateButtons();
}

void SecretBoxDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refresh();
}

void SecretBoxDialog::refresh()
{
    if (!m_box)
        return;
    const QString selected = selectedName();
    m_list->clear();
    m_list->addItems(m_box->names());
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->text() == selected)
            m_list->item(i)->setSelected(true);
    }
    updateButtons();
}

void SecretBoxDialog::updateButtons()
{
    const bool hasSelection = !selectedName().isEmpty();
    m_replace->setEnabled(hasSelection);
    m_remove->setEnabled(hasSelection);
}

QString SecretBoxDialog::selectedName() const
{
    QListWidgetItem *item = m_list ? m_list->currentItem() : nullptr;
    return item ? item->text() : QString();
}

void SecretBoxDialog::addSecret()
{
    if (!m_box)
        return;
    QDialog form(this);
    form.setWindowTitle(tr("Add Secret"));
    auto *layout = new QFormLayout(&form);
    auto *nameEdit = new QLineEdit(&form);
    auto *valueEdit = new QLineEdit(&form);
    valueEdit->setEchoMode(QLineEdit::Password);
    layout->addRow(tr("Name"), nameEdit);
    layout->addRow(tr("Value"), valueEdit);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &form);
    connect(buttons, &QDialogButtonBox::accepted, &form, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &form, &QDialog::reject);
    layout->addRow(buttons);

    if (form.exec() != QDialog::Accepted)
        return;
    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Secret Box"), tr("Enter a name for the secret."));
        return;
    }
    if (m_box->has(name)) {
        QMessageBox::warning(this, tr("Secret Box"),
                             tr("A secret named '%1' already exists. Select it and use "
                                "Replace… to change its value.")
                                 .arg(name));
        return;
    }
    const QString value = stripNewlines(valueEdit->text());
    if (value.isEmpty()) {
        QMessageBox::warning(this, tr("Secret Box"), tr("Enter a value."));
        return;
    }
    m_box->put(name, value);
    refresh();
}

void SecretBoxDialog::replaceSelected()
{
    if (!m_box)
        return;
    const QString name = selectedName();
    if (name.isEmpty())
        return;
    bool ok = false;
    const QString value = stripNewlines(QInputDialog::getText(
        this, tr("Secret Box"),
        tr("Enter the new value for '%1'. The stored value is overwritten and never "
           "displayed.")
            .arg(name),
        QLineEdit::Password, QString(), &ok));
    if (!ok)
        return;
    if (value.isEmpty()) {
        QMessageBox::warning(this, tr("Secret Box"), tr("Enter a value."));
        return;
    }
    m_box->put(name, value);
    refresh();
}

void SecretBoxDialog::removeSelected()
{
    if (!m_box)
        return;
    const QString name = selectedName();
    if (name.isEmpty())
        return;
    const auto answer = QMessageBox::question(
        this, tr("Secret Box"),
        tr("Remove the secret '%1' from the Secret Box? Anything that relies on it "
           "will stop being filled until you add it again.")
            .arg(name));
    if (answer != QMessageBox::Yes)
        return;
    m_box->remove(name);
    refresh();
}
