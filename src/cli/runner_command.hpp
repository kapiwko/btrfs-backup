#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <backup/backup_service.hpp>

namespace btrfsbackup::command {

using RunnerExecutionServices = BackupServiceDependencies;

int runner(const std::filesystem::path& profile_config_dir, const std::vector<std::string>& args, std::ostream& output);
int runner(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    RunnerExecutionServices* execution_services,
    CancellationToken* external_cancellation = nullptr
);

} // namespace btrfsbackup::command
