#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <backup/backup_run_executor.hpp>
#include <backup/backup_run_plan.hpp>
#include <backup/transfer_pipeline.hpp>
#include <backup/snapshot_inventory.hpp>
#include <config/application_config.hpp>

namespace btrfsbackup {

struct BackupServiceDependencies {
    IBackupRunActionEffects& action_effects;
    ITransferPipeline& transfer_pipeline;
    std::filesystem::path lock_root;
    ApplicationConfig application_config;
    SnapshotMetadataReader snapshot_metadata_reader = nullptr;
};

struct BackupRequest {
    std::filesystem::path profile_config_dir;
    std::string profile_id = "default";
    std::filesystem::path mountinfo = "/proc/self/mountinfo";
    std::string timestamp;
    std::string run_id;
    std::map<std::string, std::string> mount_uuid_overrides;
    std::string today;
    bool force = false;
    bool validate_only = false;
};

struct CancelBackupResult {
    std::string profile_id;
    bool cancel_requested = false;
};

enum class BackupExecutionOutcome {
    Completed,
    Skipped,
    Cancelled,
    Failed,
    Busy,
    Validated,
};

struct BackupExecutionResult {
    BackupRunPlan plan;
    BackupExecutionOutcome outcome = BackupExecutionOutcome::Completed;
    std::size_t actions_completed = 0;
    std::string error_code;
    std::string error_message;
};

BackupRunPlan plan_backup(
    const BackupRequest& request,
    BackupServiceDependencies* dependencies = nullptr
);

BackupExecutionResult start_backup(
    const BackupRequest& request,
    BackupServiceDependencies* dependencies = nullptr,
    CancellationToken* external_cancellation = nullptr
);

CancelBackupResult cancel_backup(
    const std::filesystem::path& profile_config_dir,
    const std::string& profile_id,
    BackupServiceDependencies* dependencies = nullptr
);

} // namespace btrfsbackup
