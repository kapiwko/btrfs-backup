// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>

namespace btrfsbackup::platform::linux {

// Atomic replacement of a rendered Linux directory tree.

inline constexpr const char* render_root_marker = ".btrfs-backup-render-root";

void replace_render_directory(
    const std::filesystem::path& output_dir,
    const std::function<void(const std::filesystem::path&)>& render,
    const std::function<void(const std::filesystem::path&)>& validate
);

} // namespace btrfsbackup::platform::linux
