// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#include "netplaydialogs.h"
#include "qthost.h"

#include "core/netplay.h"

#include "common/error.h"

#include <QtConcurrent/QtConcurrent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QPushButton>

CreateNetplaySessionDialog::CreateNetplaySessionDialog(QWidget* parent) : QDialog(parent)
{
  m_ui.setupUi(this);
  m_ui.nickname->setText(
    QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "Nickname", Netplay::DEFAULT_NICKNAME)));
  m_ui.port->setValue(Host::GetBaseIntSettingValue("Netplay", "Port", Netplay::DEFAULT_PORT));
  m_ui.inputDelay->setValue(Host::GetBaseIntSettingValue("Netplay", "InputDelay", 0));
  m_ui.useRelay->setChecked(Host::GetBaseBoolSettingValue("Netplay", "UseRelay", true));
  m_ui.relayServer->setText(QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "RelayServer", "")));
  m_ui.listSession->setChecked(Host::GetBaseBoolSettingValue("Netplay", "ListSessions", false));
  m_ui.confirmJoins->setChecked(Host::GetBaseBoolSettingValue("Netplay", "ConfirmJoins", false));

  connect(m_ui.nickname, &QLineEdit::textChanged, this, &CreateNetplaySessionDialog::updateState);
  connect(m_ui.useRelay, &QCheckBox::toggled, this, &CreateNetplaySessionDialog::updateState);
  connect(m_ui.buttonBox, &QDialogButtonBox::accepted, this, &CreateNetplaySessionDialog::accept);
  connect(m_ui.buttonBox, &QDialogButtonBox::rejected, this, &CreateNetplaySessionDialog::reject);
  updateState();
}

CreateNetplaySessionDialog::~CreateNetplaySessionDialog() = default;

void CreateNetplaySessionDialog::accept()
{
  if (!validate())
    return;

  const QString nickname = m_ui.nickname->text().trimmed();
  Host::SetBaseStringSettingValue("Netplay", "Nickname", nickname.toUtf8().constData());
  Host::SetBaseIntSettingValue("Netplay", "Port", m_ui.port->value());
  Host::SetBaseIntSettingValue("Netplay", "InputDelay", m_ui.inputDelay->value());
  Host::SetBaseBoolSettingValue("Netplay", "UseRelay", m_ui.useRelay->isChecked());
  Host::SetBaseStringSettingValue("Netplay", "RelayServer", m_ui.relayServer->text().trimmed().toUtf8().constData());
  Host::SetBaseBoolSettingValue("Netplay", "ListSessions", m_ui.listSession->isChecked());
  Host::SetBaseBoolSettingValue("Netplay", "ConfirmJoins", m_ui.confirmJoins->isChecked());
  Host::CommitBaseSettingChanges();

  QDialog::accept();
  g_emu_thread->createNetplaySession(nickname, m_ui.port->value(), Netplay::MAX_PLAYERS, m_ui.password->text(),
                                     m_ui.inputDelay->value());
}

bool CreateNetplaySessionDialog::validate() const
{
  return !m_ui.nickname->text().trimmed().isEmpty();
}

void CreateNetplaySessionDialog::updateState()
{
  m_ui.relayServer->setEnabled(m_ui.useRelay->isChecked());
  m_ui.listSession->setEnabled(m_ui.useRelay->isChecked());
  m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(validate());
}

JoinNetplaySessionDialog::JoinNetplaySessionDialog(QWidget* parent) : QDialog(parent)
{
  m_ui.setupUi(this);
  m_ui.nickname->setText(
    QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "Nickname", Netplay::DEFAULT_NICKNAME)));
  m_ui.hostname->setText(QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "LastHost", "")));
  m_ui.port->setValue(Host::GetBaseIntSettingValue("Netplay", "Port", Netplay::DEFAULT_PORT));
  m_ui.inputDelay->setValue(Host::GetBaseIntSettingValue("Netplay", "InputDelay", 0));
  m_ui.useRelay->setChecked(Host::GetBaseBoolSettingValue("Netplay", "UseRelay", true));
  m_ui.relayServer->setText(QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "RelayServer", "")));

  connect(m_ui.nickname, &QLineEdit::textChanged, this, &JoinNetplaySessionDialog::updateState);
  connect(m_ui.hostname, &QLineEdit::textChanged, this, &JoinNetplaySessionDialog::updateState);
  connect(m_ui.useRelay, &QCheckBox::toggled, this, &JoinNetplaySessionDialog::updateState);
  connect(m_ui.buttonBox, &QDialogButtonBox::accepted, this, &JoinNetplaySessionDialog::accept);
  connect(m_ui.buttonBox, &QDialogButtonBox::rejected, this, &JoinNetplaySessionDialog::reject);
  connect(m_ui.refreshSessions, &QPushButton::clicked, this, &JoinNetplaySessionDialog::refreshSessions);
  connect(m_ui.sessionList, &QTreeWidget::itemSelectionChanged, this, &JoinNetplaySessionDialog::onSessionSelected);
  connect(m_ui.sessionList, &QTreeWidget::itemActivated, this, &JoinNetplaySessionDialog::accept);
  connect(&m_session_list_watcher, &QFutureWatcher<SessionListResult>::finished, this,
          &JoinNetplaySessionDialog::onSessionListFetched);
  updateState();
  refreshSessions();
}

JoinNetplaySessionDialog::~JoinNetplaySessionDialog() = default;

void JoinNetplaySessionDialog::accept()
{
  if (!validate())
    return;

  const QString nickname = m_ui.nickname->text().trimmed();
  const QString hostname = m_ui.hostname->text().trimmed();
  Host::SetBaseStringSettingValue("Netplay", "Nickname", nickname.toUtf8().constData());
  Host::SetBaseStringSettingValue("Netplay", "LastHost", hostname.toUtf8().constData());
  Host::SetBaseIntSettingValue("Netplay", "Port", m_ui.port->value());
  Host::SetBaseIntSettingValue("Netplay", "InputDelay", m_ui.inputDelay->value());
  Host::SetBaseBoolSettingValue("Netplay", "UseRelay", m_ui.useRelay->isChecked());
  Host::SetBaseStringSettingValue("Netplay", "RelayServer", m_ui.relayServer->text().trimmed().toUtf8().constData());
  Host::CommitBaseSettingChanges();

  QDialog::accept();
  g_emu_thread->joinNetplaySession(nickname, hostname, m_ui.port->value(), m_ui.password->text(),
                                   m_ui.spectating->isChecked(), m_ui.inputDelay->value());
}

bool JoinNetplaySessionDialog::validate() const
{
  return !m_ui.nickname->text().trimmed().isEmpty() && !m_ui.hostname->text().trimmed().isEmpty();
}

void JoinNetplaySessionDialog::updateState()
{
  m_ui.relayServer->setEnabled(m_ui.useRelay->isChecked());
  m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(validate());
}

void JoinNetplaySessionDialog::refreshSessions()
{
  if (m_session_list_watcher.isRunning())
    return;

  m_ui.refreshSessions->setEnabled(false);
  m_ui.sessionsStatus->setText(tr("Looking for open sessions..."));
  m_session_list_watcher.setFuture(QtConcurrent::run([]() {
    SessionListResult result;
    Error error;
    result.sessions = Netplay::GetSessionList(&error);
    if (!result.sessions.has_value())
      result.error = QString::fromStdString(error.GetDescription());
    return result;
  }));
}

void JoinNetplaySessionDialog::onSessionListFetched()
{
  const SessionListResult result = m_session_list_watcher.result();
  m_ui.refreshSessions->setEnabled(true);
  m_ui.sessionList->clear();
  if (!result.sessions.has_value())
  {
    m_ui.sessionsStatus->setText(tr("Could not get the session list: %1").arg(result.error));
    return;
  }

  for (const Netplay::SessionListing& session : result.sessions.value())
  {
    QTreeWidgetItem* const item = new QTreeWidgetItem(m_ui.sessionList);
    item->setText(0, QString::fromStdString(session.game_title));
    item->setToolTip(0, QString::fromStdString(session.game_serial));
    item->setText(1, session.has_password ? tr("%1 (password)").arg(QString::fromStdString(session.host_nickname)) :
                                            QString::fromStdString(session.host_nickname));
    item->setText(2, QStringLiteral("%1/%2").arg(session.players).arg(session.max_players));
    item->setText(3, QString::number(session.session_code));
  }
  m_ui.sessionList->resizeColumnToContents(0);
  m_ui.sessionsStatus->setText(result.sessions->empty() ? tr("No open sessions for this version.") :
                                                         tr("Open sessions: %1").arg(result.sessions->size()));
}

void JoinNetplaySessionDialog::onSessionSelected()
{
  const QList<QTreeWidgetItem*> selected = m_ui.sessionList->selectedItems();
  if (!selected.isEmpty())
    m_ui.hostname->setText(selected.front()->text(3));
}
