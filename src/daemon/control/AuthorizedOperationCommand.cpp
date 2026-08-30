// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/AuthorizedOperationCommand.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <config/ConfigurationIdentity.hpp>
#include <core/Errors.hpp>

namespace btrfsbackup::daemon::control {

namespace {

#ifndef BTRFSBACKUP_INSTALL_BINDIR
#error "BTRFSBACKUP_INSTALL_BINDIR must be defined by the build system"
#endif

std::string installed_program(const char* name) {
    return std::string(BTRFSBACKUP_INSTALL_BINDIR) + "/" + name;
}

std::string environment_value(const std::string& value) {
    std::string quoted{"\""};
    for (const char character : value) {
        if (character == '\n' || character == '\r' || character == '\0') {
            throw ValidationError("authorized operation environment contains a line break");
        }
        if (character == '\\' || character == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

TransientUnitRequest transient_unit(
    const AuthorizedOperationContext& context,
    std::string operation,
    bool wait
) {
    const std::string operation_id(context.operation_id.value());
    return {
        .unit = "btrfs-backup-" + operation + "@" + operation_id + ".service",
        .command = {},
        .properties = {
            "User=root",
            "Group=root",
            "UMask=0077",
            "RuntimeDirectory=btrfs-backup",
            "RuntimeDirectoryMode=0755",
            "StateDirectory=btrfs-backup",
            "StateDirectoryMode=0755",
            "NoNewPrivileges=yes",
            "PrivateTmp=yes",
            "ProtectSystem=full",
            "ProtectKernelTunables=yes",
            "ProtectKernelModules=yes",
            "ProtectControlGroups=yes",
            "ProtectHostname=yes",
            "ProtectClock=yes",
            "ProtectProc=invisible",
            "LockPersonality=yes",
            "RestrictRealtime=yes",
            "MemoryDenyWriteExecute=yes",
            "SystemCallArchitectures=native",
            "RestrictAddressFamilies=AF_UNIX AF_NETLINK",
            "Nice=10",
            "IOSchedulingClass=best-effort",
            "IOSchedulingPriority=7",
            "TimeoutStartSec=infinity",
            "TimeoutStopSec=90s",
            "KillSignal=SIGINT",
            "KillMode=mixed",
            "SendSIGKILL=yes",
        },
        .environment = {
            "PATH=/usr/bin",
            std::string(btrfsbackup::config::expected_configuration_generation_environment) + "=" + context.generation.value(),
            std::string(btrfsbackup::config::expected_configuration_fingerprint_environment) + "=" + context.fingerprint.value(),
            std::string(btrfsbackup::config::authorized_operation_id_environment) + "=" + operation_id,
        },
        .timeout = std::chrono::minutes(10),
        .wait = wait,
    };
}

} // namespace

TransientUnitRequest authorized_backup_unit(const AuthorizedOperationContext& context) {
    const std::string profile_id(context.profile_id.value());
    TransientUnitRequest unit = transient_unit(context, "run", false);
    const std::string eject_unit = "btrfs-backup-eject@" + profile_id + ".service";
    unit.properties.push_back("OnSuccess=" + eject_unit);
    unit.properties.push_back("OnFailure=" + eject_unit);
    unit.command = {
        installed_program("btrfs-backup"),
        "--profile",
        profile_id,
        "--force",
        "--no-eject",
    };
    return unit;
}

std::string authorized_operation_environment(const AuthorizedOperationContext& context) {
    return std::string("BTRFS_BACKUP_PROFILE_ID=") +
        environment_value(std::string(context.profile_id.value())) + "\n" +
        btrfsbackup::config::expected_configuration_generation_environment + "=" +
        environment_value(context.generation.value()) + "\n" +
        btrfsbackup::config::expected_configuration_fingerprint_environment + "=" +
        environment_value(context.fingerprint.value()) + "\n" +
        btrfsbackup::config::authorized_operation_id_environment + "=" +
        environment_value(std::string(context.operation_id.value())) + "\n";
}

std::string authorized_target_validation_unit(const AuthorizedOperationContext& context) {
    return "btrfs-backup-validate@" + std::string(context.operation_id.value()) + ".service";
}

TransientUnitRequest authorized_target_eject_unit(const AuthorizedOperationContext& context) {
    const std::string profile_id(context.profile_id.value());
    TransientUnitRequest unit = transient_unit(context, "eject", true);
    unit.command = {
        installed_program("btrfs-backupctl"),
        "target",
        "eject",
        "--from-service",
        "--profile",
        profile_id,
    };
    return unit;
}

} // namespace btrfsbackup::daemon::control
