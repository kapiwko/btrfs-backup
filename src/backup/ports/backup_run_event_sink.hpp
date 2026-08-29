// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/backup_run_event.hpp>

namespace btrfsbackup::backup {

class IBackupRunEventSink {
  public:
    virtual ~IBackupRunEventSink() = default;
    virtual void on_backup_run_event(const BackupRunEvent& event) = 0;
};

} // namespace btrfsbackup::backup
