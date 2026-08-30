// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/IFilesystemSpaceProbe.hpp>

namespace btrfsbackup::platform::linux::storage {

class FilesystemSpaceProbe final : public btrfsbackup::backup::IFilesystemSpaceProbe {
  public:
    [[nodiscard]] btrfsbackup::backup::FilesystemSpace measure_verified_mount(
        const std::filesystem::path& mount_point,
        const btrfsbackup::backup::MountEntry& expected_mount
    ) const override;
};

} // namespace btrfsbackup::platform::linux::storage
