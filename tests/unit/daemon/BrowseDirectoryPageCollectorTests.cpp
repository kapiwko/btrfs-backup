// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseDirectoryPageCollector.hpp>

#include <iomanip>
#include <sstream>
#include <string>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::daemon::control::BrowseDirectoryPageCollector;
using btrfsbackup::daemon::control::BrowseEntryInfo;

std::string entry_name(int value) {
    std::ostringstream output;
    output << "entry-" << std::setw(5) << std::setfill('0') << value;
    return output.str();
}

void test_retains_only_the_smallest_page_and_lookahead() {
    BrowseDirectoryPageCollector collector(3);
    for (int value = 9999; value >= 0; --value) {
        collector.add(BrowseEntryInfo{.name = entry_name(value)});
        test_helpers::expect_true(
            "bounded collector",
            collector.retained_entries() <= 4,
            "collector retained more than page size plus lookahead"
        );
    }

    const auto page = collector.finish();
    test_helpers::expect_true("page size", page.entries.size() == 3, "wrong page size");
    test_helpers::expect_true(
        "stable order",
        page.entries[0].name == "entry-00000" &&
            page.entries[1].name == "entry-00001" &&
            page.entries[2].name == "entry-00002",
        "page is not name-sorted"
    );
    test_helpers::expect_true(
        "continuation",
        page.continuation_token == "entry-00002",
        "continuation does not name the last returned entry"
    );
}

void test_exact_page_has_no_continuation() {
    BrowseDirectoryPageCollector collector(2);
    collector.add(BrowseEntryInfo{.name = "beta"});
    collector.add(BrowseEntryInfo{.name = "alpha"});
    const auto page = collector.finish();
    test_helpers::expect_true(
        "exact page",
        page.entries.size() == 2 &&
            page.entries[0].name == "alpha" &&
            page.entries[1].name == "beta" &&
            page.continuation_token.empty(),
        "exact page result is invalid"
    );
}

} // namespace

int main() {
    test_retains_only_the_smallest_page_and_lookahead();
    test_exact_page_has_no_continuation();
    return test_helpers::finish("browse directory page collector tests");
}
