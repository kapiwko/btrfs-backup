// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>

#include <backup/ports/IFilesystemSpaceProbe.hpp>
#include <backup/ports/IMountInspector.hpp>
#include <backup/ports/ITargetStorageMeasurementStore.hpp>
#include <daemon/ManagerPaths.hpp>
#include <daemon/ManagerResponseModels.hpp>

namespace btrfsbackup::daemon::query {

class DeviceStateQueryService {
  public:
    explicit DeviceStateQueryService(ManagerPaths paths);
    DeviceStateQueryService(
        ManagerPaths paths,
        btrfsbackup::backup::IMountInspector& mounts,
        btrfsbackup::backup::IFilesystemSpaceProbe& space_probe,
        btrfsbackup::backup::ITargetStorageMeasurementReader& storage_reader
    );
    ~DeviceStateQueryService() noexcept;

    DeviceStateQueryService(const DeviceStateQueryService&) = delete;
    DeviceStateQueryService& operator=(const DeviceStateQueryService&) = delete;

    [[nodiscard]] TargetStatus get_device_state(
        const std::string& profile_id
    ) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace btrfsbackup::daemon::query
