// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>

#include <config/domain/Profile.hpp>
#include <config/domain/Validation.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace {

static_assert(std::is_same_v<decltype(btrfsbackup::config::Profile::id), btrfsbackup::ProfileId>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileSource::id), btrfsbackup::SourceId>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfilePaths::remote_root), btrfsbackup::config::RemoteSnapshotRoot>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfilePaths::incoming_root), btrfsbackup::config::IncomingRoot>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileSource::remote_subdir), btrfsbackup::config::SafeRelativePath>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileSettings::remote_retention), btrfsbackup::config::RetentionCount>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileSource::local_retention), btrfsbackup::config::RetentionCount>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileSettings::minimum_target_free_bytes), btrfsbackup::config::ByteThreshold>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileHookCommand::timeout), std::chrono::seconds>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::Profile::configuration_generation), btrfsbackup::config::ConfigurationGeneration>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileTarget::device), btrfsbackup::config::TargetDevicePath>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileTarget::mount_point), btrfsbackup::config::TargetMountPoint>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileSource::subvolume), btrfsbackup::config::SourceSubvolumePath>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileSource::local_snapshot_dir), btrfsbackup::config::LocalSnapshotRoot>);
static_assert(std::is_same_v<decltype(btrfsbackup::config::ProfileHookCommand::program), btrfsbackup::config::HookProgramPath>);
static_assert(!std::is_assignable_v<btrfsbackup::config::TargetMountPoint&, std::string>);

void test_configuration_defaults() {
    const btrfsbackup::config::ProfileHookCommand hook;
    test_helpers::expect_eq("default hook timeout", std::to_string(hook.timeout.count()), "30");
}

void test_uint_validation() {
    test_helpers::expect_eq("uint zero", std::to_string(btrfsbackup::config::parse_uint("0", "value")), "0");
    test_helpers::expect_eq("uint value", std::to_string(btrfsbackup::config::parse_uint("42", "value")), "42");
    test_helpers::expect_eq("positive uint", std::to_string(btrfsbackup::config::parse_positive_uint("42", "value")), "42");
    test_helpers::expect_validation_error("uint empty", [] { (void)btrfsbackup::config::parse_uint("", "value"); }, "non-negative");
    test_helpers::expect_validation_error("uint negative", [] { (void)btrfsbackup::config::parse_uint("-1", "value"); }, "non-negative");
    test_helpers::expect_validation_error("uint maximum", [] { (void)btrfsbackup::config::parse_uint("11", "value", 10); }, "outside");
    test_helpers::expect_validation_error("positive zero", [] { (void)btrfsbackup::config::parse_positive_uint("0", "value"); }, "greater than zero");
}

void test_path_validation() {
    test_helpers::expect_eq(
        "absolute normalized",
        btrfsbackup::config::normalized_absolute_path("/var/../mnt/backup", "path").string(),
        "/mnt/backup"
    );
    test_helpers::expect_eq(
        "relative normalized",
        btrfsbackup::config::normalized_relative_path("snapshots/home", "path").string(),
        "snapshots/home"
    );
    test_helpers::expect_validation_error("absolute required", [] { (void)btrfsbackup::config::normalized_absolute_path("relative", "path"); }, "absolute");
    test_helpers::expect_validation_error("absolute newline", [] { (void)btrfsbackup::config::normalized_absolute_path("/tmp/a\nb", "path"); }, "newline");
    test_helpers::expect_validation_error("relative absolute", [] { (void)btrfsbackup::config::normalized_relative_path("/absolute", "path"); }, "relative");
    test_helpers::expect_validation_error("relative dotdot", [] { (void)btrfsbackup::config::normalized_relative_path("a/../b", "path"); }, "relative");
}

void test_path_is_within() {
    test_helpers::expect_true("path same", btrfsbackup::config::path_is_within("/mnt/backup", "/mnt/backup"), "same path should be within");
    test_helpers::expect_true("path child", btrfsbackup::config::path_is_within("/mnt/backup/snapshots", "/mnt/backup"), "child path should be within");
    test_helpers::expect_true("path normalized child", btrfsbackup::config::path_is_within("/mnt/backup/../backup/snapshots", "/mnt/backup"), "normalized child path should be within");
    test_helpers::expect_true("path sibling", !btrfsbackup::config::path_is_within("/mnt/backup2", "/mnt/backup"), "sibling path should not be within");
}

void test_operation_paths() {
    const btrfsbackup::config::TargetDevicePath device{"/dev/disk/../mapper/backup"};
    test_helpers::expect_eq("device normalized", device.value().string(), "/dev/mapper/backup");

    const btrfsbackup::config::LocalSnapshotRoot snapshots{"/home/../.snapshots/root"};
    test_helpers::expect_eq("snapshot root normalized", snapshots.value().string(), "/.snapshots/root");

    test_helpers::expect_validation_error(
        "device outside dev",
        [] { (void)btrfsbackup::config::TargetDevicePath{"/tmp/device"}; },
        "inside /dev"
    );
    test_helpers::expect_validation_error(
        "relative mount point",
        [] { (void)btrfsbackup::config::TargetMountPoint{"mnt/backup"}; },
        "absolute"
    );
    test_helpers::expect_validation_error(
        "relative hook program",
        [] { (void)btrfsbackup::config::HookProgramPath{"hooks/prepare"}; },
        "absolute"
    );
}

} // namespace

int main() {
    test_configuration_defaults();
    test_uint_validation();
    test_path_validation();
    test_path_is_within();
    test_operation_paths();

    return test_helpers::finish("validation tests");
}
