// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <backup/ports/cancellation_request_store.hpp>
#include <backup/ports/command_runner.hpp>
#include <config/ports/profile_repository.hpp>
#include <daemon/operational_control_service.hpp>

namespace btrfsbackup::daemon {

class SystemOperationalControlBackend final : public IOperationalControlBackend {
  public:
    SystemOperationalControlBackend(
        btrfsbackup::config::IProfileRepository& profiles,
        btrfsbackup::backup::ICancellationRequestStore& cancellation_requests,
        btrfsbackup::backup::ICommandRunner& commands
    );

    void start_backup(const ProfileId& profile_id) override;
    [[nodiscard]] ManagerCancellationOutcome cancel_backup(
        const ProfileId& profile_id,
        const RunId& run_id
    ) override;
    void validate_target(const ProfileId& profile_id) override;
    void eject_target(const ProfileId& profile_id) override;

  private:
    void require_profile(const ProfileId& profile_id) const;
    void run_effect(const std::vector<std::string>& command, const char* operation);

    btrfsbackup::config::IProfileRepository& profiles_;
    btrfsbackup::backup::ICancellationRequestStore& cancellation_requests_;
    btrfsbackup::backup::ICommandRunner& commands_;
};

} // namespace btrfsbackup::daemon
