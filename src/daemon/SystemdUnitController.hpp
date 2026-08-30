// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace btrfsbackup::daemon {

enum class SystemdJobFailure {
    UnitNotFound,
    JobAlreadyRunning,
    JobConflict,
    Cancelled,
    TimedOut,
    ManagerRejected,
    UnitFailed,
};

struct SystemdJobError {
    SystemdJobFailure failure;
    std::string detail;
    std::optional<int> unit_exit_status;
};

struct StartUnitRequest {
    std::string unit;
    std::chrono::milliseconds timeout;
};

struct StopUnitRequest {
    std::string unit;
    std::chrono::milliseconds timeout;
};

struct TransientUnitRequest {
    std::string unit;
    std::vector<std::string> command;
    std::vector<std::string> properties;
    std::vector<std::string> environment;
    std::chrono::milliseconds timeout;
    bool wait = false;
};

using StartJobResult = std::expected<void, SystemdJobError>;
using StopJobResult = std::expected<void, SystemdJobError>;
using TransientJobResult = std::expected<void, SystemdJobError>;

class ISystemdUnitController {
  public:
    virtual ~ISystemdUnitController() = default;
    [[nodiscard]] virtual StartJobResult start_unit(const StartUnitRequest& request) = 0;
    [[nodiscard]] virtual StopJobResult stop_unit(const StopUnitRequest& request) = 0;
    [[nodiscard]] virtual TransientJobResult start_transient_unit(
        const TransientUnitRequest& request
    ) = 0;
};

} // namespace btrfsbackup::daemon
