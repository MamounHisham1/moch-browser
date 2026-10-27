#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "agent/routines.hpp"

// Manage scheduled routines: list, add, remove, enable/disable. Each row
// shows the goal, the humanized interval, an enabled checkbox and the last
// fire attempt with its state. Runs go through the sidebar's scheduler and
// the one shared agent, so a firing routine simply appears in the normal
// timeline and in the session history.
//
// Implemented inline in this header and without Q_OBJECT (same reasoning as
// agent/routines.hpp) so the moch target links before ui/routinedialog.cpp
// is wired into CMake; the only connects used are Qt widgets' own signals.
class RoutineDialog : public QDialog
{
public:
    explicit RoutineDialog(RoutinesStore *store, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_store(store)
    {
        setWindowTitle(tr("Routines"));
        resize(480, 360);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(tr(
            "Routines re-run a goal on a schedule while Moch is open. Each run "
            "uses the agent and lands in the session history like a normal "
            "task; a run is skipped while another task is active.")));

        m_list = new QListWidget(this);
        layout->addWidget(m_list);

        auto *addRow = new QHBoxLayout();
        m_goal = new QLineEdit(this);
        m_goal->setPlaceholderText(
            tr("Goal to re-run, e.g. summarize new PR review comments"));
        m_interval = new QComboBox(this);
        m_interval->addItem(tr("every 15m"), 15 * 60);
        m_interval->addItem(tr("every 30m"), 30 * 60);
        m_interval->addItem(tr("every 1h"), 60 * 60);
        m_interval->addItem(tr("every 6h"), 6 * 60 * 60);
        m_interval->addItem(tr("every 1d"), 24 * 60 * 60);
        m_interval->addItem(tr("every 1w"), 7 * 24 * 60 * 60);
        auto *add = new QPushButton(tr("Add"), this);
        addRow->addWidget(m_goal, 1);
        addRow->addWidget(m_interval);
        addRow->addWidget(add);
        layout->addLayout(addRow);

        auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(close);

        connect(add, &QPushButton::clicked, this, [this] { addRoutine(); });
        connect(m_goal, &QLineEdit::returnPressed, this,
                [this] { addRoutine(); });
        // A routine may fire or be rescheduled while the dialog is open. The
        // refresh is deferred so a row widget emitting the change (e.g. its
        // checkbox) is never deleted mid-emission.
        if (m_store) {
            m_store->setChangedCallback([this] {
                QTimer::singleShot(0, this, [this] { refresh(); });
            });
        }
        refresh();
    }

    ~RoutineDialog()
    {
        if (m_store)
            m_store->setChangedCallback({});
    }

private:
    // Rebuilds the list from the store; also runs whenever the store changes.
    void refresh()
    {
        if (!m_store || !m_list)
            return;
        m_list->clear();
        const QList<RoutinesStore::Routine> routines = m_store->all();
        if (routines.isEmpty()) {
            auto *hint = new QListWidgetItem(
                tr("No routines yet — add one below."), m_list);
            hint->setFlags(Qt::NoItemFlags);
            return;
        }
        for (const RoutinesStore::Routine &routine : routines) {
            auto *item = new QListWidgetItem(m_list);
            item->setData(Qt::UserRole, routine.id);

            auto *row = new QWidget();
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(8, 4, 8, 4);

            auto *enabled = new QCheckBox(row);
            enabled->setChecked(routine.enabled);
            enabled->setToolTip(tr("Run on schedule"));
            const int id = routine.id;
            connect(enabled, &QCheckBox::toggled, this,
                    [this, id](bool on) { m_store->setEnabled(id, on); });
            rowLayout->addWidget(enabled);

            QString info =
                QStringLiteral("<b>%1</b><br><small style=\"color:#8a8d94;\">"
                               "every %2")
                    .arg(routine.goal.toHtmlEscaped(),
                         humanizedInterval(routine.intervalSeconds));
            if (routine.lastRunAt > 0)
                info += QStringLiteral(" · %1 (%2)")
                            .arg(relativeTime(routine.lastRunAt),
                                 routine.lastState.toHtmlEscaped());
            else
                info += QStringLiteral(" · never run");
            info += QStringLiteral("</small>");
            auto *label = new QLabel(info, row);
            label->setTextFormat(Qt::RichText);
            rowLayout->addWidget(label, 1);

            auto *remove = new QToolButton(row);
            remove->setText(QStringLiteral("✕"));
            remove->setToolTip(tr("Remove routine"));
            remove->setAutoRaise(true);
            connect(remove, &QToolButton::clicked, this,
                    [this, id] { m_store->remove(id); });
            rowLayout->addWidget(remove);

            m_list->setItemWidget(item, row);
            item->setSizeHint(row->sizeHint());
        }
    }

    void addRoutine()
    {
        if (!m_store)
            return;
        const QString goal = m_goal->text().trimmed();
        if (goal.isEmpty()) {
            QMessageBox::warning(this, tr("Routines"),
                                 tr("Enter a goal for the routine."));
            return;
        }
        const qint64 seconds = m_interval->currentData().toLongLong();
        if (m_store->add(goal, seconds) >= 0)
            m_goal->clear(); // change callback triggers the refresh
    }

    // "30m" / "1h" / "1d" / "1w" style token for a repeat interval.
    static QString humanizedInterval(qint64 seconds)
    {
        if (seconds <= 0)
            return QStringLiteral("0s");
        if (seconds % 604800 == 0)
            return QStringLiteral("%1w").arg(seconds / 604800);
        if (seconds % 86400 == 0)
            return QStringLiteral("%1d").arg(seconds / 86400);
        if (seconds % 3600 == 0)
            return QStringLiteral("%1h").arg(seconds / 3600);
        if (seconds % 60 == 0)
            return QStringLiteral("%1m").arg(seconds / 60);
        return QStringLiteral("%1s").arg(seconds);
    }

    // Coarse relative time for an epoch in seconds; mirrors the sidebar's
    // history labels.
    static QString relativeTime(qint64 secsSinceEpoch)
    {
        qint64 delta = QDateTime::currentSecsSinceEpoch() - secsSinceEpoch;
        if (delta < 0)
            delta = 0;
        if (delta < 60)
            return tr("just now");
        if (delta < 3600)
            return tr("%1m ago").arg(delta / 60);
        if (delta < 86400)
            return tr("%1h ago").arg(delta / 3600);
        if (delta < 7 * 86400)
            return tr("%1d ago").arg(delta / 86400);
        return QDateTime::fromSecsSinceEpoch(secsSinceEpoch)
            .date()
            .toString(QStringLiteral("yyyy-MM-dd"));
    }

    RoutinesStore *m_store = nullptr;
    QListWidget *m_list = nullptr;
    QLineEdit *m_goal = nullptr;
    QComboBox *m_interval = nullptr;
};
