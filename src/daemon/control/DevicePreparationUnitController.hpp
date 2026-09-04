// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace btrfsbackup::daemon::control {

class ISystemdUnitController;

struct DevicePreparationDeviceAccess {
    std::vector<std::string> major_minor;
    bool allow_mapper_control = false;
};

class IDevicePreparationUnitController {
  public:
    virtual ~IDevicePreparationUnitController() = default;
    virtual void start(
        const std::string& operation_id,
        int passphrase_fd,
        const DevicePreparationDeviceAccess& access
    ) = 0;
    virtual void recover(
        const std::string& operation_id,
        const DevicePreparationDeviceAccess& access
    ) = 0;
    virtual void update_access(
        const std::string& operation_id,
        const DevicePreparationDeviceAccess& access
    ) = 0;
    virtual void stop(const std::string& operation_id) = 0;
    [[nodiscard]] virtual bool active(const std::string& operation_id) = 0;
};

class SystemdDevicePreparationUnitController final : public IDevicePreparationUnitController {
  public:
    SystemdDevicePreparationUnitController(
        ISystemdUnitController& units,
        std::filesystem::path secret_root = "/run/btrfs-backup-manager/device-preparations"
    );

    void start(
        const std::string& operation_id,
        int passphrase_fd,
        const DevicePreparationDeviceAccess& access
    ) override;
    void recover(
        const std::string& operation_id,
        const DevicePreparationDeviceAccess& access
    ) override;
    void update_access(
        const std::string& operation_id,
        const DevicePreparationDeviceAccess& access
    ) override;
    void stop(const std::string& operation_id) override;
    [[nodiscard]] bool active(const std::string& operation_id) override;

    [[nodiscard]] std::filesystem::path secret_path(const std::string& operation_id) const;

  private:
    void start_unit(
        const std::string& operation_id,
        const DevicePreparationDeviceAccess& access
    );
    ISystemdUnitController& units_;
    std::filesystem::path secret_root_;
};

} // namespace btrfsbackup::daemon::control
