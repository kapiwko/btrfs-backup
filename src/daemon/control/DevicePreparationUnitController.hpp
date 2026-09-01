// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup::daemon::control {

class ISystemdUnitController;

class IDevicePreparationUnitController {
  public:
    virtual ~IDevicePreparationUnitController() = default;
    virtual void start(const std::string& operation_id, int passphrase_fd) = 0;
    virtual void recover(const std::string& operation_id) = 0;
    virtual void stop(const std::string& operation_id) = 0;
    [[nodiscard]] virtual bool active(const std::string& operation_id) = 0;
};

class SystemdDevicePreparationUnitController final : public IDevicePreparationUnitController {
  public:
    SystemdDevicePreparationUnitController(
        ISystemdUnitController& units,
        std::filesystem::path secret_root = "/run/btrfs-backup-manager/device-preparations"
    );

    void start(const std::string& operation_id, int passphrase_fd) override;
    void recover(const std::string& operation_id) override;
    void stop(const std::string& operation_id) override;
    [[nodiscard]] bool active(const std::string& operation_id) override;

    [[nodiscard]] std::filesystem::path secret_path(const std::string& operation_id) const;

  private:
    void start_unit(const std::string& operation_id);
    ISystemdUnitController& units_;
    std::filesystem::path secret_root_;
};

} // namespace btrfsbackup::daemon::control
