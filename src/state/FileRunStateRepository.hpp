// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <backup/ports/CancellationRequestStore.hpp>
#include <backup/ports/CancellationMonitor.hpp>
#include <backup/ports/ICheckpointStoreFactory.hpp>
#include <backup/ports/IRunEventSinkFactory.hpp>
#include <backup/ports/IRunLedger.hpp>
#include <config/ApplicationPaths.hpp>
#include <state/PersistentDocumentOperations.hpp>

namespace btrfsbackup::state {

class FileRunStateRepository final : public btrfsbackup::backup::IRunLedger,
                                     public btrfsbackup::backup::IRunEventSinkFactory,
                                     public btrfsbackup::backup::ICheckpointStoreFactory,
                                     public btrfsbackup::backup::ICancellationRequestStore {
  public:
    FileRunStateRepository(
        btrfsbackup::config::ApplicationPaths paths,
        IPersistentDocumentOperations& files
    );

    [[nodiscard]] bool last_success_matches(
        const btrfsbackup::config::Profile& profile,
        LocalDate date,
        const std::string& fingerprint
    ) const override;
    void write_skipped(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        RuntimeTimePoint started_at,
        RuntimeTimePoint finished_at,
        std::size_t source_count
    ) override;
    void write_success(
        const btrfsbackup::config::Profile& profile,
        const RunId& run_id,
        LocalDate date,
        RuntimeTimePoint timestamp,
        const std::string& fingerprint,
        std::size_t source_count
    ) override;
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::IBackupRunCheckpointStore> checkpoints(const ProfileId& profile_id) override;
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::IBackupRunEventSink> events(btrfsbackup::backup::BackupRunStatusDescription description) override;
    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::IActiveRunRegistration> register_active_run(
        const btrfsbackup::backup::CancellationRequest& request
    ) override;
    [[nodiscard]] btrfsbackup::backup::CancellationRequestOutcome request_cancel(
        const btrfsbackup::backup::CancellationRequest& request
    ) override;
    [[nodiscard]] bool cancel_requested(
        const btrfsbackup::backup::CancellationRequest& request
    ) const override;
    void clear_cancel_request(const btrfsbackup::backup::CancellationRequest& request) override;

  private:
    [[nodiscard]] std::filesystem::path state_dir(const ProfileId& profile_id) const;
    btrfsbackup::config::ApplicationPaths paths_;
    IPersistentDocumentOperations& files_;
};

class FileCancellationMonitor final : public btrfsbackup::backup::ICancellationMonitor {
  public:
    explicit FileCancellationMonitor(btrfsbackup::backup::ICancellationRequestStore& requests);

    [[nodiscard]] std::unique_ptr<btrfsbackup::backup::ICancellationWatch> watch(
        const btrfsbackup::backup::CancellationRequest& request,
        CancellationToken& cancellation
    ) override;

  private:
    btrfsbackup::backup::ICancellationRequestStore& requests_;
};

} // namespace btrfsbackup::state
