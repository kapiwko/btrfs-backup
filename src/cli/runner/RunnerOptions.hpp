// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <backup/BackupService.hpp>
#include <core/RuntimeTime.hpp>

namespace btrfsbackup::cli::runner {

enum class RunnerCommandKind {
    Plan,
    Execute,
    Cancel,
};

class RunnerOptionsError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct RunnerOptions {
    RunnerCommandKind command;
    btrfsbackup::backup::BackupRequest request;
    bool mount_target = false;
    std::filesystem::path mountinfo;
    std::map<std::string, std::string> mount_uuid_overrides;
    btrfsbackup::RuntimeTimePoint timestamp;
    btrfsbackup::LocalDate today;
    btrfsbackup::RunId run_id;
};

RunnerOptions parse_runner_options(const std::vector<std::string>& args);

} // namespace btrfsbackup::cli::runner
