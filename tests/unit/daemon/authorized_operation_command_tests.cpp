// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <string>
#include <vector>

#include <daemon/authorized_operation_command.hpp>

#include "support/test_helpers.hpp"

namespace {

#ifndef BTRFSBACKUP_TEST_INSTALL_BINDIR
#error "BTRFSBACKUP_TEST_INSTALL_BINDIR must be defined by the build system"
#endif

std::string installed_program(const char* name) {
    return std::string(BTRFSBACKUP_TEST_INSTALL_BINDIR) + "/" + name;
}

btrfsbackup::daemon::AuthorizedOperationContext context() {
    return {
        .profile_id = btrfsbackup::ProfileId{"laptop"},
        .generation = btrfsbackup::config::ConfigurationGeneration{"generation-1"},
        .fingerprint = btrfsbackup::config::ConfigurationFingerprint{"fingerprint-1"},
        .operation_id = btrfsbackup::OperationId{"operation-1"},
    };
}

bool contains(const std::vector<std::string>& command, const std::string& argument) {
    return std::find(command.begin(), command.end(), argument) != command.end();
}

void expect_authorized_environment(
    const std::string& name,
    const btrfsbackup::daemon::TransientUnitRequest& unit
) {
    test_helpers::expect_true(
        name + " generation",
        contains(unit.environment, "BTRFS_BACKUP_CONFIGURATION_GENERATION=generation-1"),
        "authorized generation was not passed"
    );
    test_helpers::expect_true(
        name + " fingerprint",
        contains(unit.environment, "BTRFS_BACKUP_CONFIGURATION_FINGERPRINT=fingerprint-1"),
        "authorized fingerprint was not passed"
    );
    test_helpers::expect_true(
        name + " operation id",
        contains(unit.environment, "BTRFS_BACKUP_OPERATION_ID=operation-1"),
        "operation id was not passed"
    );
}

void test_backup_uses_versioned_transient_unit() {
    const auto unit = btrfsbackup::daemon::authorized_backup_unit(context());

    expect_authorized_environment("backup", unit);
    test_helpers::expect_true("backup async", !unit.wait, "backup transient unit is not asynchronous");
    test_helpers::expect_true(
        "backup unit",
        unit.unit == "btrfs-backup-run@operation-1.service",
        "backup unit is not operation-specific"
    );
    test_helpers::expect_true(
        "backup cleanup",
        contains(
            unit.properties,
            "ExecStopPost=" + installed_program("btrfs-backupctl") +
                " target eject --from-service --profile laptop"
        ),
        "backup cleanup did not retain the authorized environment"
    );
    test_helpers::expect_true(
        "backup executable",
        unit.command.at(0) == installed_program("btrfs-backup"),
        "backup transient unit ignored the configured install bindir"
    );
    test_helpers::expect_true(
        "manual backup force",
        contains(unit.command, "--force"),
        "manual backup remains subject to the daily limit"
    );
}

void test_synchronous_target_operations_carry_identity() {
    const std::string validation = btrfsbackup::daemon::authorized_target_validation_unit(context());
    const auto eject = btrfsbackup::daemon::authorized_target_eject_unit(context());

    expect_authorized_environment("eject", eject);
    test_helpers::expect_true("eject waits", eject.wait, "eject does not wait for the process");
    test_helpers::expect_true(
        "validation unit",
        validation == "btrfs-backup-validate@operation-1.service",
        "validation did not use the dedicated operation-specific unit"
    );
    test_helpers::expect_true(
        "eject unit",
        eject.unit == "btrfs-backup-eject@operation-1.service",
        "eject unit is not operation-specific"
    );
    test_helpers::expect_true(
        "eject executable",
        eject.command.at(0) == installed_program("btrfs-backupctl"),
        "eject transient unit ignored the configured install bindir"
    );
    test_helpers::expect_true(
        "eject preserves busy check",
        !contains(eject.command, "--force"),
        "manager eject bypasses the target lease and identity checks"
    );
}

void test_validation_environment_carries_authorized_context() {
    const std::string environment = btrfsbackup::daemon::authorized_operation_environment(context());
    test_helpers::expect_contains("validation profile environment", environment, "BTRFS_BACKUP_PROFILE_ID=\"laptop\"\n");
    test_helpers::expect_contains(
        "validation generation environment",
        environment,
        "BTRFS_BACKUP_CONFIGURATION_GENERATION=\"generation-1\"\n"
    );
    test_helpers::expect_contains(
        "validation fingerprint environment",
        environment,
        "BTRFS_BACKUP_CONFIGURATION_FINGERPRINT=\"fingerprint-1\"\n"
    );
    test_helpers::expect_contains("validation operation environment", environment, "BTRFS_BACKUP_OPERATION_ID=\"operation-1\"\n");
}

} // namespace

int main() {
    test_backup_uses_versioned_transient_unit();
    test_synchronous_target_operations_carry_identity();
    test_validation_environment_carries_authorized_context();
    return test_helpers::finish("authorized operation command tests");
}
