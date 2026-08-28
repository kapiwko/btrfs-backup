// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/model/json.hpp>
#include <core/durable_file_operations.hpp>
#include <state/run_status.hpp>

namespace btrfsbackup::state {

btrfsbackup::config::Json build_status_json(const RunStatus& status);
std::string dump_status_json(const RunStatus& status);
btrfsbackup::config::Json build_public_status_json(const RunStatus& status);
std::string dump_public_status_json(const RunStatus& status);

void write_current_status(
    IDurableFileOperations& files,
    const std::filesystem::path& status_root,
    const RunStatus& status,
    std::filesystem::perms permissions = public_read_file_permissions
);

} // namespace btrfsbackup::state
