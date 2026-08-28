// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <state/run_status.hpp>
#include <state/status_service.hpp>

namespace btrfsbackup {
class IDurableFileOperations;
}

namespace btrfsbackup::state {

void write_history_entry(
    IDurableFileOperations& files,
    const std::filesystem::path& history_root,
    const RunStatus& status
);

[[nodiscard]] std::vector<StatusDocument> get_status_history(
    const std::filesystem::path& history_root,
    const std::string& profile_id,
    std::size_t limit
);

} // namespace btrfsbackup::state
