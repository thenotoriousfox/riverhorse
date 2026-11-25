// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "savesyncsettingswidget.h"
#include "qthost.h"
#include "settingswindow.h"
#include "settingwidgetbinder.h"

#include "core/save_sync.h"

#include "common/error.h"

#include <QtCore/QPointer>

SaveSyncSettingsWidget::SaveSyncSettingsWidget(SettingsWindow* dialog, QWidget* parent)
  : QWidget(parent), m_dialog(dialog)
{
  SettingsInterface* sif = dialog->getSettingsInterface();

  m_ui.setupUi(this);

  for (u32 i = 0; i < static_cast<u32>(SaveSync::Mode::Count); i++)
    m_ui.mode->addItem(QString::fromUtf8(SaveSync::GetModeDisplayName(static_cast<SaveSync::Mode>(i))));

  SettingWidgetBinder::BindWidgetToEnumSetting(sif, m_ui.mode, "SaveSync", "Mode", &SaveSync::ParseModeName,
                                               &SaveSync::GetModeName, SaveSync::Mode::Disabled);
  SettingWidgetBinder::BindWidgetToFolderSetting(sif, m_ui.folder, m_ui.folderBrowse, tr("Select Sync Folder"), nullptr,
                                                 nullptr, "SaveSync", "Folder", std::string(), false);
  SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.url, "SaveSync", "URL");
  SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.username, "SaveSync", "Username");
  SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.password, "SaveSync", "Password");
  SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.deviceName, "SaveSync", "DeviceName");
  SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.memoryCards, "SaveSync", "MemoryCards", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.saveStates, "SaveSync", "SaveStates", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.gameSettings, "SaveSync", "GameSettings", true);

  m_ui.deviceName->setPlaceholderText(tr("This computer's name"));

  connect(m_ui.mode, &QComboBox::currentIndexChanged, this, &SaveSyncSettingsWidget::updateEnabledOptions);
  connect(m_ui.syncNow, &QPushButton::clicked, this, &SaveSyncSettingsWidget::onSyncNowClicked);
  updateEnabledOptions();

  dialog->registerWidgetHelp(m_ui.mode, tr("Sync With"), tr("Disabled"),
                             tr("Folder: a folder that another program, such as Syncthing, Dropbox or Nextcloud, "
                                "keeps the same on all your devices. WebDAV Server: a server such as Nextcloud, a NAS "
                                "or rclone, which Riverhorse talks to directly."));
  dialog->registerWidgetHelp(m_ui.url, tr("Server Address"), tr("Empty"),
                             tr("The WebDAV address of a folder for Riverhorse's saves. Riverhorse creates its own "
                                "folders inside it."));
  dialog->registerWidgetHelp(m_ui.deviceName, tr("Device Name"), tr("This computer's name"),
                             tr("Used in the names of conflict copies, so you can tell which device a save came "
                                "from."));
  dialog->registerWidgetHelp(m_ui.gameSettings, tr("Game Settings"), tr("Checked"),
                             tr("Syncs the settings you change for individual games, such as controller types and "
                                "patches."));
}

SaveSyncSettingsWidget::~SaveSyncSettingsWidget() = default;

void SaveSyncSettingsWidget::updateEnabledOptions()
{
  const SaveSync::Mode mode = static_cast<SaveSync::Mode>(m_ui.mode->currentIndex());
  const bool folder = (mode == SaveSync::Mode::Folder);
  const bool webdav = (mode == SaveSync::Mode::WebDAV);
  m_ui.folderLabel->setVisible(folder);
  m_ui.folder->setVisible(folder);
  m_ui.folderBrowse->setVisible(folder);
  for (QWidget* widget : {static_cast<QWidget*>(m_ui.urlLabel), static_cast<QWidget*>(m_ui.url),
                          static_cast<QWidget*>(m_ui.usernameLabel), static_cast<QWidget*>(m_ui.username),
                          static_cast<QWidget*>(m_ui.passwordLabel), static_cast<QWidget*>(m_ui.password)})
  {
    widget->setVisible(webdav);
  }

  m_ui.contentsGroup->setEnabled(mode != SaveSync::Mode::Disabled);
  m_ui.syncNow->setEnabled(mode != SaveSync::Mode::Disabled);
  m_ui.deviceName->setEnabled(mode != SaveSync::Mode::Disabled);
}

void SaveSyncSettingsWidget::onSyncNowClicked()
{
  if (QtHost::IsSystemValid())
  {
    m_ui.status->setText(tr("Close the game first, so its saves aren't changed while it's using them."));
    return;
  }

  m_ui.syncNow->setEnabled(false);
  m_ui.status->setText(tr("Syncing..."));

  Host::RunOnCPUThread([widget = QPointer<SaveSyncSettingsWidget>(this)]() {
    SaveSync::Result result;
    Error error;
    const bool success = SaveSync::SyncNow(&result, &error);
    QtHost::RunOnUIThread([widget, success, result, message = error.GetDescription()]() {
      if (!widget)
        return;

      widget->m_ui.syncNow->setEnabled(true);
      if (!success)
      {
        widget->m_ui.status->setText(tr("Sync failed: %1").arg(QString::fromStdString(message)));
        return;
      }

      QString text =
        tr("Synced: %1 sent, %2 received, %3 removed.").arg(result.uploaded).arg(result.downloaded).arg(result.deleted);
      if (result.conflicts > 0)
        text += QStringLiteral(" ") + tr("%n save(s) changed on two devices; both versions are kept.", "",
                                         static_cast<int>(result.conflicts));
      widget->m_ui.status->setText(text);
    });
  });
}
