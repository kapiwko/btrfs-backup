// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <backup/backup_service.hpp>
#include <core/runtime_time.hpp>

namespace btrfsbackup::cli {

enum class RunnerCommandKind {
    Plan,
    Execute,
    Cancel,
};

struct RunnerOptions {
    RunnerCommandKind command;
    btrfsbackup::backup::BackupRequest request;
    std::filesystem::path mountinfo;
    std::map<std::string, std::string> mount_uuid_overrides;
    btrfsbackup::RuntimeTimePoint timestamp;
    btrfsbackup::LocalDate today;
    btrfsbackup::RunId run_id;
};

RunnerOptions parse_runner_options(const std::vector<std::string>& args);

} // namespace btrfsbackup::cli
