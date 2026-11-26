// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#include "netplaydialogs.h"
#include "qthost.h"

#include "core/netplay.h"

#include "common/error.h"
#include "common/string_util.h"

#include <QtConcurrent/QtConcurrent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMessageBox>
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
  updateState();
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

NetplayLobbyWindow::NetplayLobbyWindow(QWidget* parent) : QDialog(parent), m_refresh_timer(new QTimer(this))
{
  m_ui.setupUi(this);
  setAttribute(Qt::WA_DeleteOnClose);
  m_ui.sessionList->setIconSize(QSize(COVER_ICON_SIZE, COVER_ICON_SIZE));
  m_ui.nickname->setText(
    QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "Nickname", Netplay::DEFAULT_NICKNAME)));
  m_ui.listSessions->setChecked(Host::GetBaseBoolSettingValue("Netplay", "ListSessions", false));
  m_ui.confirmJoins->setChecked(Host::GetBaseBoolSettingValue("Netplay", "ConfirmJoins", false));

  connect(m_ui.nickname, &QLineEdit::editingFinished, this, &NetplayLobbyWindow::saveSettings);
  connect(m_ui.listSessions, &QCheckBox::toggled, this, &NetplayLobbyWindow::saveSettings);
  connect(m_ui.confirmJoins, &QCheckBox::toggled, this, &NetplayLobbyWindow::saveSettings);
  connect(m_ui.refreshSessions, &QPushButton::clicked, this, &NetplayLobbyWindow::refreshSessions);
  connect(m_ui.sessionList, &QTreeWidget::itemSelectionChanged, this, &NetplayLobbyWindow::updateState);
  connect(m_ui.sessionList, &QTreeWidget::itemActivated, this, &NetplayLobbyWindow::joinSelectedSession);
  connect(m_ui.join, &QPushButton::clicked, this, &NetplayLobbyWindow::joinSelectedSession);
  connect(m_ui.hostGame, &QPushButton::clicked, this, &NetplayLobbyWindow::hostGame);
  connect(m_ui.joinByCode, &QPushButton::clicked, this, &NetplayLobbyWindow::joinByCode);
  connect(m_ui.close, &QPushButton::clicked, this, &NetplayLobbyWindow::close);
  connect(&m_session_list_watcher, &QFutureWatcher<SessionListResult>::finished, this,
          &NetplayLobbyWindow::onSessionListFetched);
  connect(m_refresh_timer, &QTimer::timeout, this, &NetplayLobbyWindow::refreshSessions);

  m_refresh_timer->start(REFRESH_INTERVAL_MS);
  updateState();
  refreshSessions();
}

NetplayLobbyWindow::~NetplayLobbyWindow() = default;

void NetplayLobbyWindow::refreshSessions()
{
  if (m_session_list_watcher.isRunning())
    return;

  m_ui.refreshSessions->setEnabled(false);
  if (m_ui.sessionList->topLevelItemCount() == 0)
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

void NetplayLobbyWindow::onSessionListFetched()
{
  const SessionListResult result = m_session_list_watcher.result();
  m_ui.refreshSessions->setEnabled(true);
  if (!result.sessions.has_value())
  {
    m_ui.sessionsStatus->setText(tr("Could not get the session list: %1").arg(result.error));
    return;
  }

  const QList<QTreeWidgetItem*> selected = m_ui.sessionList->selectedItems();
  const QVariant selected_code = selected.isEmpty() ? QVariant() : selected.front()->data(0, CodeRole);

  m_ui.sessionList->clear();
  {
    const auto lock = GameList::GetLock();
    for (const Netplay::SessionListing& session : result.sessions.value())
    {
      const GameList::Entry* entry = GameList::GetEntryBySerial(session.game_serial);
      QTreeWidgetItem* const item = new QTreeWidgetItem(m_ui.sessionList);
      item->setText(0, QString::fromStdString(session.game_title));
      item->setToolTip(0, QString::fromStdString(session.game_serial));
      if (entry)
        item->setIcon(0, getCoverIcon(entry));
      item->setText(1, QString::fromStdString(session.host_nickname));
      item->setText(2, QStringLiteral("%1/%2").arg(session.players).arg(session.max_players));
      item->setText(3, session.has_password ? tr("Yes") : tr("No"));
      item->setText(4, entry ? tr("Yes") : tr("No"));
      item->setData(0, CodeRole, session.session_code);
      item->setData(0, PasswordRole, session.has_password);
      item->setData(0, GamePathRole, entry ? QString::fromStdString(entry->path) : QString());
      if (!entry)
      {
        for (int column = 0; column < m_ui.sessionList->columnCount(); column++)
          item->setForeground(column, palette().brush(QPalette::Disabled, QPalette::Text));
      }
      if (selected_code.isValid() && selected_code == item->data(0, CodeRole))
        item->setSelected(true);
    }
  }

  m_ui.sessionList->resizeColumnToContents(0);
  m_ui.sessionsStatus->setText(result.sessions->empty() ?
                                 tr("No open sessions right now. Host a game with \"List sessions I host in the "
                                    "lobby\" turned on so other players can find it.") :
                                 tr("Open sessions: %1").arg(result.sessions->size()));
  updateState();
}

QIcon NetplayLobbyWindow::getCoverIcon(const GameList::Entry* entry)
{
  const QString path = QString::fromStdString(GameList::GetCoverImagePathForEntry(entry));
  if (path.isEmpty())
    return QIcon();

  auto it = m_cover_icons.find(path);
  if (it == m_cover_icons.end())
  {
    const qreal dpr = devicePixelRatioF();
    const int size = static_cast<int>(COVER_ICON_SIZE * dpr);
    QPixmap pixmap(path);
    if (!pixmap.isNull())
    {
      pixmap = pixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      pixmap.setDevicePixelRatio(dpr);
    }
    it = m_cover_icons.insert(path, pixmap.isNull() ? QIcon() : QIcon(pixmap));
  }

  return it.value();
}

void NetplayLobbyWindow::updateState()
{
  const QList<QTreeWidgetItem*> selected = m_ui.sessionList->selectedItems();
  m_ui.join->setEnabled(!selected.isEmpty() && !selected.front()->data(0, GamePathRole).toString().isEmpty());
}

void NetplayLobbyWindow::saveSettings()
{
  const QString nickname = m_ui.nickname->text().trimmed();
  if (!nickname.isEmpty())
    Host::SetBaseStringSettingValue("Netplay", "Nickname", nickname.toUtf8().constData());
  Host::SetBaseBoolSettingValue("Netplay", "ListSessions", m_ui.listSessions->isChecked());
  Host::SetBaseBoolSettingValue("Netplay", "ConfirmJoins", m_ui.confirmJoins->isChecked());
  Host::CommitBaseSettingChanges();
}

void NetplayLobbyWindow::joinSelectedSession()
{
  const QList<QTreeWidgetItem*> selected = m_ui.sessionList->selectedItems();
  if (selected.isEmpty())
    return;

  const QTreeWidgetItem* const item = selected.front();
  const QString game_path = item->data(0, GamePathRole).toString();
  if (game_path.isEmpty())
  {
    QMessageBox::information(this, windowTitle(),
                             tr("%1 is not in your game list, so you can't join this session.").arg(item->text(0)));
    return;
  }

  QString password;
  if (item->data(0, PasswordRole).toBool())
  {
    bool ok = false;
    password = QInputDialog::getText(this, windowTitle(), tr("This session needs a password. Ask the host for it."),
                                     QLineEdit::Password, QString(), &ok);
    if (!ok)
      return;
  }

  saveSettings();
  g_emu_thread->joinNetplaySession(
    QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "Nickname", Netplay::DEFAULT_NICKNAME)),
    QString::number(item->data(0, CodeRole).toUInt()),
    Host::GetBaseIntSettingValue("Netplay", "Port", Netplay::DEFAULT_PORT), password, false,
    Host::GetBaseIntSettingValue("Netplay", "InputDelay", 0), game_path);
  close();
}

void NetplayLobbyWindow::hostGame()
{
  QStringList titles;
  QStringList paths;
  {
    const auto lock = GameList::GetLock();
    std::vector<const GameList::Entry*> entries;
    for (u32 i = 0; i < GameList::GetEntryCount(); i++)
    {
      const GameList::Entry* entry = GameList::GetEntryByIndex(i);
      if (GameList::IsEntryListed(entry, true))
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const GameList::Entry* a, const GameList::Entry* b) {
      return StringUtil::Strcasecmp(a->title.c_str(), b->title.c_str()) < 0;
    });

    for (const GameList::Entry* entry : entries)
    {
      const GameList::Entry* boot_entry = entry->IsDiscSet() ? GameList::GetFirstDiscSetMember(entry->path) : entry;
      if (!boot_entry)
        continue;

      titles.push_back(QString::fromStdString(entry->title));
      paths.push_back(QString::fromStdString(boot_entry->path));
    }
  }

  if (titles.isEmpty())
  {
    QMessageBox::information(this, windowTitle(), tr("No games found. Add a folder with your games to the game list."));
    return;
  }

  bool ok = false;
  const QString title = QInputDialog::getItem(this, tr("Host a Game"), tr("Game to host:"), titles, 0, false, &ok);
  if (!ok)
    return;

  saveSettings();
  emit hostGameRequested(paths[titles.indexOf(title)]);
  close();
}

void NetplayLobbyWindow::joinByCode()
{
  saveSettings();
  JoinNetplaySessionDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted)
    close();
}
