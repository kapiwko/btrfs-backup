// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdlib>
#include <ctime>

#include <core/runtime_time.hpp>

#include "support/test_helpers.hpp"

namespace {

void test_utc_formats_round_trip() {
    const auto parsed = btrfsbackup::parse_utc_timestamp("2026-08-23T23:59:58Z");
    test_helpers::expect_true("parse ISO UTC", parsed.has_value(), "valid timestamp was rejected");
    if (!parsed.has_value()) {
        return;
    }
    test_helpers::expect_eq(
        "format snapshot UTC",
        btrfsbackup::format_utc_snapshot_timestamp(*parsed),
        "2026-08-23T235958Z"
    );
    test_helpers::expect_eq(
        "format ISO UTC",
        btrfsbackup::format_utc_iso_timestamp(*parsed),
        "2026-08-23T23:59:58Z"
    );
    test_helpers::expect_true(
        "parse compact UTC",
        btrfsbackup::parse_utc_timestamp("2026-08-23T235958Z") == parsed,
        "compact timestamp changed the instant"
    );
}

void test_local_date_is_typed_and_validated() {
    const auto date = btrfsbackup::parse_local_date("2024-02-29");
    test_helpers::expect_true("parse leap day", date.has_value(), "valid leap day was rejected");
    if (date.has_value()) {
        test_helpers::expect_eq("format local date", btrfsbackup::format_local_date(*date), "2024-02-29");
    }
    test_helpers::expect_true(
        "reject invalid local date",
        !btrfsbackup::parse_local_date("2025-02-29").has_value(),
        "invalid local date was accepted"
    );
    test_helpers::expect_true(
        "reject invalid UTC clock",
        !btrfsbackup::parse_utc_timestamp("2026-08-23T24:00:00Z").has_value(),
        "invalid UTC clock was accepted"
    );
}

void test_local_date_and_offset_follow_dst() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    const auto before_transition = *btrfsbackup::parse_utc_timestamp("2026-03-29T00:30:00Z");
    const auto after_transition = *btrfsbackup::parse_utc_timestamp("2026-03-29T01:30:00Z");
    test_helpers::expect_eq(
        "local time before DST",
        btrfsbackup::format_local_timestamp(before_transition),
        "2026-03-29T01:30:00+0100"
    );
    test_helpers::expect_eq(
        "local time after DST",
        btrfsbackup::format_local_timestamp(after_transition),
        "2026-03-29T03:30:00+0200"
    );

    const auto utc_previous_day = *btrfsbackup::parse_utc_timestamp("2026-03-28T23:30:00Z");
    test_helpers::expect_eq(
        "local day across UTC midnight",
        btrfsbackup::format_local_date(btrfsbackup::local_date_at(utc_previous_day)),
        "2026-03-29"
    );
}

} // namespace

int main() {
    test_utc_formats_round_trip();
    test_local_date_is_typed_and_validated();
    test_local_date_and_offset_follow_dst();
    return test_helpers::finish("runtime time tests");
}
