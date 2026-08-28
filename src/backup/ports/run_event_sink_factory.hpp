// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <memory>
#include <string>

#include <backup/model/backup_run_event.hpp>

namespace btrfsbackup::backup {

struct BackupRunStatusDescription {
    std::string profile_name;
    int source_count = 0;
    std::string started_at;
    std::map<std::string, std::string> source_names;
    std::string target_name;
};

class IRunEventSinkFactory {
  public:
    virtual ~IRunEventSinkFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IBackupRunEventSink> events(
        BackupRunStatusDescription description
    ) = 0;
};

} // namespace btrfsbackup::backup
