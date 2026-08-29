// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <daemon/operational_control_service.hpp>
#include <daemon/systemd_unit_controller.hpp>

namespace btrfsbackup::daemon {

[[nodiscard]] TransientUnitRequest authorized_backup_unit(
    const AuthorizedOperationContext& context
);
[[nodiscard]] std::string authorized_operation_environment(
    const AuthorizedOperationContext& context
);
[[nodiscard]] std::string authorized_target_validation_unit(
    const AuthorizedOperationContext& context
);
[[nodiscard]] TransientUnitRequest authorized_target_eject_unit(
    const AuthorizedOperationContext& context
);

} // namespace btrfsbackup::daemon
