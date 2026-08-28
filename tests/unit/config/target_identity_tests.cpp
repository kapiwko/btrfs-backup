// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <type_traits>

#include <config/model/target_identity.hpp>

#include "support/test_helpers.hpp"
#include "support/validation_test_helpers.hpp"

namespace {

static_assert(!std::is_default_constructible_v<btrfsbackup::config::LuksUuid>);
static_assert(!std::is_default_constructible_v<btrfsbackup::config::BtrfsUuid>);
static_assert(!std::is_default_constructible_v<btrfsbackup::config::PartitionUuid>);
static_assert(!std::is_default_constructible_v<btrfsbackup::config::MapperName>);
static_assert(!std::is_convertible_v<std::string, btrfsbackup::config::LuksUuid>);
static_assert(!std::is_convertible_v<btrfsbackup::config::LuksUuid, btrfsbackup::config::BtrfsUuid>);

void test_uuid_types_normalize_case() {
    const btrfsbackup::config::LuksUuid luks{"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"};
    const btrfsbackup::config::BtrfsUuid btrfs{"11111111-2222-3333-AAAA-BBBBBBBBBBBB"};

    test_helpers::expect_eq(
        "canonical LUKS UUID",
        luks.value(),
        "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    );
    test_helpers::expect_eq(
        "canonical Btrfs UUID",
        btrfs.value(),
        "11111111-2222-3333-aaaa-bbbbbbbbbbbb"
    );
}

void test_required_uuid_types_reject_invalid_values() {
    test_helpers::expect_validation_error("empty LUKS UUID", [] { (void)btrfsbackup::config::LuksUuid{""}; }, "LUKS UUID is not a canonical UUID");
    test_helpers::expect_validation_error("invalid Btrfs UUID", [] { (void)btrfsbackup::config::BtrfsUuid{"not-a-uuid"}; }, "Btrfs UUID is not a canonical UUID");
}

void test_partition_uuid_explicitly_models_absence() {
    const btrfsbackup::config::PartitionUuid missing{""};
    const btrfsbackup::config::PartitionUuid present{"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"};

    test_helpers::expect_true("empty partition UUID", missing.empty(), "empty partition UUID was rejected");
    test_helpers::expect_eq(
        "canonical partition UUID",
        present.value(),
        "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    );
}

void test_mapper_name_reuses_identifier_policy() {
    const btrfsbackup::config::MapperName mapper{"backup-disk"};
    test_helpers::expect_eq("mapper name", mapper.value(), "backup-disk");
    test_helpers::expect_validation_error("invalid mapper name", [] { (void)btrfsbackup::config::MapperName{"backup disk"}; }, "mapper name");
}

} // namespace

int main() {
    test_uuid_types_normalize_case();
    test_required_uuid_types_reject_invalid_values();
    test_partition_uuid_explicitly_models_absence();
    test_mapper_name_reuses_identifier_policy();
    return test_helpers::finish("target identity tests");
}
