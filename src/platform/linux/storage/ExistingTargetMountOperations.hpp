// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace btrfsbackup::platform::linux::storage {

class IExistingTargetMountOperations {
  public:
    virtual ~IExistingTargetMountOperations() = default;
    virtual void mount_btrfs_read_only(
        const std::filesystem::path& source,
        const std::filesystem::path& target
    ) = 0;
    virtual void unmount(const std::filesystem::path& target) = 0;
};

class LibmountExistingTargetMountOperations final : public IExistingTargetMountOperations {
  public:
    void mount_btrfs_read_only(
        const std::filesystem::path& source,
        const std::filesystem::path& target
    ) override;
    void unmount(const std::filesystem::path& target) override;
};

} // namespace btrfsbackup::platform::linux::storage
