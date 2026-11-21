// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui_netplaycreatesessiondialog.h"
#include "ui_netplayjoinsessiondialog.h"

#include <QtWidgets/QDialog>

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
