// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "interfacesettingswidget.h"
#include "qthost.h"

#include "util/host.h"
#include "util/imgui_fullscreen.h"

#include "common/path.h"

#include "fmt/format.h"

#include <QtCore/QFile>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleFactory>

namespace QtHost {
static void SetStyleFromSettings();
static QPalette GetThemeFilePalette(std::string_view theme_ini);
} // namespace QtHost

static QString s_unthemed_style_name;
static QPalette s_unthemed_palette;
static bool s_unthemed_style_name_set;

const char* QtHost::GetDefaultThemeName()
{
  return "riverhorse-dark";
}

void QtHost::UpdateApplicationTheme()
{
  if (!s_unthemed_style_name_set)
  {
    s_unthemed_style_name_set = true;
    s_unthemed_style_name = QApplication::style()->objectName();
    s_unthemed_palette = QApplication::palette();
  }

  SetStyleFromSettings();
  SetIconThemeFromStyle();
}

void QtHost::SetStyleFromSettings()
{
  const std::string theme = Host::GetBaseStringSettingValue("UI", "Theme", InterfaceSettingsWidget::DEFAULT_THEME_NAME);

  if (theme == "qdarkstyle")
  {
    qApp->setStyle(s_unthemed_style_name);
    qApp->setPalette(s_unthemed_palette);
    qApp->setStyleSheet(QString());

    QFile f(QStringLiteral(":qdarkstyle/style.qss"));
    if (f.open(QFile::ReadOnly | QFile::Text))
      qApp->setStyleSheet(f.readAll());
  }
  else if (theme == "fusion")
  {
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setPalette(s_unthemed_palette);
    qApp->setStyleSheet(QString());
  }
  else if (theme == "darkfusion")
  {
    // adapted from https://gist.github.com/QuantumCD/6245215
    qApp->setStyle(QStyleFactory::create("Fusion"));

    const QColor lighterGray(75, 75, 75);
    const QColor darkGray(53, 53, 53);
    const QColor gray(128, 128, 128);
    const QColor black(25, 25, 25);
    const QColor blue(198, 238, 255);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, darkGray);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, black);
    darkPalette.setColor(QPalette::AlternateBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, darkGray);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, blue);
    darkPalette.setColor(QPalette::Highlight, lighterGray);
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::PlaceholderText, QColor(Qt::white).darker());

    darkPalette.setColor(QPalette::Active, QPalette::Button, darkGray);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkGray);

    qApp->setPalette(darkPalette);
    qApp->setStyleSheet(QString());
  }
  else if (theme == "darkfusionblue")
  {
    // adapted from https://gist.github.com/QuantumCD/6245215
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // const QColor lighterGray(75, 75, 75);
    const QColor darkGray(53, 53, 53);
    const QColor gray(128, 128, 128);
    const QColor black(25, 25, 25);
    const QColor blue(198, 238, 255);
    const QColor blue2(0, 88, 208);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, darkGray);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, black);
    darkPalette.setColor(QPalette::AlternateBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipBase, blue2);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, darkGray);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, blue);
    darkPalette.setColor(QPalette::Highlight, blue2);
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::PlaceholderText, QColor(Qt::white).darker());

    darkPalette.setColor(QPalette::Active, QPalette::Button, gray.darker());
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkGray);

    qApp->setPalette(darkPalette);
    qApp->setStyleSheet(QString());
  }
  else if (theme == "cobaltsky")
  {
    // Custom palette by KamFretoZ, A soothing deep royal blue
    // that are meant to be easy on the eyes as the main color.
    // Alternative dark theme.
    qApp->setStyle(QStyleFactory::create("Fusion"));

    const QColor gray(150, 150, 150);
    const QColor royalBlue(29, 41, 81);
    const QColor darkishBlue(17, 30, 108);
    const QColor lighterBlue(25, 32, 130);
    const QColor highlight(36, 93, 218);
    const QColor link(0, 202, 255);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, royalBlue);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, royalBlue.lighter());
    darkPalette.setColor(QPalette::AlternateBase, darkishBlue);
    darkPalette.setColor(QPalette::ToolTipBase, darkishBlue);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, lighterBlue);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, link);
    darkPalette.setColor(QPalette::Highlight, highlight);
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);

    darkPalette.setColor(QPalette::Active, QPalette::Button, lighterBlue);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, gray);

    qApp->setPalette(darkPalette);
    qApp->setStyleSheet(QString());
  }
  else if (theme == "greymatter")
  {
    qApp->setStyle(QStyleFactory::create("Fusion"));

    const QColor darkGray(46, 52, 64);
    const QColor lighterGray(59, 66, 82);
    const QColor gray(111, 111, 111);
    const QColor blue(198, 238, 255);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, darkGray);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, lighterGray);
    darkPalette.setColor(QPalette::AlternateBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, lighterGray);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, blue);
    darkPalette.setColor(QPalette::Highlight, lighterGray.darker());
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::PlaceholderText, QColor(Qt::white).darker());

    darkPalette.setColor(QPalette::Active, QPalette::Button, lighterGray);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray.lighter());
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray.lighter());
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray.lighter());
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkGray);

    qApp->setPalette(darkPalette);
    qApp->setStyleSheet(QString());
  }
  else if (theme == "darkruby")
  {
    qApp->setStyle(QStyleFactory::create("Fusion"));

    const QColor gray(128, 128, 128);
    const QColor slate(18, 18, 18);
    const QColor rubyish(172, 21, 31);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, slate);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, slate.lighter());
    darkPalette.setColor(QPalette::AlternateBase, slate.lighter());
    darkPalette.setColor(QPalette::ToolTipBase, slate);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, slate);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, Qt::white);
    darkPalette.setColor(QPalette::Highlight, rubyish);
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);

    darkPalette.setColor(QPalette::Active, QPalette::Button, slate);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, slate.lighter());

    qApp->setPalette(darkPalette);
    qApp->setStyleSheet(QString());
  }
  else if (theme == "purplerain")
  {
    qApp->setStyle(QStyleFactory::create("Fusion"));

    const QColor darkPurple(73, 41, 121);
    const QColor darkerPurple(53, 29, 87);
    const QColor gold(250, 207, 0);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, darkPurple);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, darkerPurple);
    darkPalette.setColor(QPalette::AlternateBase, darkPurple);
    darkPalette.setColor(QPalette::ToolTipBase, darkPurple);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, darkerPurple);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, gold);
    darkPalette.setColor(QPalette::Highlight, gold);
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    darkPalette.setColor(QPalette::PlaceholderText, QColor(Qt::white).darker());

    darkPalette.setColor(QPalette::Active, QPalette::Button, darkerPurple);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, darkPurple.lighter());
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, darkPurple.lighter());
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, darkPurple.lighter());
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkPurple);

    qApp->setPalette(darkPalette);
    qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #505a70; border: 1px solid white; }");
  }
  else if (theme.starts_with("riverhorse-"))
  {
    const std::optional<std::string> theme_ini =
      Host::ReadResourceFileToString(fmt::format("themes/{}.ini", theme), true);
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setPalette(theme_ini.has_value() ? GetThemeFilePalette(theme_ini.value()) : s_unthemed_palette);
    qApp->setStyleSheet(QString());
  }
#ifdef _WIN32
  else if (theme == "windowsvista")
  {
    qApp->setStyle(QStyleFactory::create("windowsvista"));
    qApp->setPalette(s_unthemed_palette);
    qApp->setStyleSheet(QString());
  }
#endif
  else
  {
    qApp->setStyle(s_unthemed_style_name);
    qApp->setPalette(s_unthemed_palette);
    qApp->setStyleSheet(QString());
  }
}

QPalette QtHost::GetThemeFilePalette(std::string_view theme_ini)
{
  const auto color = [theme_ini](std::string_view key) {
    return QColor(QString::fromUtf8(ImGuiFullscreen::GetThemeValue(theme_ini, key).value_or("").substr(0, 7)));
  };
  const QColor disabled = color("Disabled");

  QPalette palette;
  palette.setColor(QPalette::Window, color("Background"));
  palette.setColor(QPalette::WindowText, color("BackgroundText"));
  palette.setColor(QPalette::Base, color("PrimaryLight"));
  palette.setColor(QPalette::AlternateBase, color("Primary"));
  palette.setColor(QPalette::ToolTipBase, color("Primary"));
  palette.setColor(QPalette::ToolTipText, color("PrimaryText"));
  palette.setColor(QPalette::Text, color("PrimaryText"));
  palette.setColor(QPalette::Button, color("BackgroundHighlight"));
  palette.setColor(QPalette::ButtonText, color("PrimaryText"));
  palette.setColor(QPalette::Link, color("TextHighlight"));
  palette.setColor(QPalette::Highlight, color("Secondary"));
  palette.setColor(QPalette::HighlightedText, color("SecondaryText"));
  palette.setColor(QPalette::PlaceholderText, disabled);
  palette.setColor(QPalette::Light, color("PrimaryLight"));
  palette.setColor(QPalette::Mid, color("BackgroundLine"));
  palette.setColor(QPalette::Dark, color("PrimaryDark"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
  palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
  return palette;
}

bool QtHost::IsDarkApplicationTheme()
{
  QPalette palette = qApp->palette();
  return (palette.windowText().color().value() > palette.window().color().value());
}

void QtHost::SetIconThemeFromStyle()
{
  const bool dark = IsDarkApplicationTheme();
  QIcon::setThemeName(dark ? QStringLiteral("white") : QStringLiteral("black"));
}
