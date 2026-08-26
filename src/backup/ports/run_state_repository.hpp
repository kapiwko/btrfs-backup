// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include <backup/model/backup_run_event.hpp>
#include <config/model/profile.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup {

struct BackupRunStatusDescription {
    std::string profile_name;
    int source_count = 0;
    std::string started_at;
    std::map<std::string, std::string> source_names;
    std::string target_name;
};

class IRunStateRepository {
  public:
    virtual ~IRunStateRepository() = default;

    [[nodiscard]] virtual bool last_success_matches(
        const Profile& profile,
        const std::string& date,
        const std::string& fingerprint
    ) const = 0;
    virtual void write_skipped(
        const Profile& profile,
        const RunId& run_id,
        const std::string& started_at,
        const std::string& finished_at,
        std::size_t source_count
    ) = 0;
    virtual void write_success(
        const Profile& profile,
        const RunId& run_id,
        const std::string& date,
        const std::string& timestamp,
        const std::string& fingerprint,
        std::size_t source_count
    ) = 0;
    [[nodiscard]] virtual std::unique_ptr<IBackupRunCheckpointStore> checkpoints(const ProfileId& profile_id) = 0;
    [[nodiscard]] virtual std::unique_ptr<IBackupRunEventSink> events(BackupRunStatusDescription description) = 0;
    virtual void request_cancel(const ProfileId& profile_id) = 0;
    [[nodiscard]] virtual bool cancel_requested(const ProfileId& profile_id) const = 0;
    virtual void clear_cancel_request(const ProfileId& profile_id) = 0;
};

} // namespace btrfsbackup
