// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <backup/ports/mount_inspector.hpp>

namespace btrfsbackup {

using FilesystemUuidResolver = std::function<std::string(const std::string&)>;

std::string blkid_filesystem_uuid(const std::string& source);
std::vector<MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path = "/proc/self/mountinfo");
std::vector<MountEntry> read_mount_table(const std::filesystem::path& mountinfo_path, const FilesystemUuidResolver& filesystem_uuid_resolver);
std::vector<std::string> btrfs_mount_targets(const std::filesystem::path& mountinfo_path = "/proc/self/mountinfo");
class LinuxMountInspector final : public IMountInspector {
  public:
    explicit LinuxMountInspector(
        std::filesystem::path mountinfo = "/proc/self/mountinfo",
        FilesystemUuidResolver uuid_resolver = blkid_filesystem_uuid
    );

    [[nodiscard]] std::vector<MountEntry> inspect() const override;

  private:
    std::filesystem::path mountinfo_;
    FilesystemUuidResolver uuid_resolver_;
};

} // namespace btrfsbackup
