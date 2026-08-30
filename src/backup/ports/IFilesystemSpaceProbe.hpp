// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/model/TargetStorageMeasurement.hpp>
#include <backup/ports/IMountInspector.hpp>

namespace btrfsbackup::backup {

class IFilesystemSpaceProbe {
  public:
    virtual ~IFilesystemSpaceProbe() = default;

    [[nodiscard]] virtual FilesystemSpace measure_verified_mount(
        const std::filesystem::path& mount_point,
        const MountEntry& expected_mount
    ) const = 0;
};

} // namespace btrfsbackup::backup
