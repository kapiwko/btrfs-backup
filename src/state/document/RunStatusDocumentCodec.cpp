// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/document/RunStatusDocumentCodec.hpp>

#include <limits>
#include <string>
#include <utility>

#include <config/json/Json.hpp>
#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <core/RuntimeTime.hpp>

namespace json = btrfsbackup::config::json;

namespace btrfsbackup::state {
namespace {

using Json = json::Json;
using document::ExtensibleValue;
using document::PrivateRunHistoryV2;
using document::PublicActivity;
using document::PublicErrorCode;
using document::PublicRunState;
using document::PublicRunStatusV3;
using document::TransferProgress;

Json parse_document(std::string_view content, int schema_version, const char* kind) {
    Json input;
    try {
        input = Json::parse(content);
    } catch (const std::exception& error) {
        throw ValidationError(std::string("invalid ") + kind + " JSON: " + error.what());
    }
    if (!input.is_object()) {
        throw ValidationError(std::string(kind) + " JSON must be an object");
    }
    const auto schema = input.find("schemaVersion");
    if (schema == input.end() || !schema->is_number_integer() || schema->get<int>() != schema_version) {
        throw ValidationError(std::string(kind) + " JSON has unsupported schemaVersion");
    }
    return input;
}

const Json& required(const Json& input, const char* field) {
    const auto item = input.find(field);
    if (item == input.end()) {
        throw ValidationError(std::string("status JSON is missing required field: ") + field);
    }
    return *item;
}

std::string required_string(const Json& input, const char* field) {
    const Json& value = required(input, field);
    if (!value.is_string()) {
        throw ValidationError(std::string("status JSON field ") + field + " must be a string");
    }
    return value.get<std::string>();
}

bool required_bool(const Json& input, const char* field) {
    const Json& value = required(input, field);
    if (!value.is_boolean()) {
        throw ValidationError(std::string("status JSON field ") + field + " must be a boolean");
    }
    return value.get<bool>();
}

std::int64_t required_integer(const Json& input, const char* field) {
    const Json& value = required(input, field);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw ValidationError(std::string("status JSON field ") + field + " must be an integer");
    }
    try {
        return value.get<std::int64_t>();
    } catch (const std::exception&) {
        throw ValidationError(std::string("status JSON field ") + field + " is out of range");
    }
}

std::uint64_t required_non_negative(const Json& input, const char* field) {
    const Json& raw = required(input, field);
    if (raw.is_number_unsigned()) {
        return raw.get<std::uint64_t>();
    }
    if (!raw.is_number_integer()) {
        throw ValidationError(std::string("status JSON field ") + field + " must be an integer");
    }
    const std::int64_t value = raw.get<std::int64_t>();
    if (value < 0) {
        throw ValidationError(std::string("status JSON field ") + field + " must be non-negative");
    }
    return static_cast<std::uint64_t>(value);
}

std::optional<std::uint64_t> required_optional_non_negative(const Json& input, const char* field) {
    const Json& raw = required(input, field);
    if (raw.is_number_unsigned()) {
        return raw.get<std::uint64_t>();
    }
    if (!raw.is_number_integer()) {
        throw ValidationError(std::string("status JSON field ") + field + " must be an integer");
    }
    const std::int64_t value = raw.get<std::int64_t>();
    if (value == -1) {
        return std::nullopt;
    }
    if (value < 0) {
        throw ValidationError(std::string("status JSON field ") + field + " must be -1 or non-negative");
    }
    return static_cast<std::uint64_t>(value);
}

std::optional<int> required_percentage(const Json& input, const char* field) {
    const std::int64_t value = required_integer(input, field);
    if (value == -1) {
        return std::nullopt;
    }
    if (value < 0 || value > 100) {
        throw ValidationError(std::string("status JSON field ") + field + " must be -1 or between 0 and 100");
    }
    return static_cast<int>(value);
}

int required_int(const Json& input, const char* field) {
    const std::int64_t value = required_integer(input, field);
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        throw ValidationError(std::string("status JSON field ") + field + " is out of range");
    }
    return static_cast<int>(value);
}

ProgressAccuracy parse_accuracy(const std::string& value) {
    if (value == "indeterminate")
        return ProgressAccuracy::Indeterminate;
    if (value == "estimated")
        return ProgressAccuracy::Estimated;
    if (value == "exact")
        return ProgressAccuracy::Exact;
    throw ValidationError("status JSON field progressAccuracy has an unsupported value");
}

PublicRunState parse_state(const std::string& value) {
    if (value == "idle")
        return PublicRunState::Idle;
    if (value == "starting")
        return PublicRunState::Starting;
    if (value == "running")
        return PublicRunState::Running;
    if (value == "validating")
        return PublicRunState::Validating;
    if (value == "validated")
        return PublicRunState::Validated;
    if (value == "skipped")
        return PublicRunState::Skipped;
    if (value == "succeeded")
        return PublicRunState::Succeeded;
    if (value == "failed")
        return PublicRunState::Failed;
    if (value == "cancelled")
        return PublicRunState::Cancelled;
    if (value == "exited")
        return PublicRunState::Exited;
    if (value == "unavailable")
        return PublicRunState::Unavailable;
    return PublicRunState::Unknown;
}

PublicActivity parse_activity(const std::string& value) {
    if (value == "preparing")
        return PublicActivity::Preparing;
    if (value == "sizing")
        return PublicActivity::Sizing;
    if (value == "transferring")
        return PublicActivity::Transferring;
    if (value == "finalizing")
        return PublicActivity::Finalizing;
    if (value == "idle")
        return PublicActivity::Idle;
    return PublicActivity::Unknown;
}

PublicErrorCode parse_public_error(const std::string& value) {
    if (value.empty())
        return PublicErrorCode::None;
    if (value == "backup.failed")
        return PublicErrorCode::Failed;
    if (value == "backup.cancelled")
        return PublicErrorCode::Cancelled;
    throw ValidationError("status JSON field errorCode has an unsupported value");
}

bool active(PublicRunState state) {
    return state == PublicRunState::Starting || state == PublicRunState::Running || state == PublicRunState::Validating;
}

ExtensibleValue extensible(std::string value, bool known) {
    return {.value = std::move(value), .known = known};
}

bool known_phase(const std::string& value) {
    for (const char* known : {
             "idle",
             "run-started",
             "source-started",
             "recover-pending",
             "cleanup-incoming",
             "before-snapshot-hook",
             "create-snapshot",
             "after-snapshot-hook",
             "send-receive",
             "sizing",
             "transferring",
             "verify-received",
             "commit-received",
             "apply-remote-retention",
             "apply-local-retention",
             "cleanup-source",
             "source-completed",
             "succeeded",
             "failed",
             "cancelled",
             "skipped",
             "validating-target",
             "validated",
             "completed",
         }) {
        if (value == known)
            return true;
    }
    return false;
}

TransferProgress parse_progress(const Json& input) {
    const std::uint64_t bytes_total_estimated = input.contains("bytesTotalEstimated")
        ? required_non_negative(input, "bytesTotalEstimated")
        : 0;
    return {
        .bytes_processed = input.contains("bytesProcessed")
            ? required_non_negative(input, "bytesProcessed")
            : 0,
        .bytes_total_estimated = bytes_total_estimated == 0
            ? std::nullopt
            : std::optional<std::uint64_t>{bytes_total_estimated},
        .speed_bps = required_non_negative(input, "speedBps"),
        .eta_seconds = required_optional_non_negative(input, "etaSeconds"),
        .source_percent = required_percentage(input, "sourceProgress"),
        .overall_percent = required_percentage(input, "overallProgress"),
        .accuracy = parse_accuracy(required_string(input, "progressAccuracy")),
    };
}

Json details_json(const RunDetails& details) {
    Json result = Json::object();
    for (const auto& [name, value] : details) {
        std::visit([&](const auto& item) { result[name] = item; }, value);
    }
    return result;
}

RunDetails parse_details(const Json& input) {
    const Json& details = required(input, "details");
    if (!details.is_object()) {
        throw ValidationError("status JSON field details must be an object");
    }
    RunDetails result;
    for (const auto& [name, value] : details.items()) {
        if (value.is_boolean())
            result[name] = value.get<bool>();
        else if (value.is_number_unsigned())
            result[name] = value.get<std::uint64_t>();
        else if (value.is_number_integer())
            result[name] = value.get<std::int64_t>();
        else if (value.is_string())
            result[name] = value.get<std::string>();
        else
            throw ValidationError("status JSON detail values must be booleans, integers, or strings");
    }
    return result;
}

void validate_public_semantics(const PublicRunStatusV3& status) {
    if (status.phase.value.empty()) {
        throw ValidationError("status JSON phase must not be empty");
    }
    if (status.state == PublicRunState::Unknown && status.unknown_state.empty()) {
        throw ValidationError("status JSON unknown state must retain its value");
    }
    if (status.activity == PublicActivity::Unknown && status.unknown_activity.empty()) {
        throw ValidationError("status JSON unknown activity must retain its value");
    }
    const auto validate_percent = [](const std::optional<int>& value, const char* field) {
        if (value.has_value() && (*value < 0 || *value > 100)) {
            throw ValidationError(std::string("status JSON field ") + field + " must be between 0 and 100");
        }
    };
    validate_percent(status.progress.source_percent, "sourceProgress");
    validate_percent(status.progress.overall_percent, "overallProgress");
    if (status.can_cancel && !active(status.state)) {
        throw ValidationError("status JSON canCancel is true for a non-active state");
    }
    if (status.can_cancel && !status.run_id.has_value()) {
        throw ValidationError("status JSON canCancel requires runId");
    }
    if (active(status.state) && !status.run_id.has_value()) {
        throw ValidationError("active status JSON requires runId");
    }
    if (status.state != PublicRunState::Unknown) {
        const PublicErrorCode expected_error = status.state == PublicRunState::Failed
            ? PublicErrorCode::Failed
            : (status.state == PublicRunState::Cancelled ? PublicErrorCode::Cancelled : PublicErrorCode::None);
        if (status.error_code != expected_error) {
            throw ValidationError("status JSON errorCode does not match state");
        }
    }
}

} // namespace

namespace document {

std::string public_run_state_name(const PublicRunStatusV3& status) {
    switch (status.state) {
    case PublicRunState::Idle:
        return "idle";
    case PublicRunState::Starting:
        return "starting";
    case PublicRunState::Running:
        return "running";
    case PublicRunState::Validating:
        return "validating";
    case PublicRunState::Validated:
        return "validated";
    case PublicRunState::Skipped:
        return "skipped";
    case PublicRunState::Succeeded:
        return "succeeded";
    case PublicRunState::Failed:
        return "failed";
    case PublicRunState::Cancelled:
        return "cancelled";
    case PublicRunState::Exited:
        return "exited";
    case PublicRunState::Unavailable:
        return "unavailable";
    case PublicRunState::Unknown:
        return status.unknown_state;
    }
    return status.unknown_state;
}

std::string public_activity_name(const PublicRunStatusV3& status) {
    switch (status.activity) {
    case PublicActivity::Preparing:
        return "preparing";
    case PublicActivity::Sizing:
        return "sizing";
    case PublicActivity::Transferring:
        return "transferring";
    case PublicActivity::Finalizing:
        return "finalizing";
    case PublicActivity::Idle:
        return "idle";
    case PublicActivity::Unknown:
        return status.unknown_activity;
    }
    return status.unknown_activity;
}

std::string public_error_code_name(PublicErrorCode code) {
    switch (code) {
    case PublicErrorCode::None:
        return {};
    case PublicErrorCode::Failed:
        return "backup.failed";
    case PublicErrorCode::Cancelled:
        return "backup.cancelled";
    }
    return {};
}

PublicRunStatusV3 RunStatusDocumentCodec::parse_public(std::string_view content) const {
    const Json input = parse_document(content, 3, "public status");
    const std::string state_value = required_string(input, "state");
    const std::string phase_value = required_string(input, "phase");
    const std::string activity_value = required_string(input, "activity");
    const std::string run_id = required_string(input, "runId");
    PublicRunStatusV3 result{
        .run_id = run_id.empty() ? std::nullopt : std::optional<RunId>{RunId{run_id}},
        .state = parse_state(state_value),
        .phase = extensible(phase_value, known_phase(phase_value)),
        .activity = parse_activity(activity_value),
        .can_cancel = required_bool(input, "canCancel"),
        .error_code = parse_public_error(required_string(input, "errorCode")),
        .source_name = required_string(input, "sourceName"),
        .target_name = required_string(input, "targetName"),
        .progress = parse_progress(input),
        .unknown_state = parse_state(state_value) == PublicRunState::Unknown ? state_value : std::string{},
        .unknown_activity = parse_activity(activity_value) == PublicActivity::Unknown ? activity_value : std::string{},
    };
    validate_public_semantics(result);
    return result;
}

std::optional<PublicRunStatusV3> RunStatusDocumentCodec::try_parse_public(std::string_view content) const noexcept {
    try {
        return parse_public(content);
    } catch (...) {
        return std::nullopt;
    }
}

PrivateRunHistoryV2 RunStatusDocumentCodec::parse_private(std::string_view content) const {
    const Json input = parse_document(content, 2, "private history");
    const std::string state = required_string(input, "state");
    const std::string phase = required_string(input, "phase");
    PrivateRunHistoryV2 result{
        .profile_id = ProfileId{required_string(input, "profileId")},
        .profile_name = required_string(input, "profileName"),
        .run_id = RunId{required_string(input, "runId")},
        .state = extensible(state, parse_state(state) != PublicRunState::Unknown),
        .phase = extensible(phase, known_phase(phase)),
        .message = required_string(input, "message"),
        .current_source_name = required_string(input, "currentSourceName"),
        .target_name = required_string(input, "targetName"),
        .source_index = required_int(input, "sourceIndex"),
        .source_count = required_int(input, "sourceCount"),
        .started_at = required_string(input, "startedAt"),
        .updated_at = required_string(input, "updatedAt"),
        .finished_at = required_string(input, "finishedAt"),
        .error_code = required_string(input, "errorCode"),
        .error_message = required_string(input, "errorMessage"),
        .details = parse_details(input),
        .recoverable = required_bool(input, "recoverable"),
        .suggested_action = required_string(input, "suggestedAction"),
        .can_cancel = required_bool(input, "canCancel"),
        .bytes_processed = required_non_negative(input, "bytesProcessed"),
        .bytes_total_estimated = required_non_negative(input, "bytesTotalEstimated"),
        .run_bytes_processed = required_non_negative(input, "runBytesProcessed"),
        .progress = parse_progress(input),
        .exit_code = required_int(input, "exitCode"),
    };
    if (result.source_index < 0 || result.source_count < 0 || result.source_index > result.source_count) {
        throw ValidationError("private history source indexes are inconsistent");
    }
    return result;
}

std::string RunStatusDocumentCodec::serialize_public(const PublicRunStatusV3& status) const {
    validate_public_semantics(status);
    const Json result = {
        {"schemaVersion", 3},
        {"runId", status.run_id.has_value() ? std::string(status.run_id->value()) : std::string{}},
        {"state", public_run_state_name(status)},
        {"phase", status.phase.value},
        {"activity", public_activity_name(status)},
        {"canCancel", status.can_cancel},
        {"errorCode", public_error_code_name(status.error_code)},
        {"sourceName", status.source_name},
        {"targetName", status.target_name},
        {"bytesProcessed", status.progress.bytes_processed},
        {"bytesTotalEstimated", status.progress.bytes_total_estimated.value_or(0)},
        {"speedBps", status.progress.speed_bps},
        {"etaSeconds", status.progress.eta_seconds.has_value() ? Json(*status.progress.eta_seconds) : Json(-1)},
        {"sourceProgress", status.progress.source_percent.value_or(-1)},
        {"overallProgress", status.progress.overall_percent.value_or(-1)},
        {"progressAccuracy", progress_accuracy_name(status.progress.accuracy)},
    };
    return json::dump_json(result);
}

std::string RunStatusDocumentCodec::serialize_private(const PrivateRunHistoryV2& history) const {
    if (history.source_index < 0 || history.source_count < 0 || history.source_index > history.source_count) {
        throw ValidationError("private history source indexes are inconsistent");
    }
    const Json result = {
        {"schemaVersion", 2},
        {"profileId", std::string(history.profile_id.value())},
        {"profileName", history.profile_name},
        {"runId", std::string(history.run_id.value())},
        {"state", history.state.value},
        {"phase", history.phase.value},
        {"message", history.message},
        {"currentSourceName", history.current_source_name},
        {"targetName", history.target_name},
        {"sourceIndex", history.source_index},
        {"sourceCount", history.source_count},
        {"startedAt", history.started_at},
        {"updatedAt", history.updated_at},
        {"finishedAt", history.finished_at},
        {"errorCode", history.error_code},
        {"errorMessage", history.error_message},
        {"details", details_json(history.details)},
        {"recoverable", history.recoverable},
        {"suggestedAction", history.suggested_action},
        {"canCancel", history.can_cancel},
        {"bytesProcessed", history.bytes_processed},
        {"bytesTotalEstimated", history.bytes_total_estimated},
        {"runBytesProcessed", history.run_bytes_processed},
        {"speedBps", history.progress.speed_bps},
        {"etaSeconds", history.progress.eta_seconds.has_value() ? Json(*history.progress.eta_seconds) : Json(-1)},
        {"sourceProgress", history.progress.source_percent.value_or(-1)},
        {"overallProgress", history.progress.overall_percent.value_or(-1)},
        {"progressAccuracy", progress_accuracy_name(history.progress.accuracy)},
        {"exitCode", history.exit_code},
    };
    return json::dump_json(result);
}

PublicRunStatusV3 make_public_status(const RunStatus& status) {
    const std::string state = run_state_name(status.state);
    const std::string phase = run_phase_name(status.phase);
    PublicActivity activity = PublicActivity::Preparing;
    if (status.state != state::RunState::Running && status.state != state::RunState::Validating)
        activity = PublicActivity::Idle;
    else if (status.phase == state::RunPhase::Sizing)
        activity = PublicActivity::Sizing;
    else if (status.phase == state::RunPhase::Transferring)
        activity = PublicActivity::Transferring;
    else if (status.phase == state::RunPhase::VerifyReceived || status.phase == state::RunPhase::CommitReceived || status.phase == state::RunPhase::ApplyRemoteRetention || status.phase == state::RunPhase::ApplyLocalRetention || status.phase == state::RunPhase::CleanupSource || status.phase == state::RunPhase::SourceCompleted)
        activity = PublicActivity::Finalizing;

    PublicErrorCode error = PublicErrorCode::None;
    if (status.error.has_value())
        error = status.state == state::RunState::Cancelled ? PublicErrorCode::Cancelled : PublicErrorCode::Failed;
    std::optional<std::uint64_t> eta;
    if (status.progress.eta_seconds.has_value()) {
        if (*status.progress.eta_seconds < 0)
            throw ValidationError("run status etaSeconds must be non-negative");
        eta = static_cast<std::uint64_t>(*status.progress.eta_seconds);
    }
    return {
        .run_id = status.run_id,
        .state = parse_state(state),
        .phase = extensible(phase, true),
        .activity = activity,
        .can_cancel = status.can_cancel,
        .error_code = error,
        .source_name = status.current_source_name,
        .target_name = status.target_name,
        .progress = {
            .bytes_processed = status.progress.processed_bytes,
            .bytes_total_estimated = status.progress.estimated_bytes,
            .speed_bps = status.progress.speed_bps,
            .eta_seconds = eta,
            .source_percent = status.progress.source_percent,
            .overall_percent = status.progress.overall_percent,
            .accuracy = status.progress.accuracy,
        },
        .unknown_state = {},
        .unknown_activity = {},
    };
}

PrivateRunHistoryV2 make_private_history(const RunStatus& status) {
    const RunError* error = status.error ? &*status.error : nullptr;
    std::optional<std::uint64_t> eta;
    if (status.progress.eta_seconds.has_value()) {
        if (*status.progress.eta_seconds < 0)
            throw ValidationError("run status etaSeconds must be non-negative");
        eta = static_cast<std::uint64_t>(*status.progress.eta_seconds);
    }
    return {
        .profile_id = status.profile_id,
        .profile_name = status.profile_name,
        .run_id = status.run_id,
        .state = extensible(run_state_name(status.state), true),
        .phase = extensible(run_phase_name(status.phase), true),
        .message = status.message,
        .current_source_name = status.current_source_name,
        .target_name = status.target_name,
        .source_index = status.source_index,
        .source_count = status.source_count,
        .started_at = format_utc_iso_timestamp(status.started_at),
        .updated_at = format_utc_iso_timestamp(status.updated_at),
        .finished_at = status.finished_at.has_value() ? format_utc_iso_timestamp(*status.finished_at) : "",
        .error_code = error == nullptr ? "" : error_code_name(error->code),
        .error_message = error == nullptr ? "" : error->message,
        .details = status.details,
        .recoverable = error != nullptr && error->recoverable,
        .suggested_action = error == nullptr ? "" : error->suggested_action.value,
        .can_cancel = status.can_cancel,
        .bytes_processed = status.progress.processed_bytes,
        .bytes_total_estimated = status.progress.estimated_bytes.value_or(0),
        .run_bytes_processed = status.progress.run_processed_bytes,
        .progress = {
            .bytes_processed = status.progress.processed_bytes,
            .bytes_total_estimated = status.progress.estimated_bytes,
            .speed_bps = status.progress.speed_bps,
            .eta_seconds = eta,
            .source_percent = status.progress.source_percent,
            .overall_percent = status.progress.overall_percent,
            .accuracy = status.progress.accuracy,
        },
        .exit_code = status.exit_code,
    };
}

} // namespace document
} // namespace btrfsbackup::state
