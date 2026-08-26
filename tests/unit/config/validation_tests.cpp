// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <config/identifiers.hpp>
#include <config/profile.hpp>
#include <config/validation.hpp>

#include "support/validation_test_helpers.hpp"

namespace {

static_assert(!std::is_default_constructible_v<btrfsbackup::ProfileId>);
static_assert(!std::is_default_constructible_v<btrfsbackup::SourceId>);
static_assert(!std::is_default_constructible_v<btrfsbackup::RunId>);
static_assert(std::is_same_v<decltype(std::declval<const btrfsbackup::ProfileId&>().value()), std::string_view>);
static_assert(std::is_same_v<decltype(btrfsbackup::Profile::id), btrfsbackup::ProfileId>);
static_assert(std::is_same_v<decltype(btrfsbackup::ProfileSource::id), btrfsbackup::SourceId>);
static_assert(std::is_same_v<decltype(btrfsbackup::ProfileSettings::remote_retention), std::size_t>);
static_assert(std::is_same_v<decltype(btrfsbackup::ProfileSource::local_retention), std::size_t>);
static_assert(std::is_same_v<decltype(btrfsbackup::ProfileSettings::minimum_target_free_bytes), std::uint64_t>);
static_assert(std::is_same_v<decltype(btrfsbackup::ProfileHookCommand::timeout), std::chrono::seconds>);

void test_configuration_defaults() {
    const btrfsbackup::ProfileHookCommand hook;
    test_helpers::expect_eq("default hook timeout", std::to_string(hook.timeout.count()), "30");
}

void test_identifier_validation() {
    test_helpers::expect_eq(
        "profile identifier",
        std::string(btrfsbackup::ProfileId{"default"}.value()),
        "default"
    );
    test_helpers::expect_validation_error("empty profile identifier", [] { (void)btrfsbackup::ProfileId{""}; }, "invalid profile id");
    test_helpers::expect_validation_error("invalid source identifier", [] { (void)btrfsbackup::SourceId{"../root"}; }, "sourceId contains unsupported characters");
    test_helpers::expect_validation_error("invalid run identifier", [] { (void)btrfsbackup::RunId{"../run"}; }, "invalid run id");
}

void test_uint_validation() {
    test_helpers::expect_eq("uint zero", std::to_string(btrfsbackup::parse_uint("0", "value")), "0");
    test_helpers::expect_eq("uint value", std::to_string(btrfsbackup::parse_uint("42", "value")), "42");
    test_helpers::expect_eq("positive uint", std::to_string(btrfsbackup::parse_positive_uint("42", "value")), "42");
    test_helpers::expect_validation_error("uint empty", [] {
        (void)btrfsbackup::parse_uint("", "value");
    }, "non-negative");
    test_helpers::expect_validation_error("uint negative", [] {
        (void)btrfsbackup::parse_uint("-1", "value");
    }, "non-negative");
    test_helpers::expect_validation_error("uint maximum", [] {
        (void)btrfsbackup::parse_uint("11", "value", 10);
    }, "outside");
    test_helpers::expect_validation_error("positive zero", [] {
        (void)btrfsbackup::parse_positive_uint("0", "value");
    }, "greater than zero");
}

void test_path_validation() {
    test_helpers::expect_eq(
        "absolute normalized",
        btrfsbackup::normalized_absolute_path("/var/../mnt/backup", "path").string(),
        "/mnt/backup"
    );
    test_helpers::expect_eq(
        "relative normalized",
        btrfsbackup::normalized_relative_path("snapshots/home", "path").string(),
        "snapshots/home"
    );
    test_helpers::expect_validation_error("absolute required", [] {
        (void)btrfsbackup::normalized_absolute_path("relative", "path");
    }, "absolute");
    test_helpers::expect_validation_error("absolute newline", [] {
        (void)btrfsbackup::normalized_absolute_path("/tmp/a\nb", "path");
    }, "newline");
    test_helpers::expect_validation_error("relative absolute", [] {
        (void)btrfsbackup::normalized_relative_path("/absolute", "path");
    }, "relative");
    test_helpers::expect_validation_error("relative dotdot", [] {
        (void)btrfsbackup::normalized_relative_path("a/../b", "path");
    }, "relative");
}

void test_path_is_within() {
    test_helpers::expect_true("path same", btrfsbackup::path_is_within("/mnt/backup", "/mnt/backup"), "same path should be within");
    test_helpers::expect_true("path child", btrfsbackup::path_is_within("/mnt/backup/snapshots", "/mnt/backup"), "child path should be within");
    test_helpers::expect_true("path normalized child", btrfsbackup::path_is_within("/mnt/backup/../backup/snapshots", "/mnt/backup"), "normalized child path should be within");
    test_helpers::expect_true("path sibling", !btrfsbackup::path_is_within("/mnt/backup2", "/mnt/backup"), "sibling path should not be within");
}

} // namespace

int main() {
    test_configuration_defaults();
    test_identifier_validation();
    test_uint_validation();
    test_path_validation();
    test_path_is_within();

    return test_helpers::finish("validation tests");
}
