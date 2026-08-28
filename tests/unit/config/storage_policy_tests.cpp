// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <type_traits>

#include <config/model/storage_policy.hpp>

#include "support/test_helpers.hpp"
#include "support/validation_test_helpers.hpp"

namespace {

static_assert(!std::is_default_constructible_v<btrfsbackup::config::RetentionCount>);
static_assert(!std::is_default_constructible_v<btrfsbackup::config::ByteThreshold>);
static_assert(!std::is_convertible_v<std::uint64_t, btrfsbackup::config::RetentionCount>);
static_assert(!std::is_convertible_v<std::uint64_t, btrfsbackup::config::ByteThreshold>);

void test_retention_count_accepts_supported_range() {
    const btrfsbackup::config::RetentionCount unlimited{0};
    const btrfsbackup::config::RetentionCount maximum{btrfsbackup::config::RetentionCount::maximum};

    test_helpers::expect_eq("unlimited retention", std::to_string(unlimited.value()), "0");
    test_helpers::expect_eq("maximum retention", std::to_string(maximum.value()), "100000");
}

void test_retention_count_rejects_unsupported_range() {
    test_helpers::expect_validation_error("retention above maximum", [] { (void)btrfsbackup::config::RetentionCount{btrfsbackup::config::RetentionCount::maximum + 1}; }, "outside the supported range");
}

void test_byte_threshold_accepts_supported_range() {
    const btrfsbackup::config::ByteThreshold disabled{0};
    const btrfsbackup::config::ByteThreshold maximum{btrfsbackup::config::ByteThreshold::maximum};

    test_helpers::expect_eq("disabled byte threshold", std::to_string(disabled.value()), "0");
    test_helpers::expect_eq("maximum byte threshold", std::to_string(maximum.value()), "1000000000000000");
}

void test_byte_threshold_rejects_unsupported_range() {
    test_helpers::expect_validation_error("byte threshold above maximum", [] { (void)btrfsbackup::config::ByteThreshold{btrfsbackup::config::ByteThreshold::maximum + 1}; }, "outside the supported range");
}

} // namespace

int main() {
    test_retention_count_accepts_supported_range();
    test_retention_count_rejects_unsupported_range();
    test_byte_threshold_accepts_supported_range();
    test_byte_threshold_rejects_unsupported_range();
    return test_helpers::finish("storage policy tests");
}
