// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <backup/ports/cancellation_monitor.hpp>
#include <backup/ports/run_state_repository.hpp>
#include <config/application_paths.hpp>
#include <core/durable_file_operations.hpp>

namespace btrfsbackup::state {

class FileRunStateRepository final : public btrfsbackup::backup::IRunStateRepository {
  public:
    FileRunStateRepository(btrfsbackup::config::ApplicationPaths paths, IDurableFileOperations& files);

    [[nodiscard]] bool last_success_matches(
        const btrfsbackup::config::Profile& profile,
        const std::string& date,
        const std::string& fingerprint
    ) const override;
    void write_skipped(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        const std::string& started_at,
        const std::string& finished_at,
        std::size_t source_count
    ) override;
    void write_success(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        const std::string& date,
        const std::string& timestamp,
        const std::string& fingerprint,
        std::size_t source_count
    ) override;
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::IBackupRunCheckpointStore> checkpoints(const ProfileId& profile_id) override;
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::IBackupRunEventSink> events(btrfsbackup::backup::BackupRunStatusDescription description) override;
    void request_cancel(const ProfileId& profile_id) override;
    [[nodiscard]] bool cancel_requested(const ProfileId& profile_id) const override;
    void clear_cancel_request(const ProfileId& profile_id) override;

  private:
    [[nodiscard]] std::filesystem::path state_dir(const ProfileId& profile_id) const;
    btrfsbackup::config::ApplicationPaths paths_;
    IDurableFileOperations& files_;
};

class FileCancellationMonitor final : public btrfsbackup::backup::ICancellationMonitor {
  public:
    explicit FileCancellationMonitor(btrfsbackup::backup::IRunStateRepository& state);

    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch(
        const ProfileId& profile_id,
        CancellationToken& cancellation
    ) override;

  private:
    btrfsbackup::backup::IRunStateRepository& state_;
};

} // namespace btrfsbackup::state
