// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

#include <config/ConfigurationIdentity.hpp>
#include <core/Identifiers.hpp>
#include <daemon/ManagerResponseModels.hpp>

namespace btrfsbackup::daemon::control {

enum class ManagerAuthorizationAction {
    StartBackup,
    CancelBackup,
    ValidateTarget,
    EjectTarget,
};

enum class ManagerCancellationOutcome {
    Accepted,
    StaleRun,
    RunMismatch,
};

struct OperationalResourceVersion {
    btrfsbackup::config::ConfigurationGeneration generation;
    btrfsbackup::config::ConfigurationFingerprint fingerprint;

    bool operator==(const OperationalResourceVersion&) const = default;
};

struct AuthorizedOperationContext {
    ProfileId profile_id;
    btrfsbackup::config::ConfigurationGeneration generation;
    btrfsbackup::config::ConfigurationFingerprint fingerprint;
    OperationId operation_id;
};

using OperationIdGenerator = std::function<OperationId()>;

[[nodiscard]] const char* manager_authorization_action_id(ManagerAuthorizationAction action) noexcept;

class IManagerAuthorizer {
  public:
    virtual ~IManagerAuthorizer() = default;
    [[nodiscard]] virtual bool authorize(
        const std::string& caller_bus_name,
        ManagerAuthorizationAction action
    ) = 0;
    [[nodiscard]] virtual bool caller_is_active(const std::string& caller_bus_name) = 0;
};

class IOperationalControlBackend {
  public:
    virtual ~IOperationalControlBackend() = default;
    [[nodiscard]] virtual OperationalResourceVersion inspect_profile(const ProfileId& profile_id) const = 0;
    virtual void start_backup(const AuthorizedOperationContext& context) = 0;
    [[nodiscard]] virtual ManagerCancellationOutcome cancel_backup(
        const RunId& run_id,
        const AuthorizedOperationContext& context
    ) = 0;
    virtual void validate_target(const AuthorizedOperationContext& context) = 0;
    virtual void eject_target(const AuthorizedOperationContext& context) = 0;
};

class OperationalControlService {
  public:
    OperationalControlService(
        IManagerAuthorizer& authorizer,
        IOperationalControlBackend& backend,
        OperationIdGenerator operation_ids = {}
    );

    [[nodiscard]] OperationResult start_backup(
        const std::string& caller_bus_name,
        const std::string& profile_id
    );
    [[nodiscard]] OperationResult cancel_backup(
        const std::string& caller_bus_name,
        const std::string& profile_id,
        const std::string& run_id
    );
    [[nodiscard]] OperationResult validate_target(
        const std::string& caller_bus_name,
        const std::string& profile_id
    );
    [[nodiscard]] OperationResult eject_target(
        const std::string& caller_bus_name,
        const std::string& profile_id
    );

  private:
    void require_authorized(const std::string& caller_bus_name, ManagerAuthorizationAction action);
    [[nodiscard]] AuthorizedOperationContext authorized_context(
        const ProfileId& profile_id,
        const OperationalResourceVersion& version
    );

    IManagerAuthorizer& authorizer_;
    IOperationalControlBackend& backend_;
    OperationIdGenerator operation_ids_;
};

} // namespace btrfsbackup::daemon::control
