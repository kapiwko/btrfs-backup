// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <cli/RunnerOptions.hpp>

#include "support/TestHelpers.hpp"

namespace {

void test_execute_options_are_typed() {
    const btrfsbackup::cli::RunnerOptions options = btrfsbackup::cli::parse_runner_options({
        "execute",
        "--profile",
        "laptop",
        "--timestamp",
        "2026-08-23T08:00:00Z",
        "--today",
        "2026-08-23",
        "--run-id",
        "manual-run",
        "--mountinfo",
        "/tmp/mountinfo",
        "--mount-uuid",
        "/dev/mapper/backup",
        "target-uuid",
        "--force",
        "--validate",
    });

    test_helpers::expect_true(
        "runner command kind",
        options.command == btrfsbackup::cli::RunnerCommandKind::Execute,
        "expected execute command"
    );
    test_helpers::expect_eq("runner profile", std::string(options.request.profile_id.value()), "laptop");
    test_helpers::expect_true("runner force", options.request.force, "force option was not parsed");
    test_helpers::expect_true("runner validate", options.request.validate_only, "validate option was not parsed");
    test_helpers::expect_eq("runner run id", std::string(options.run_id.value()), "manual-run");
    test_helpers::expect_eq("runner mountinfo", options.mountinfo.string(), "/tmp/mountinfo");
    test_helpers::expect_eq(
        "runner mount uuid",
        options.mount_uuid_overrides.at("/dev/mapper/backup"),
        "target-uuid"
    );
    test_helpers::expect_eq(
        "runner timestamp",
        btrfsbackup::format_utc_iso_timestamp(options.timestamp),
        "2026-08-23T08:00:00Z"
    );
    test_helpers::expect_eq("runner date", btrfsbackup::format_local_date(options.today), "2026-08-23");
}

void test_run_id_defaults_from_timestamp() {
    const btrfsbackup::cli::RunnerOptions options = btrfsbackup::cli::parse_runner_options({
        "plan",
        "--timestamp",
        "2026-08-23T08:00:00Z",
    });

    test_helpers::expect_true(
        "plan command kind",
        options.command == btrfsbackup::cli::RunnerCommandKind::Plan,
        "expected plan command"
    );
    test_helpers::expect_eq(
        "generated runner run id",
        std::string(options.run_id.value()),
        "20260823T080000Z-shadow"
    );
}

void test_plan_target_mode_is_offline_unless_mount_is_explicit() {
    const btrfsbackup::cli::RunnerOptions offline = btrfsbackup::cli::parse_runner_options({"plan", "--offline"});
    const btrfsbackup::cli::RunnerOptions mounted = btrfsbackup::cli::parse_runner_options({"plan", "--mount-target"});

    test_helpers::expect_true("offline plan target", !offline.mount_target, "offline plan enabled mounting");
    test_helpers::expect_true("mounted plan target", mounted.mount_target, "mounted plan did not enable mounting");
}

} // namespace

int main() {
    test_execute_options_are_typed();
    test_run_id_defaults_from_timestamp();
    test_plan_target_mode_is_offline_unless_mount_is_explicit();
    return test_helpers::finish("runner options tests");
}
