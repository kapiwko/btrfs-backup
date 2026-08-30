// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/document/TargetStatusDocumentCodec.hpp>

#include <exception>

#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <core/ManagerProtocol.hpp>

namespace json = btrfsbackup::config::json;

namespace btrfsbackup::state {
namespace document {

namespace {

const json::Json& required(const json::Json& input, const char* field) {
    const auto value = input.find(field);
    if (value == input.end()) {
        throw ValidationError(std::string("target status is missing field: ") + field);
    }
    return *value;
}

std::string required_string(const json::Json& input, const char* field) {
    const json::Json& value = required(input, field);
    if (!value.is_string()) {
        throw ValidationError(std::string("target status field must be a string: ") + field);
    }
    return value.get<std::string>();
}

bool required_bool(const json::Json& input, const char* field) {
    const json::Json& value = required(input, field);
    if (!value.is_boolean()) {
        throw ValidationError(std::string("target status field must be a boolean: ") + field);
    }
    return value.get<bool>();
}

std::uint64_t required_non_negative(const json::Json& input, const char* field) {
    const json::Json& value = required(input, field);
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (!value.is_number_integer()) {
        throw ValidationError(std::string("target storage field must be an integer: ") + field);
    }
    const std::int64_t result = value.get<std::int64_t>();
    if (result < 0) {
        throw ValidationError(std::string("target storage field must be non-negative: ") + field);
    }
    return static_cast<std::uint64_t>(result);
}

TargetSpaceState parse_space_state(const std::string& value) {
    if (value == "normal") {
        return TargetSpaceState::Normal;
    }
    if (value == "below-configured-minimum") {
        return TargetSpaceState::BelowConfiguredMinimum;
    }
    throw ValidationError("target storage has unknown spaceState");
}

std::optional<TargetStorageStatusV1> parse_storage(const json::Json& input) {
    try {
        if (!input.is_object() || required_non_negative(input, "schemaVersion") != static_cast<std::uint64_t>(manager_protocol::target_storage_schema_version)) {
            return std::nullopt;
        }
        const std::uint64_t capacity = required_non_negative(input, "capacityBytes");
        const std::uint64_t used = required_non_negative(input, "usedBytes");
        const std::uint64_t available = required_non_negative(input, "availableBytes");
        const std::uint64_t usage = required_non_negative(input, "usagePercent");
        const std::optional<RuntimeTimePoint> measured_at = parse_utc_timestamp(required_string(input, "measuredAt"));
        if (used > capacity || available > capacity || usage > 100 || !measured_at.has_value()) {
            return std::nullopt;
        }
        return TargetStorageStatusV1{
            .capacity_bytes = capacity,
            .used_bytes = used,
            .available_bytes = available,
            .usage_percent = static_cast<int>(usage),
            .measured_at = *measured_at,
            .live = required_bool(input, "live"),
            .space_state = parse_space_state(required_string(input, "spaceState")),
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void validate_target_status(const TargetStatusV1& status) {
    validate_profile_id(status.profile_id);
    if (status.target_name.empty() || status.state.empty()) {
        throw ValidationError("target status names must not be empty");
    }
    if (status.storage.has_value() &&
        (status.storage->used_bytes > status.storage->capacity_bytes ||
         status.storage->available_bytes > status.storage->capacity_bytes ||
         status.storage->usage_percent < 0 || status.storage->usage_percent > 100)) {
        throw ValidationError("target storage values are inconsistent");
    }
}

} // namespace

TargetStatusV1 TargetStatusDocumentCodec::parse(std::string_view document) const {
    json::Json input;
    try {
        input = json::Json::parse(document);
    } catch (const std::exception& error) {
        throw ValidationError(std::string("invalid target status JSON: ") + error.what());
    }
    if (!input.is_object() || required_non_negative(input, "schemaVersion") != static_cast<std::uint64_t>(manager_protocol::device_state_schema_version)) {
        throw ValidationError("target status JSON has unsupported schemaVersion");
    }
    TargetStatusV1 result{
        .profile_id = required_string(input, "profileId"),
        .target_name = required_string(input, "targetName"),
        .state = required_string(input, "state"),
        .connected = required_bool(input, "connected"),
        .unlocked = required_bool(input, "unlocked"),
        .mounted = required_bool(input, "mounted"),
        .safe_to_remove = required_bool(input, "safeToRemove"),
        .storage = std::nullopt,
    };
    validate_target_status(result);
    if (const auto storage = input.find("storage"); storage != input.end()) {
        result.storage = parse_storage(*storage);
    }
    return result;
}

std::optional<TargetStatusV1> TargetStatusDocumentCodec::try_parse(std::string_view document) const noexcept {
    try {
        return parse(document);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string TargetStatusDocumentCodec::serialize(const TargetStatusV1& status) const {
    validate_target_status(status);
    json::Json document{
        {"schemaVersion", manager_protocol::device_state_schema_version},
        {"profileId", status.profile_id},
        {"targetName", status.target_name},
        {"state", status.state},
        {"connected", status.connected},
        {"unlocked", status.unlocked},
        {"mounted", status.mounted},
        {"safeToRemove", status.safe_to_remove},
    };
    if (status.storage.has_value()) {
        document["storage"] = {
            {"schemaVersion", manager_protocol::target_storage_schema_version},
            {"capacityBytes", status.storage->capacity_bytes},
            {"usedBytes", status.storage->used_bytes},
            {"availableBytes", status.storage->available_bytes},
            {"usagePercent", status.storage->usage_percent},
            {"measuredAt", format_utc_iso_timestamp(status.storage->measured_at)},
            {"live", status.storage->live},
            {"spaceState", target_space_state_name(status.storage->space_state)},
        };
    }
    return json::dump_json(document);
}

std::string target_space_state_name(TargetSpaceState state) {
    switch (state) {
    case TargetSpaceState::Normal:
        return "normal";
    case TargetSpaceState::BelowConfiguredMinimum:
        return "below-configured-minimum";
    }
    return "normal";
}

} // namespace document
} // namespace btrfsbackup::state
