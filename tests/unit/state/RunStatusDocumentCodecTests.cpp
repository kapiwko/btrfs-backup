// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <config/json/Json.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace {

using btrfsbackup::config::json::Json;
using btrfsbackup::state::document::PublicRunState;
using btrfsbackup::state::document::PublicOperationKind;
using btrfsbackup::state::document::RunStatusDocumentCodec;

Json public_document() {
    return {
        {"schemaVersion", 4},
        {"runId", "run-1"},
        {"operationKind", "backup"},
        {"state", "running"},
        {"phase", "transferring"},
        {"activity", "transferring"},
        {"canCancel", true},
        {"errorCode", ""},
        {"sourceName", "Home"},
        {"targetName", "Backup disk"},
        {"bytesProcessed", 1048576},
        {"bytesTotalEstimated", 4194304},
        {"speedBps", 1024},
        {"etaSeconds", 20},
        {"sourceProgress", 30},
        {"overallProgress", 40},
        {"progressAccuracy", "estimated"},
    };
}

Json private_document() {
    return {
        {"schemaVersion", 2},
        {"profileId", "default"},
        {"profileName", "Default backup"},
        {"runId", "run-1"},
        {"state", "succeeded"},
        {"phase", "succeeded"},
        {"message", "Completed"},
        {"currentSourceName", "Home"},
        {"targetName", "Backup disk"},
        {"sourceIndex", 1},
        {"sourceCount", 1},
        {"startedAt", "2026-08-30T10:00:00Z"},
        {"updatedAt", "2026-08-30T10:01:00Z"},
        {"finishedAt", "2026-08-30T10:01:00Z"},
        {"errorCode", ""},
        {"errorMessage", ""},
        {"details", {{"attempt", 1}, {"verified", true}}},
        {"recoverable", false},
        {"suggestedAction", ""},
        {"canCancel", false},
        {"bytesProcessed", 1024},
        {"bytesTotalEstimated", 1024},
        {"runBytesProcessed", 1024},
        {"speedBps", 0},
        {"etaSeconds", -1},
        {"sourceProgress", 100},
        {"overallProgress", 100},
        {"progressAccuracy", "exact"},
        {"exitCode", 0},
    };
}

void expect_public_rejected(const std::string& name, const Json& document) {
    test_helpers::expect_validation_error(
        name,
        [&] { (void)RunStatusDocumentCodec{}.parse_public(document.dump()); },
        "status JSON"
    );
}

void test_public_round_trip_is_typed() {
    const RunStatusDocumentCodec codec;
    const auto status = codec.parse_public(public_document().dump());
    test_helpers::expect_true("typed state", status.state == PublicRunState::Running, "state was not decoded");
    test_helpers::expect_true("typed run id", status.run_id.has_value(), "run id was not decoded");
    test_helpers::expect_true("typed operation", status.operation_kind == PublicOperationKind::Backup, "operation kind was not decoded");
    test_helpers::expect_true("typed ETA", status.progress.eta_seconds == 20, "ETA was not decoded");
    test_helpers::expect_true("typed transferred bytes", status.progress.bytes_processed == 1048576, "processed bytes were not decoded");
    test_helpers::expect_true("typed estimated bytes", status.progress.bytes_total_estimated == 4194304, "estimated bytes were not decoded");
    const auto round_trip = codec.parse_public(codec.serialize_public(status));
    test_helpers::expect_true("round-trip progress", round_trip.progress.overall_percent == 40, "progress changed");
}

void test_public_requires_every_contract_field() {
    for (const char* field : {
             "schemaVersion",
             "runId",
             "operationKind",
             "state",
             "phase",
             "activity",
             "canCancel",
             "errorCode",
             "sourceName",
             "targetName",
             "speedBps",
             "etaSeconds",
             "sourceProgress",
             "overallProgress",
             "progressAccuracy",
         }) {
        Json input = public_document();
        input.erase(field);
        expect_public_rejected(std::string("missing ") + field, input);
    }
}

void test_public_rejects_wrong_types_and_ranges() {
    for (const char* field : {
             "schemaVersion",
             "runId",
             "operationKind",
             "state",
             "phase",
             "activity",
             "canCancel",
             "errorCode",
             "sourceName",
             "targetName",
             "speedBps",
             "etaSeconds",
             "sourceProgress",
             "overallProgress",
             "progressAccuracy",
         }) {
        Json input = public_document();
        input[field] = nullptr;
        expect_public_rejected(std::string("wrong type ") + field, input);
    }

    for (const auto& [field, value] : std::vector<std::pair<const char*, int>>{
             {"speedBps", -1},
             {"etaSeconds", -2},
             {"sourceProgress", 101},
             {"overallProgress", -2},
         }) {
        Json input = public_document();
        input[field] = value;
        expect_public_rejected(std::string("invalid range ") + field, input);
    }

    Json terminal_cancel = public_document();
    terminal_cancel["state"] = "succeeded";
    expect_public_rejected("terminal canCancel", terminal_cancel);

    Json active_without_id = public_document();
    active_without_id["runId"] = "";
    active_without_id["canCancel"] = false;
    expect_public_rejected("active without run id", active_without_id);

    Json failed_without_error = public_document();
    failed_without_error["state"] = "failed";
    failed_without_error["canCancel"] = false;
    expect_public_rejected("failed without error", failed_without_error);
}

void test_public_is_forward_compatible() {
    Json input = public_document();
    input["phase"] = "future-phase";
    input["futureField"] = {"value", 1};
    const auto status = RunStatusDocumentCodec{}.parse_public(input.dump());
    test_helpers::expect_true("unknown phase retained", status.phase.value == "future-phase", "phase was lost");
    test_helpers::expect_true("unknown phase marked", !status.phase.known, "phase was treated as known");

    input["state"] = "future-state";
    input["canCancel"] = false;
    input["errorCode"] = "backup.failed";
    const auto future_state = RunStatusDocumentCodec{}.parse_public(input.dump());
    test_helpers::expect_true("unknown state typed", future_state.state == PublicRunState::Unknown, "state was rejected");
    test_helpers::expect_eq("unknown state retained", future_state.unknown_state, "future-state");

    input["operationKind"] = "future-operation";
    const auto future_operation = RunStatusDocumentCodec{}.parse_public(input.dump());
    test_helpers::expect_true(
        "unknown operation typed",
        future_operation.operation_kind == PublicOperationKind::Unknown,
        "operation kind was rejected"
    );
    test_helpers::expect_eq(
        "unknown operation retained",
        future_operation.unknown_operation_kind,
        "future-operation"
    );
}

void test_private_history_round_trip_and_validation() {
    const RunStatusDocumentCodec codec;
    const auto history = codec.parse_private(private_document().dump());
    test_helpers::expect_eq("private profile", std::string(history.profile_id.value()), "default");
    test_helpers::expect_true("private detail", history.details.contains("verified"), "detail was lost");
    const auto round_trip = codec.parse_private(codec.serialize_private(history));
    test_helpers::expect_eq("private round trip", round_trip.message, "Completed");

    Json missing = private_document();
    missing.erase("errorMessage");
    test_helpers::expect_validation_error(
        "private missing field",
        [&] { (void)codec.parse_private(missing.dump()); },
        "missing required field"
    );
    Json nested_detail = private_document();
    nested_detail["details"]["invalid"] = Json::object();
    test_helpers::expect_validation_error(
        "private invalid detail",
        [&] { (void)codec.parse_private(nested_detail.dump()); },
        "detail values"
    );
}

} // namespace

int main() {
    test_public_round_trip_is_typed();
    test_public_requires_every_contract_field();
    test_public_rejects_wrong_types_and_ranges();
    test_public_is_forward_compatible();
    test_private_history_round_trip_and_validation();
    return test_helpers::finish("run status document codec tests");
}
