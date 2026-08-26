// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <backup/backup_run_executor.hpp>
#include <backup/model/backup_run_plan.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <config/application_paths.hpp>
#include <config/profile_repository.hpp>
#include <core/identifiers.hpp>
#include <config/model/profile.hpp>
#include <core/error_code.hpp>

namespace btrfsbackup {

struct BackupRequest {
    ProfileId profile_id;
    bool force = false;
    bool validate_only = false;
};

struct CancelBackupResult {
    ProfileId profile_id;
    bool cancel_requested = false;
};

enum class BackupExecutionOutcome { Completed,
                                    Skipped,
                                    Cancelled,
                                    Failed,
                                    Busy,
                                    Validated };

struct BackupExecutionResult {
    BackupRunPlan plan;
    BackupExecutionOutcome outcome = BackupExecutionOutcome::Completed;
    std::size_t actions_completed = 0;
    std::optional<ErrorCode> error_code;
    std::string error_message;
};

class ITargetMounter {
  public:
    virtual ~ITargetMounter() = default;
    virtual void ensure_mounted(const Profile& profile) = 0;
};

class IBackupPlanner {
  public:
    virtual ~IBackupPlanner() = default;
    [[nodiscard]] virtual BackupRunPlan build(
        const Profile& profile,
        const std::vector<MountEntry>& mounts,
        const ApplicationPaths& paths,
        const RunId& run_id,
        const std::string& snapshot_timestamp
    ) const = 0;
};

class IBackupRunFactory {
  public:
    virtual ~IBackupRunFactory() = default;
    [[nodiscard]] virtual BackupRunExecutionResult execute(
        BackupRunPlan plan,
        IBackupRunEventSink& events,
        IBackupRunCheckpointStore& checkpoints,
        CancellationToken& cancellation
    ) = 0;
};

class IBackupRunLease {
  public:
    virtual ~IBackupRunLease() = default;
};

struct BackupRunLeaseResult {
    std::unique_ptr<IBackupRunLease> lease;
    std::optional<ErrorCode> error_code;
    std::string error_message;
};

class IBackupRunLeaseProvider {
  public:
    virtual ~IBackupRunLeaseProvider() = default;
    [[nodiscard]] virtual BackupRunLeaseResult try_acquire(const Profile& profile) = 0;
};

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

class ICancellationWatch {
  public:
    virtual ~ICancellationWatch() = default;
};

class ICancellationMonitor {
  public:
    virtual ~ICancellationMonitor() = default;
    [[nodiscard]] virtual std::unique_ptr<ICancellationWatch> watch(
        const ProfileId& profile_id,
        CancellationToken& cancellation
    ) = 0;
};

class IClock {
  public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::string snapshot_timestamp() const = 0;
    [[nodiscard]] virtual std::string local_date() const = 0;
    [[nodiscard]] virtual std::string local_timestamp() const = 0;
};

class IRunIdGenerator {
  public:
    virtual ~IRunIdGenerator() = default;
    [[nodiscard]] virtual RunId generate(const std::string& snapshot_timestamp) = 0;
};

class BackupService {
  public:
    BackupService(
        IProfileRepository& profiles,
        IMountInspector& mounts,
        ITargetMounter& target_mounter,
        IBackupPlanner& planner,
        IBackupRunFactory& run_factory,
        IBackupRunLeaseProvider& leases,
        IRunStateRepository& state,
        ICancellationMonitor& cancellation_monitor,
        IClock& clock,
        IRunIdGenerator& run_ids,
        CancellationToken& cancellation
    );

    [[nodiscard]] BackupExecutionResult start(const BackupRequest& request);
    [[nodiscard]] BackupRunPlan plan(const BackupRequest& request);
    [[nodiscard]] CancelBackupResult cancel(const ProfileId& profile_id);

  private:
    [[nodiscard]] BackupRunPlan prepare_plan(const Profile& profile, const RunId& run_id, const std::string& timestamp);

    IProfileRepository& profiles_;
    IMountInspector& mounts_;
    ITargetMounter& target_mounter_;
    IBackupPlanner& planner_;
    IBackupRunFactory& run_factory_;
    IBackupRunLeaseProvider& leases_;
    IRunStateRepository& state_;
    ICancellationMonitor& cancellation_monitor_;
    IClock& clock_;
    IRunIdGenerator& run_ids_;
    CancellationToken& cancellation_;
};

} // namespace btrfsbackup
