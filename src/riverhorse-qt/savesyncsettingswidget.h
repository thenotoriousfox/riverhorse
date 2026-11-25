// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtWidgets/QWidget>

#include "ui_savesyncsettingswidget.h"

class SettingsWindow;

class SaveSyncSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  SaveSyncSettingsWidget(SettingsWindow* dialog, QWidget* parent);
  ~SaveSyncSettingsWidget();

private Q_SLOTS:
  void updateEnabledOptions();
  void onSyncNowClicked();

private:
  Ui::SaveSyncSettingsWidget m_ui;
  SettingsWindow* m_dialog;
};
