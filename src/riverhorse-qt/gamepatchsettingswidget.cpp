// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "gamepatchsettingswidget.h"
#include "settingswindow.h"

#include "common/settings_interface.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

GamePatchSettingsWidget::GamePatchSettingsWidget(std::vector<std::string> patch_names, SettingsWindow* dialog,
                                                 QWidget* parent)
  : QWidget(parent), m_dialog(dialog)
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  QLabel* label = new QLabel(tr("Patches change the game itself, for example to make it widescreen or run at 60 FPS. "
                                "Ticked patches are applied when the game starts. Use widescreen patches with a 16:9 "
                                "aspect ratio."),
                             this);
  label->setWordWrap(true);
  layout->addWidget(label);

  m_list = new QListWidget(this);
  layout->addWidget(m_list, 1);

  const std::vector<std::string> enabled = dialog->getSettingsInterface()->GetStringList("Patches", "Enable");
  for (const std::string& name : patch_names)
  {
    QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(name), m_list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState((std::find(enabled.begin(), enabled.end(), name) != enabled.end()) ? Qt::Checked :
                                                                                              Qt::Unchecked);
  }

  connect(m_list, &QListWidget::itemChanged, this, &GamePatchSettingsWidget::onItemChanged);
}

GamePatchSettingsWidget::~GamePatchSettingsWidget() = default;

void GamePatchSettingsWidget::onItemChanged(QListWidgetItem* item)
{
  std::vector<std::string> enabled;
  for (int i = 0; i < m_list->count(); i++)
  {
    const QListWidgetItem* row = m_list->item(i);
    if (row->checkState() == Qt::Checked)
      enabled.push_back(row->text().toStdString());
  }

  SettingsInterface* sif = m_dialog->getSettingsInterface();
  if (enabled.empty())
    sif->DeleteValue("Patches", "Enable");
  else
    sif->SetStringList("Patches", "Enable", enabled);
  m_dialog->saveAndReloadGameSettings();
}
