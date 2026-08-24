#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <btrfsbackup/application_config.hpp>
#include <btrfsbackup/backup_run_executor.hpp>
#include <btrfsbackup/snapshot_inventory.hpp>

namespace btrfsbackup::command {

struct RunnerExecutionServices {
    IBackupRunActionEffects& action_effects;
    ITransferPipeline& transfer_pipeline;
    std::filesystem::path lock_root;
    ApplicationConfig application_config;
    SnapshotMetadataReader snapshot_metadata_reader = nullptr;
};

int runner(const std::filesystem::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output);
int runner(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    RunnerExecutionServices* execution_services,
    CancellationToken* external_cancellation = nullptr
);

} // namespace btrfsbackup::command
