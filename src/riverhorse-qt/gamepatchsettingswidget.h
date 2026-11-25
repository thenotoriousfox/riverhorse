// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/cheats.h"

#include <QtWidgets/QWidget>

#include <string>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class SettingsWindow;

class GamePatchSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  GamePatchSettingsWidget(std::string serial, SettingsWindow* dialog, QWidget* parent);
  ~GamePatchSettingsWidget() override;

private Q_SLOTS:
  void onItemChanged(QListWidgetItem* item);
  void onAddClicked();
  void onEditClicked();
  void onRemoveClicked();
  void updateButtons();

private:
  void fillList();
  void saveUserPatches();
  void saveEnabledPatches(std::vector<std::string> enabled);
  std::vector<std::string> getEnabledPatches() const;
  int getSelectedUserPatch() const;

  std::string m_serial;
  CheatList m_user_patches;
  SettingsWindow* m_dialog;
  QListWidget* m_list;
  QPushButton* m_edit_button;
  QPushButton* m_remove_button;
};
