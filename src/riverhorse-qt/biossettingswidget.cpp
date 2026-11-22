// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include "biossettingswidget.h"
#include "qthost.h"
#include "qtutils.h"
#include "settingswindow.h"
#include "settingwidgetbinder.h"

#include "core/bios.h"
#include "core/settings.h"

#include <QtCore/QDir>
#include <QtWidgets/QFileDialog>
#include <algorithm>

BIOSSettingsWidget::BIOSSettingsWidget(SettingsWindow* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
  SettingsInterface* sif = dialog->getSettingsInterface();

  m_ui.setupUi(this);

  SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.enableTTYLogging, "BIOS", "TTYLogging", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.fastBoot, "BIOS", "PatchFastBoot", false);

  dialog->registerWidgetHelp(m_ui.fastBoot, tr("Fast Boot"), tr("Unchecked"),
                             tr("Patches the BIOS to skip the console's boot animation. Does not work with all games, "
                                "but usually safe to enable."));
  dialog->registerWidgetHelp(m_ui.enableTTYLogging, tr("Enable TTY Logging"), tr("Unchecked"),
                             tr("Logs BIOS calls to printf(). Not all games contain debugging messages."));

  for (u32 i = 0; i < static_cast<u32>(ParallelPortDevice::Count); i++)
  {
    m_ui.parallelPortDevice->addItem(
      QString::fromUtf8(Settings::GetParallelPortDeviceDisplayName(static_cast<ParallelPortDevice>(i))));
  }
  SettingWidgetBinder::BindWidgetToEnumSetting(sif, m_ui.parallelPortDevice, "PIO", "DeviceType",
                                               &Settings::ParseParallelPortDeviceName,
                                               &Settings::GetParallelPortDeviceName,
                                               Settings::DEFAULT_PARALLEL_PORT_DEVICE);
  SettingWidgetBinder::BindWidgetToStringSetting(sif, m_ui.parallelPortImagePath, "PIO", "FlashImagePath");
  SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.parallelPortSwitchOn, "PIO", "SwitchActive", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.parallelPortImageWrites, "PIO", "FlashImageWriteEnable",
                                               false);
  connect(m_ui.parallelPortDevice, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &BIOSSettingsWidget::onParallelPortDeviceChanged);
  connect(m_ui.parallelPortImagePathBrowse, &QPushButton::clicked, this,
          &BIOSSettingsWidget::onParallelPortImagePathBrowseClicked);
  onParallelPortDeviceChanged();

  dialog->registerWidgetHelp(m_ui.parallelPortDevice, tr("Device"), tr("None"),
                             tr("Plugs a device into the console's parallel port. The Xplorer/Xploder cheat cartridge "
                                "needs a dump of its flash memory."));
  dialog->registerWidgetHelp(m_ui.parallelPortImagePath, tr("Image Path"), tr("Empty"),
                             tr("The cartridge flash dump to load."));
  dialog->registerWidgetHelp(m_ui.parallelPortSwitchOn, tr("Cartridge Switch On"), tr("Checked"),
                             tr("Position of the switch on the cartridge. Most cartridge firmware only starts when the "
                                "switch is on."));
  dialog->registerWidgetHelp(m_ui.parallelPortImageWrites, tr("Allow Image Writes"), tr("Unchecked"),
                             tr("Saves changes the cartridge makes to its flash memory back to the image file. "
                                "<strong>This overwrites your dump</strong>, so keep a backup."));

  connect(m_ui.imageNTSCJ, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    if (m_dialog->isPerGameSettings() && index == 0)
    {
      m_dialog->removeSettingValue("BIOS", "PathNTSCJ");
    }
    else
    {
      m_dialog->setStringSettingValue("BIOS", "PathNTSCJ",
                                      m_ui.imageNTSCJ->itemData(index).toString().toStdString().c_str());
    }
  });
  connect(m_ui.imageNTSCU, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    if (m_dialog->isPerGameSettings() && index == 0)
    {
      m_dialog->removeSettingValue("BIOS", "PathNTSCU");
    }
    else
    {
      m_dialog->setStringSettingValue("BIOS", "PathNTSCU",
                                      m_ui.imageNTSCU->itemData(index).toString().toStdString().c_str());
    }
  });
  connect(m_ui.imagePAL, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
    if (m_dialog->isPerGameSettings() && index == 0)
    {
      m_dialog->removeSettingValue("BIOS", "PathPAL");
    }
    else
    {
      m_dialog->setStringSettingValue("BIOS", "PathPAL",
                                      m_ui.imagePAL->itemData(index).toString().toStdString().c_str());
    }
  });

  connect(m_ui.refresh, &QPushButton::clicked, this, &BIOSSettingsWidget::refreshList);

  if (!m_dialog->isPerGameSettings())
  {
    SettingWidgetBinder::BindWidgetToFolderSetting(
      sif, m_ui.searchDirectory, m_ui.browseSearchDirectory, tr("Select BIOS Directory"), m_ui.openSearchDirectory,
      nullptr, "BIOS", "SearchDirectory", Path::Combine(EmuFolders::DataRoot, "bios"));
    connect(m_ui.searchDirectory, &QLineEdit::textChanged, this, &BIOSSettingsWidget::refreshList);
  }
  else
  {
    m_ui.mainLayout->removeWidget(m_ui.directoryGroupBox);
    delete m_ui.directoryGroupBox;
    m_ui.directoryGroupBox = nullptr;
    m_ui.directoryGroupBoxLabel = nullptr;
    m_ui.directoryGroupBoxLayout = nullptr;
    m_ui.directoryGroupBoxHorizontalLayout = nullptr;
    m_ui.searchDirectory = nullptr;
    m_ui.browseSearchDirectory = nullptr;
  }

  refreshList();
}

BIOSSettingsWidget::~BIOSSettingsWidget() = default;

void BIOSSettingsWidget::refreshList()
{
  auto images = BIOS::FindBIOSImagesInDirectory(EmuFolders::Bios.c_str());
  populateDropDownForRegion(ConsoleRegion::NTSC_J, m_ui.imageNTSCJ, images, m_dialog->isPerGameSettings());
  populateDropDownForRegion(ConsoleRegion::NTSC_U, m_ui.imageNTSCU, images, m_dialog->isPerGameSettings());
  populateDropDownForRegion(ConsoleRegion::PAL, m_ui.imagePAL, images, m_dialog->isPerGameSettings());

  setDropDownValue(m_ui.imageNTSCJ, m_dialog->getStringValue("BIOS", "PathNTSCJ", std::nullopt),
                   m_dialog->isPerGameSettings());
  setDropDownValue(m_ui.imageNTSCU, m_dialog->getStringValue("BIOS", "PathNTSCU", std::nullopt),
                   m_dialog->isPerGameSettings());
  setDropDownValue(m_ui.imagePAL, m_dialog->getStringValue("BIOS", "PathPAL", std::nullopt),
                   m_dialog->isPerGameSettings());
}

void BIOSSettingsWidget::populateDropDownForRegion(ConsoleRegion region, QComboBox* cb,
                                                   std::vector<std::pair<std::string, const BIOS::ImageInfo*>>& images,
                                                   bool per_game)
{
  QSignalBlocker sb(cb);
  cb->clear();

  if (per_game)
    cb->addItem(QIcon(QStringLiteral(":/icons/system-search.png")), tr("Use Global Setting"));

  cb->addItem(QIcon(QStringLiteral(":/icons/system-search.png")), tr("Auto-Detect"));

  std::sort(images.begin(), images.end(), [region](const auto& left, const auto& right) {
    const bool left_region_match = (left.second && left.second->region == region);
    const bool right_region_match = (right.second && right.second->region == region);
    if (left_region_match && !right_region_match)
      return true;
    else if (right_region_match && !left_region_match)
      return false;

    return left.first < right.first;
  });

  for (const auto& [name, info] : images)
  {
    QString name_str(QString::fromStdString(name));
    cb->addItem(QtUtils::GetIconForRegion(info ? info->region : ConsoleRegion::Count),
                QStringLiteral("%1 (%2)")
                  .arg(info ? QString(info->description) : qApp->translate("BIOSSettingsWidget", "Unknown"))
                  .arg(name_str),
                QVariant(name_str));
  }
}

void BIOSSettingsWidget::setDropDownValue(QComboBox* cb, const std::optional<std::string>& name, bool per_game)
{
  QSignalBlocker sb(cb);

  if (!name.has_value() || name->empty())
  {
    cb->setCurrentIndex((per_game && name.has_value()) ? 1 : 0);
    return;
  }

  QString qname(QString::fromStdString(name.value()));
  for (int i = 1; i < cb->count(); i++)
  {
    if (cb->itemData(i) == qname)
    {
      cb->setCurrentIndex(i);
      return;
    }
  }

  cb->addItem(qname, QVariant(qname));
  cb->setCurrentIndex(cb->count() - 1);
}

void BIOSSettingsWidget::onParallelPortDeviceChanged()
{
  const ParallelPortDevice device =
    Settings::ParseParallelPortDeviceName(
      m_dialog
        ->getEffectiveStringValue("PIO", "DeviceType",
                                  Settings::GetParallelPortDeviceName(Settings::DEFAULT_PARALLEL_PORT_DEVICE))
        .c_str())
      .value_or(Settings::DEFAULT_PARALLEL_PORT_DEVICE);
  const bool is_cartridge = (device == ParallelPortDevice::XplorerCartridge);
  m_ui.parallelPortImagePathLabel->setEnabled(is_cartridge);
  m_ui.parallelPortImagePath->setEnabled(is_cartridge);
  m_ui.parallelPortImagePathBrowse->setEnabled(is_cartridge);
  m_ui.parallelPortSwitchOn->setEnabled(is_cartridge);
  m_ui.parallelPortImageWrites->setEnabled(is_cartridge);
}

void BIOSSettingsWidget::onParallelPortImagePathBrowseClicked()
{
  const QString path = QDir::toNativeSeparators(QFileDialog::getOpenFileName(
    QtUtils::GetRootWidget(this), tr("Select Cartridge Image"), m_ui.parallelPortImagePath->text()));
  if (path.isEmpty())
    return;

  m_ui.parallelPortImagePath->setText(path);
}
