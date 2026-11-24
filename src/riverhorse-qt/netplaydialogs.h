// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui_netplaycreatesessiondialog.h"
#include "ui_netplayjoinsessiondialog.h"

#include "core/netplay.h"

#include <QtCore/QFutureWatcher>
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
  void refreshSessions();
  void onSessionListFetched();
  void onSessionSelected();

private:
  struct SessionListResult
  {
    std::optional<std::vector<Netplay::SessionListing>> sessions;
    QString error;
  };

  bool validate() const;

  Ui::NetplayJoinSessionDialog m_ui;
  QFutureWatcher<SessionListResult> m_session_list_watcher;
};
