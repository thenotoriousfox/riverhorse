// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtWidgets/QWidget>

#include <string>
#include <vector>

class QListWidget;
class QListWidgetItem;
class SettingsWindow;

class GamePatchSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  GamePatchSettingsWidget(std::vector<std::string> patch_names, SettingsWindow* dialog, QWidget* parent);
  ~GamePatchSettingsWidget() override;

private Q_SLOTS:
  void onItemChanged(QListWidgetItem* item);

private:
  SettingsWindow* m_dialog;
  QListWidget* m_list;
};
