// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#include "netplaydialogs.h"
#include "qthost.h"

#include "core/netplay.h"

#include <QtWidgets/QPushButton>

CreateNetplaySessionDialog::CreateNetplaySessionDialog(QWidget* parent) : QDialog(parent)
{
  m_ui.setupUi(this);
  m_ui.nickname->setText(
    QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "Nickname", Netplay::DEFAULT_NICKNAME)));
  m_ui.port->setValue(Host::GetBaseIntSettingValue("Netplay", "Port", Netplay::DEFAULT_PORT));
  m_ui.inputDelay->setValue(Host::GetBaseIntSettingValue("Netplay", "InputDelay", 0));
  m_ui.relayServer->setText(QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "RelayServer", "")));

  connect(m_ui.nickname, &QLineEdit::textChanged, this, &CreateNetplaySessionDialog::updateState);
  connect(m_ui.buttonBox, &QDialogButtonBox::accepted, this, &CreateNetplaySessionDialog::accept);
  connect(m_ui.buttonBox, &QDialogButtonBox::rejected, this, &CreateNetplaySessionDialog::reject);
  updateState();
}

CreateNetplaySessionDialog::~CreateNetplaySessionDialog() = default;

void CreateNetplaySessionDialog::accept()
{
  if (!validate())
    return;

  const QString nickname = m_ui.nickname->text().trimmed();
  Host::SetBaseStringSettingValue("Netplay", "Nickname", nickname.toUtf8().constData());
  Host::SetBaseIntSettingValue("Netplay", "Port", m_ui.port->value());
  Host::SetBaseIntSettingValue("Netplay", "InputDelay", m_ui.inputDelay->value());
  Host::SetBaseStringSettingValue("Netplay", "RelayServer", m_ui.relayServer->text().trimmed().toUtf8().constData());
  Host::CommitBaseSettingChanges();

  QDialog::accept();
  g_emu_thread->createNetplaySession(nickname, m_ui.port->value(), Netplay::MAX_PLAYERS, m_ui.password->text(),
                                     m_ui.inputDelay->value());
}

bool CreateNetplaySessionDialog::validate() const
{
  return !m_ui.nickname->text().trimmed().isEmpty();
}

void CreateNetplaySessionDialog::updateState()
{
  m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(validate());
}

JoinNetplaySessionDialog::JoinNetplaySessionDialog(QWidget* parent) : QDialog(parent)
{
  m_ui.setupUi(this);
  m_ui.nickname->setText(
    QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "Nickname", Netplay::DEFAULT_NICKNAME)));
  m_ui.hostname->setText(QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "LastHost", "")));
  m_ui.port->setValue(Host::GetBaseIntSettingValue("Netplay", "Port", Netplay::DEFAULT_PORT));
  m_ui.inputDelay->setValue(Host::GetBaseIntSettingValue("Netplay", "InputDelay", 0));
  m_ui.relayServer->setText(QString::fromStdString(Host::GetBaseStringSettingValue("Netplay", "RelayServer", "")));

  connect(m_ui.nickname, &QLineEdit::textChanged, this, &JoinNetplaySessionDialog::updateState);
  connect(m_ui.hostname, &QLineEdit::textChanged, this, &JoinNetplaySessionDialog::updateState);
  connect(m_ui.buttonBox, &QDialogButtonBox::accepted, this, &JoinNetplaySessionDialog::accept);
  connect(m_ui.buttonBox, &QDialogButtonBox::rejected, this, &JoinNetplaySessionDialog::reject);
  updateState();
}

JoinNetplaySessionDialog::~JoinNetplaySessionDialog() = default;

void JoinNetplaySessionDialog::accept()
{
  if (!validate())
    return;

  const QString nickname = m_ui.nickname->text().trimmed();
  const QString hostname = m_ui.hostname->text().trimmed();
  Host::SetBaseStringSettingValue("Netplay", "Nickname", nickname.toUtf8().constData());
  Host::SetBaseStringSettingValue("Netplay", "LastHost", hostname.toUtf8().constData());
  Host::SetBaseIntSettingValue("Netplay", "Port", m_ui.port->value());
  Host::SetBaseIntSettingValue("Netplay", "InputDelay", m_ui.inputDelay->value());
  Host::SetBaseStringSettingValue("Netplay", "RelayServer", m_ui.relayServer->text().trimmed().toUtf8().constData());
  Host::CommitBaseSettingChanges();

  QDialog::accept();
  g_emu_thread->joinNetplaySession(nickname, hostname, m_ui.port->value(), m_ui.password->text(),
                                   m_ui.spectating->isChecked(), m_ui.inputDelay->value());
}

bool JoinNetplaySessionDialog::validate() const
{
  return !m_ui.nickname->text().trimmed().isEmpty() && !m_ui.hostname->text().trimmed().isEmpty();
}

void JoinNetplaySessionDialog::updateState()
{
  m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(validate());
}
