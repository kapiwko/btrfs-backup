// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/model/json.hpp>

namespace btrfsbackup::daemon {

[[nodiscard]] btrfsbackup::config::Json read_manager_json_document(
    const std::filesystem::path& path
);
[[nodiscard]] bool manager_regular_file_if_present(const std::filesystem::path& path);
[[nodiscard]] bool manager_regular_file_without_symlink(
    const std::filesystem::directory_entry& entry
);

} // namespace btrfsbackup::daemon
