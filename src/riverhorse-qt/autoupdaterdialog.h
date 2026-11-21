// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include "ui_autoupdaterdialog.h"

#include <memory>
#include <string>
#include <string_view>

#include <QtCore/QDateTime>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtWidgets/QDialog>

class Error;
class HTTPDownloader;

class EmuThread;

class AutoUpdaterDialog final : public QDialog
{
  Q_OBJECT

public:
  explicit AutoUpdaterDialog(QWidget* parent = nullptr);
  ~AutoUpdaterDialog();

  static bool isSupported();
  static void cleanupAfterUpdate();

Q_SIGNALS:
  void updateCheckCompleted();

public Q_SLOTS:
  void queueUpdateCheck(bool display_message);

private Q_SLOTS:
  void httpPollTimerPoll();

  void downloadUpdateClicked();
  void skipThisUpdateClicked();
  void remindMeLaterClicked();

private:
  void reportError(const std::string_view msg);

  bool ensureHttpReady();

  bool updateNeeded() const;

  void getLatestReleaseComplete(s32 status_code, std::vector<u8> response);

  void queueGetChanges();
  void getChangesComplete(s32 status_code, std::vector<u8> response);

  bool processUpdate(const std::vector<u8>& update_data);

#ifdef _WIN32
  bool doesUpdaterNeedElevation(const std::string& application_dir) const;
  bool doUpdate(const std::string& application_dir, const std::string& zip_path, const std::string& updater_path);
  bool extractUpdater(const std::string& zip_path, const std::string& destination_path, Error* error);
#endif

  Ui::AutoUpdaterDialog m_ui;

  std::unique_ptr<HTTPDownloader> m_http;
  QTimer* m_http_poll_timer = nullptr;
  QString m_latest_version;
  QString m_download_url;
  int m_download_size = 0;

  bool m_display_messages = false;
  bool m_update_will_break_save_states = false;
};
