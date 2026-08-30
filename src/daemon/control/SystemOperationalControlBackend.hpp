// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <backup/ports/CancellationRequestStore.hpp>
#include <config/ports/IProfileRepository.hpp>
#include <daemon/control/OperationalControlService.hpp>
#include <daemon/control/SystemdUnitController.hpp>

namespace btrfsbackup::daemon::control {

class SystemOperationalControlBackend final : public IOperationalControlBackend {
  public:
    SystemOperationalControlBackend(
        btrfsbackup::config::IProfileRepository& profiles,
        btrfsbackup::backup::ICancellationRequestStore& cancellation_requests,
        ISystemdUnitController& units,
        std::filesystem::path operation_environment_root = "/run/btrfs-backup-manager"
    );

    [[nodiscard]] OperationalResourceVersion inspect_profile(const ProfileId& profile_id) const override;
    void start_backup(const AuthorizedOperationContext& context) override;
    [[nodiscard]] ManagerCancellationOutcome cancel_backup(
        const RunId& run_id,
        const AuthorizedOperationContext& context
    ) override;
    void validate_target(const AuthorizedOperationContext& context) override;
    void eject_target(const AuthorizedOperationContext& context) override;

  private:
    void require_profile_version(
        const AuthorizedOperationContext& context
    ) const;
    static void require_job_accepted(const TransientJobResult& result, const char* operation);
    void run_target_validation(const AuthorizedOperationContext& context);

    btrfsbackup::config::IProfileRepository& profiles_;
    btrfsbackup::backup::ICancellationRequestStore& cancellation_requests_;
    ISystemdUnitController& units_;
    std::filesystem::path operation_environment_root_;
};

} // namespace btrfsbackup::daemon::control
