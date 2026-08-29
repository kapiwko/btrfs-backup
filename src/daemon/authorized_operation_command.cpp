// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/authorized_operation_command.hpp>

#include <string>
#include <utility>
#include <vector>

#include <config/configuration_identity.hpp>
#include <core/errors.hpp>

namespace btrfsbackup::daemon {

namespace {

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

std::vector<std::string> transient_command(
    const AuthorizedOperationContext& context,
    std::string operation,
    bool wait
) {
    const std::string operation_id(context.operation_id.value());
    return {
        "systemd-run",
        "--quiet",
        wait ? "--wait" : "--no-block",
        "--collect",
        "--unit=btrfs-backup-" + std::move(operation) + "@" + operation_id + ".service",
        "--property=User=root",
        "--property=Group=root",
        "--property=UMask=0077",
        "--property=RuntimeDirectory=btrfs-backup",
        "--property=RuntimeDirectoryMode=0755",
        "--property=StateDirectory=btrfs-backup",
        "--property=StateDirectoryMode=0755",
        "--property=NoNewPrivileges=yes",
        "--property=PrivateTmp=yes",
        "--property=ProtectSystem=full",
        "--property=ProtectKernelTunables=yes",
        "--property=ProtectKernelModules=yes",
        "--property=ProtectControlGroups=yes",
        "--property=ProtectHostname=yes",
        "--property=ProtectClock=yes",
        "--property=ProtectProc=invisible",
        "--property=LockPersonality=yes",
        "--property=RestrictRealtime=yes",
        "--property=MemoryDenyWriteExecute=yes",
        "--property=SystemCallArchitectures=native",
        "--property=RestrictAddressFamilies=AF_UNIX AF_NETLINK",
        "--property=Nice=10",
        "--property=IOSchedulingClass=best-effort",
        "--property=IOSchedulingPriority=7",
        "--property=TimeoutStartSec=infinity",
        "--property=TimeoutStopSec=90s",
        "--property=KillSignal=SIGINT",
        "--property=KillMode=mixed",
        "--property=SendSIGKILL=yes",
        "--setenv=PATH=/usr/bin",
        std::string("--setenv=") + btrfsbackup::config::expected_configuration_generation_environment + "=" + context.generation.value(),
        std::string("--setenv=") + btrfsbackup::config::expected_configuration_fingerprint_environment + "=" + context.fingerprint.value(),
        std::string("--setenv=") + btrfsbackup::config::authorized_operation_id_environment + "=" + operation_id,
    };
}

} // namespace

std::vector<std::string> authorized_backup_command(const AuthorizedOperationContext& context) {
    const std::string profile_id(context.profile_id.value());
    std::vector<std::string> command = transient_command(context, "run", false);
    command.push_back(
        "--property=ExecStopPost=/usr/bin/btrfs-backupctl target eject --from-service --profile " + profile_id
    );
    command.insert(command.end(), {"/usr/bin/btrfs-backup", "--profile", profile_id, "--no-eject"});
    return command;
}

std::vector<std::string> authorized_target_validation_command(
    const AuthorizedOperationContext& context
) {
    return {"systemctl", "start", authorized_target_validation_unit(context)};
}

std::vector<std::string> authorized_target_validation_status_command(
    const AuthorizedOperationContext& context
) {
    return {
        "systemctl",
        "show",
        "--property=ExecMainStatus",
        "--value",
        authorized_target_validation_unit(context),
    };
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

std::vector<std::string> authorized_target_eject_command(const AuthorizedOperationContext& context) {
    const std::string profile_id(context.profile_id.value());
    std::vector<std::string> command = transient_command(context, "eject", true);
    command.insert(
        command.end(),
        {"/usr/bin/btrfs-backupctl", "target", "eject", "--from-service", "--profile", profile_id}
    );
    return command;
}

} // namespace btrfsbackup::daemon
