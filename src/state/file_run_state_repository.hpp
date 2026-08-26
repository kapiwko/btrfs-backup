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

namespace btrfsbackup {

class FileRunStateRepository final : public IRunStateRepository {
  public:
    FileRunStateRepository(ApplicationPaths paths, IDurableFileOperations& files);

    [[nodiscard]] bool last_success_matches(
        const Profile& profile,
        const std::string& date,
        const std::string& fingerprint
    ) const override;
    void write_skipped(
        const Profile& profile,
        const RunId& run_id,
        const std::string& started_at,
        const std::string& finished_at,
        std::size_t source_count
    ) override;
    void write_success(
        const Profile& profile,
        const RunId& run_id,
        const std::string& date,
        const std::string& timestamp,
        const std::string& fingerprint,
        std::size_t source_count
    ) override;
    [[nodiscard]] std::unique_ptr<IBackupRunCheckpointStore> checkpoints(const ProfileId& profile_id) override;
    [[nodiscard]] std::unique_ptr<IBackupRunEventSink> events(BackupRunStatusDescription description) override;
    void request_cancel(const ProfileId& profile_id) override;
    [[nodiscard]] bool cancel_requested(const ProfileId& profile_id) const override;
    void clear_cancel_request(const ProfileId& profile_id) override;

  private:
    [[nodiscard]] std::filesystem::path state_dir(const ProfileId& profile_id) const;
    ApplicationPaths paths_;
    IDurableFileOperations& files_;
};

class FileCancellationMonitor final : public ICancellationMonitor {
  public:
    explicit FileCancellationMonitor(IRunStateRepository& state);

    [[nodiscard]] std::unique_ptr<ICancellationWatch> watch(
        const ProfileId& profile_id,
        CancellationToken& cancellation
    ) override;

  private:
    IRunStateRepository& state_;
};

} // namespace btrfsbackup
