// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/testing/NullBackupRunEventSink.hpp>

namespace btrfsbackup::backup {

void NullBackupRunEventSink::on_backup_run_event(const BackupRunEvent&) {
}

} // namespace btrfsbackup::backup
