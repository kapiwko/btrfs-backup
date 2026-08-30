// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <sys/types.h>

#include <filesystem>

namespace btrfsbackup::platform::linux::filesystem {

void validate_trusted_directory(
    const std::filesystem::path& path,
    const std::filesystem::path& trusted_root = "/",
    uid_t trusted_owner = 0
);

void ensure_trusted_directory(
    const std::filesystem::path& path,
    unsigned int mode,
    const std::filesystem::path& trusted_root = "/",
    uid_t trusted_owner = 0
);

} // namespace btrfsbackup::platform::linux::filesystem
