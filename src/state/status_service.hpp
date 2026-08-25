// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <config/json.hpp>

namespace btrfsbackup {

struct StatusDocument {
    Json data;
    std::string content;
    std::filesystem::path source;
};

std::vector<StatusDocument> get_statuses(
    const std::filesystem::path& status_root,
    const std::filesystem::path& history_root,
    const std::string& profile_id,
    bool all
);
std::vector<StatusDocument> get_status_history(
    const std::filesystem::path& history_root,
    const std::string& profile_id,
    std::size_t limit
);
std::optional<StatusDocument> poll_status(
    const std::filesystem::path& status_root,
    const std::string& profile_id,
    const std::string& previous
);

} // namespace btrfsbackup
