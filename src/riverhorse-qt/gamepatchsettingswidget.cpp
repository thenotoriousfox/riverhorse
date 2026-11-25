// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "gamepatchsettingswidget.h"
#include "cheatcodeeditordialog.h"
#include "settingswindow.h"

#include "core/system.h"

#include "common/file_system.h"
#include "common/settings_interface.h"

#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

static constexpr int USER_PATCH_ROLE = Qt::UserRole;

GamePatchSettingsWidget::GamePatchSettingsWidget(std::string serial, SettingsWindow* dialog, QWidget* parent)
  : QWidget(parent), m_serial(std::move(serial)), m_dialog(dialog)
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  QLabel* label = new QLabel(tr("Patches change the game itself, for example to make it widescreen or run at 60 FPS. "
                                "Ticked patches are applied when the game starts. Use widescreen patches with a 16:9 "
                                "aspect ratio. Patches you add are kept in the patches folder."),
                             this);
  label->setWordWrap(true);
  layout->addWidget(label);

  m_list = new QListWidget(this);
  layout->addWidget(m_list, 1);

  QHBoxLayout* buttons = new QHBoxLayout();
  QPushButton* add_button = new QPushButton(tr("Add Patch..."), this);
  m_edit_button = new QPushButton(tr("Edit..."), this);
  m_remove_button = new QPushButton(tr("Remove"), this);
  buttons->addWidget(add_button);
  buttons->addWidget(m_edit_button);
  buttons->addWidget(m_remove_button);
  buttons->addStretch(1);
  layout->addLayout(buttons);

  const std::string user_filename = System::GetUserGamePatchFileName(m_serial);
  if (FileSystem::FileExists(user_filename.c_str()))
    m_user_patches.LoadFromPCSXRFile(user_filename.c_str());

  fillList();

  connect(m_list, &QListWidget::itemChanged, this, &GamePatchSettingsWidget::onItemChanged);
  connect(m_list, &QListWidget::currentRowChanged, this, &GamePatchSettingsWidget::updateButtons);
  connect(m_list, &QListWidget::itemDoubleClicked, this, &GamePatchSettingsWidget::onEditClicked);
  connect(add_button, &QPushButton::clicked, this, &GamePatchSettingsWidget::onAddClicked);
  connect(m_edit_button, &QPushButton::clicked, this, &GamePatchSettingsWidget::onEditClicked);
  connect(m_remove_button, &QPushButton::clicked, this, &GamePatchSettingsWidget::onRemoveClicked);
}

GamePatchSettingsWidget::~GamePatchSettingsWidget() = default;

void GamePatchSettingsWidget::fillList()
{
  const QSignalBlocker blocker(m_list);
  m_list->clear();

  const std::vector<std::string> names = System::GetGamePatchNames(m_serial);
  const std::vector<std::string> enabled = m_dialog->getSettingsInterface()->GetStringList("Patches", "Enable");
  const size_t first_user_patch = names.size() - std::min<size_t>(names.size(), m_user_patches.GetCodeCount());
  for (size_t i = 0; i < names.size(); i++)
  {
    const bool user_patch = (i >= first_user_patch);
    QListWidgetItem* item = new QListWidgetItem(
      user_patch ? tr("%1 (yours)").arg(QString::fromStdString(names[i])) : QString::fromStdString(names[i]), m_list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setData(USER_PATCH_ROLE, user_patch ? static_cast<int>(i - first_user_patch) : -1);
    item->setData(Qt::ToolTipRole, QString::fromStdString(names[i]));
    item->setCheckState((std::find(enabled.begin(), enabled.end(), names[i]) != enabled.end()) ? Qt::Checked :
                                                                                                 Qt::Unchecked);
  }

  updateButtons();
}

std::vector<std::string> GamePatchSettingsWidget::getEnabledPatches() const
{
  std::vector<std::string> enabled;
  for (int i = 0; i < m_list->count(); i++)
  {
    const QListWidgetItem* row = m_list->item(i);
    if (row->checkState() == Qt::Checked)
      enabled.push_back(row->data(Qt::ToolTipRole).toString().toStdString());
  }
  return enabled;
}

void GamePatchSettingsWidget::saveEnabledPatches(std::vector<std::string> enabled)
{
  SettingsInterface* sif = m_dialog->getSettingsInterface();
  if (enabled.empty())
    sif->DeleteValue("Patches", "Enable");
  else
    sif->SetStringList("Patches", "Enable", enabled);
  m_dialog->saveAndReloadGameSettings();
}

void GamePatchSettingsWidget::onItemChanged(QListWidgetItem* item)
{
  saveEnabledPatches(getEnabledPatches());
}

int GamePatchSettingsWidget::getSelectedUserPatch() const
{
  const QListWidgetItem* item = m_list->currentItem();
  return item ? item->data(USER_PATCH_ROLE).toInt() : -1;
}

void GamePatchSettingsWidget::updateButtons()
{
  const bool user_patch = (getSelectedUserPatch() >= 0);
  m_edit_button->setEnabled(user_patch);
  m_remove_button->setEnabled(user_patch);
}

void GamePatchSettingsWidget::saveUserPatches()
{
  const std::string filename = System::GetUserGamePatchFileName(m_serial);
  if (m_user_patches.GetCodeCount() == 0)
  {
    FileSystem::DeleteFile(filename.c_str());
    return;
  }

  if (!m_user_patches.SaveToPCSXRFile(filename.c_str()))
    QMessageBox::critical(this, tr("Error"), tr("Failed to save patches to %1.").arg(QString::fromStdString(filename)));
}

void GamePatchSettingsWidget::onAddClicked()
{
  CheatCode code;
  CheatCodeEditorDialog editor(QStringList(), &code, this);
  if (editor.exec() != QDialog::Accepted)
    return;

  m_user_patches.AddCode(std::move(code));
  saveUserPatches();
  fillList();
}

void GamePatchSettingsWidget::onEditClicked()
{
  const int index = getSelectedUserPatch();
  if (index < 0)
    return;

  CheatCode code = m_user_patches.GetCode(static_cast<u32>(index));
  const std::string old_name = code.description;
  CheatCodeEditorDialog editor(QStringList(), &code, this);
  if (editor.exec() != QDialog::Accepted)
    return;

  std::vector<std::string> enabled = getEnabledPatches();
  std::replace(enabled.begin(), enabled.end(), old_name, code.description);
  m_user_patches.SetCode(static_cast<u32>(index), std::move(code));
  saveUserPatches();
  saveEnabledPatches(std::move(enabled));
  fillList();
}

void GamePatchSettingsWidget::onRemoveClicked()
{
  const int index = getSelectedUserPatch();
  if (index < 0)
    return;

  const std::string name = m_user_patches.GetCode(static_cast<u32>(index)).description;
  if (QMessageBox::question(this, tr("Remove Patch"),
                            tr("Remove the patch \"%1\"?").arg(QString::fromStdString(name))) != QMessageBox::Yes)
  {
    return;
  }

  std::vector<std::string> enabled = getEnabledPatches();
  std::erase(enabled, name);
  m_user_patches.RemoveCode(static_cast<u32>(index));
  saveUserPatches();
  saveEnabledPatches(std::move(enabled));
  fillList();
}
