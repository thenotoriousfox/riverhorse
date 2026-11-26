// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui_netplaycreatesessiondialog.h"
#include "ui_netplayjoinsessiondialog.h"
#include "ui_netplaylobbywindow.h"

#include "core/game_list.h"
#include "core/netplay.h"

#include <QtCore/QFutureWatcher>
#include <QtCore/QHash>
#include <QtCore/QTimer>
#include <QtWidgets/QDialog>

#include <optional>
#include <vector>

class CreateNetplaySessionDialog final : public QDialog
{
  Q_OBJECT

public:
  explicit CreateNetplaySessionDialog(QWidget* parent);
  ~CreateNetplaySessionDialog() override;

public Q_SLOTS:
  void accept() override;

private Q_SLOTS:
  void updateState();

private:
  bool validate() const;

  Ui::NetplayCreateSessionDialog m_ui;
};

class JoinNetplaySessionDialog final : public QDialog
{
  Q_OBJECT

public:
  explicit JoinNetplaySessionDialog(QWidget* parent);
  ~JoinNetplaySessionDialog() override;

public Q_SLOTS:
  void accept() override;

private Q_SLOTS:
  void updateState();

private:
  bool validate() const;

  Ui::NetplayJoinSessionDialog m_ui;
};

/// Lists the sessions players have listed on the relay server, and joins or hosts from them.
class NetplayLobbyWindow final : public QDialog
{
  Q_OBJECT

public:
  explicit NetplayLobbyWindow(QWidget* parent);
  ~NetplayLobbyWindow() override;

Q_SIGNALS:
  /// The player picked a game to boot and host.
  void hostGameRequested(const QString& path);

private Q_SLOTS:
  void refreshSessions();
  void onSessionListFetched();
  void updateState();
  void saveSettings();
  void joinSelectedSession();
  void hostGame();
  void joinByCode();

private:
  static constexpr int REFRESH_INTERVAL_MS = 10000;
  static constexpr int COVER_ICON_SIZE = 48;

  enum : int
  {
    CodeRole = Qt::UserRole,
    PasswordRole,
    GamePathRole,
  };

  struct SessionListResult
  {
    std::optional<std::vector<Netplay::SessionListing>> sessions;
    QString error;
  };

  QIcon getCoverIcon(const GameList::Entry* entry);

  Ui::NetplayLobbyWindow m_ui;
  QHash<QString, QIcon> m_cover_icons;
  QFutureWatcher<SessionListResult> m_session_list_watcher;
  QTimer* m_refresh_timer;
};
