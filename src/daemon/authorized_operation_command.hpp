// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <daemon/operational_control_service.hpp>

namespace btrfsbackup::daemon {

[[nodiscard]] std::vector<std::string> authorized_backup_command(
    const AuthorizedOperationContext& context
);
[[nodiscard]] std::vector<std::string> authorized_target_validation_command(
    const AuthorizedOperationContext& context
);
[[nodiscard]] std::vector<std::string> authorized_target_validation_status_command(
    const AuthorizedOperationContext& context
);
[[nodiscard]] std::string authorized_operation_environment(
    const AuthorizedOperationContext& context
);
[[nodiscard]] std::string authorized_target_validation_unit(
    const AuthorizedOperationContext& context
);
[[nodiscard]] std::vector<std::string> authorized_target_eject_command(
    const AuthorizedOperationContext& context
);

} // namespace btrfsbackup::daemon
