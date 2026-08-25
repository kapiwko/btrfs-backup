// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace btrfsbackup {

struct MountEntry {
    std::string source;
    std::string target;
    std::string fstype;
    std::string root;
    std::string options;
    std::string device_id;
    std::string filesystem_uuid;
};

class IMountInspector {
  public:
    virtual ~IMountInspector() = default;
    [[nodiscard]] virtual std::vector<MountEntry> inspect() const = 0;
};

std::optional<MountEntry> mount_at(const std::vector<MountEntry>& entries, const std::filesystem::path& target);
std::optional<MountEntry> mount_for_path(const std::vector<MountEntry>& entries, const std::filesystem::path& path);
bool paths_are_same_filesystem(
    const std::vector<MountEntry>& entries,
    const std::filesystem::path& path_a,
    const std::filesystem::path& path_b
);
bool mount_uses_mapper(
    const std::vector<MountEntry>& entries,
    const std::filesystem::path& mountpoint,
    const std::filesystem::path& mapper_path
);

} // namespace btrfsbackup
