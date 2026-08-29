// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <string>
#include <vector>

#include <daemon/authorized_operation_command.hpp>

#include "support/test_helpers.hpp"

namespace {

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

void expect_authorized_environment(const std::string& name, const std::vector<std::string>& command) {
    test_helpers::expect_true(
        name + " generation",
        contains(command, "--setenv=BTRFS_BACKUP_CONFIGURATION_GENERATION=generation-1"),
        "authorized generation was not passed"
    );
    test_helpers::expect_true(
        name + " fingerprint",
        contains(command, "--setenv=BTRFS_BACKUP_CONFIGURATION_FINGERPRINT=fingerprint-1"),
        "authorized fingerprint was not passed"
    );
    test_helpers::expect_true(
        name + " operation id",
        contains(command, "--setenv=BTRFS_BACKUP_OPERATION_ID=operation-1"),
        "operation id was not passed"
    );
}

void test_backup_uses_versioned_transient_unit() {
    const std::vector<std::string> command = btrfsbackup::daemon::authorized_backup_command(context());

    expect_authorized_environment("backup", command);
    test_helpers::expect_true("backup async", contains(command, "--no-block"), "backup transient unit is not asynchronous");
    test_helpers::expect_true(
        "backup unit",
        contains(command, "--unit=btrfs-backup-run@operation-1.service"),
        "backup unit is not operation-specific"
    );
    test_helpers::expect_true(
        "backup cleanup",
        contains(command, "--property=ExecStopPost=/usr/bin/btrfs-backupctl target eject --from-service --profile laptop"),
        "backup cleanup did not retain the authorized environment"
    );
}

void test_synchronous_target_operations_carry_identity() {
    const std::vector<std::string> validation = btrfsbackup::daemon::authorized_target_validation_command(context());
    const std::vector<std::string> eject = btrfsbackup::daemon::authorized_target_eject_command(context());

    expect_authorized_environment("validation", validation);
    expect_authorized_environment("eject", eject);
    test_helpers::expect_true("validation waits", contains(validation, "--wait"), "validation does not wait for the process");
    test_helpers::expect_true("eject waits", contains(eject, "--wait"), "eject does not wait for the process");
    test_helpers::expect_true(
        "validation unit",
        contains(validation, "--unit=btrfs-backup-validate@operation-1.service"),
        "validation unit is not operation-specific"
    );
    test_helpers::expect_true(
        "eject unit",
        contains(eject, "--unit=btrfs-backup-eject@operation-1.service"),
        "eject unit is not operation-specific"
    );
}

} // namespace

int main() {
    test_backup_uses_versioned_transient_unit();
    test_synchronous_target_operations_carry_identity();
    return test_helpers::finish("authorized operation command tests");
}
