// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <core/identifiers.hpp>

#include "support/validation_test_helpers.hpp"

namespace {

static_assert(!std::is_default_constructible_v<btrfsbackup::ProfileId>);
static_assert(!std::is_default_constructible_v<btrfsbackup::SourceId>);
static_assert(!std::is_default_constructible_v<btrfsbackup::RunId>);
static_assert(std::is_same_v<decltype(std::declval<const btrfsbackup::ProfileId&>().value()), std::string_view>);

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

} // namespace

int main() {
    test_identifier_validation();
    return test_helpers::finish("identifier tests");
}
