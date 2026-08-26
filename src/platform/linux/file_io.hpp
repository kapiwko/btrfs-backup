// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <sys/types.h>

#include <core/durable_file_operations.hpp>

namespace btrfsbackup {

void atomic_write(const std::filesystem::path& path, const std::string& data, mode_t mode);
void fsync_dir(const std::filesystem::path& path);

class PosixDurableFileOperations final : public IDurableFileOperations {
  public:
    void ensure_directory(
        const std::filesystem::path& path,
        std::filesystem::perms permissions
    ) override;
    void write_atomically(
        const std::filesystem::path& path,
        std::string_view data,
        std::filesystem::perms permissions
    ) override;
    void remove_durably(const std::filesystem::path& path) override;
};

} // namespace btrfsbackup
