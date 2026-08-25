// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <sys/types.h>

namespace btrfsbackup {

void atomic_write(const std::filesystem::path& path, const std::string& data, mode_t mode);
void fsync_dir(const std::filesystem::path& path);

} // namespace btrfsbackup
