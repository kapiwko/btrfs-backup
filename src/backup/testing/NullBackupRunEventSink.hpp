// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/IBackupRunEventSink.hpp>

namespace btrfsbackup::backup {

class NullBackupRunEventSink final : public IBackupRunEventSink {
  public:
    void on_backup_run_event(const BackupRunEvent& event) override;
};

} // namespace btrfsbackup::backup
