// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/json/Json.hpp>
#include <state/document/TargetStatusDocumentCodec.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace {

using btrfsbackup::config::json::Json;
using btrfsbackup::state::document::TargetSpaceState;
using btrfsbackup::state::document::TargetStatusDocumentCodec;

Json target_document() {
    return {
        {"schemaVersion", 1},
        {"profileId", "default"},
        {"targetName", "Backup disk"},
        {"state", "mounted"},
        {"connected", true},
        {"unlocked", true},
        {"mounted", true},
        {"safeToRemove", false},
        {"storage",
         {
             {"schemaVersion", 1},
             {"capacityBytes", 1000},
             {"usedBytes", 400},
             {"availableBytes", 600},
             {"usagePercent", 40},
             {"measuredAt", "2026-08-30T10:00:00Z"},
             {"live", true},
             {"spaceState", "normal"},
         }},
    };
}

void test_round_trip_is_typed() {
    const TargetStatusDocumentCodec codec;
    const auto status = codec.parse(target_document().dump());
    test_helpers::expect_eq("profile", status.profile_id, "default");
    test_helpers::expect_true("storage present", status.storage.has_value(), "storage was not decoded");
    test_helpers::expect_true(
        "typed space state",
        status.storage->space_state == TargetSpaceState::Normal,
        "space state was not decoded"
    );

    const auto round_trip = codec.parse(codec.serialize(status));
    test_helpers::expect_true(
        "round-trip capacity",
        round_trip.storage.has_value() && round_trip.storage->capacity_bytes == 1000,
        "capacity changed"
    );
}

void test_storage_is_optional() {
    Json input = target_document();
    input.erase("storage");
    const auto status = TargetStatusDocumentCodec{}.parse(input.dump());
    test_helpers::expect_true("storage absent", !status.storage.has_value(), "missing storage was synthesized");
}

void test_malformed_optional_storage_is_ignored() {
    for (const auto& storage : {
             Json(nullptr),
             Json{{"schemaVersion", 2}},
             Json{
                 {"schemaVersion", 1},
                 {"capacityBytes", 100},
                 {"usedBytes", 101},
                 {"availableBytes", 0},
                 {"usagePercent", 100},
                 {"measuredAt", "2026-08-30T10:00:00Z"},
                 {"live", false},
                 {"spaceState", "normal"},
             },
         }) {
        Json input = target_document();
        input["storage"] = storage;
        const auto status = TargetStatusDocumentCodec{}.parse(input.dump());
        test_helpers::expect_true(
            "malformed storage ignored",
            !status.storage.has_value(),
            "malformed storage was accepted"
        );
    }
}

void test_invalid_parent_is_rejected() {
    Json missing = target_document();
    missing.erase("profileId");
    test_helpers::expect_validation_error(
        "missing profile",
        [&] { (void)TargetStatusDocumentCodec{}.parse(missing.dump()); },
        "missing field"
    );

    Json wrong_type = target_document();
    wrong_type["mounted"] = "yes";
    test_helpers::expect_validation_error(
        "wrong parent type",
        [&] { (void)TargetStatusDocumentCodec{}.parse(wrong_type.dump()); },
        "must be a boolean"
    );
}

} // namespace

int main() {
    test_round_trip_is_typed();
    test_storage_is_optional();
    test_malformed_optional_storage_is_ignored();
    test_invalid_parent_is_rejected();
    return test_helpers::finish("target status document codec tests");
}
