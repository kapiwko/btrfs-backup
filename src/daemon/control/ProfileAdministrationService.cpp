// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProfileAdministrationService.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <ranges>
#include <set>

#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {

namespace {

using Json = config::json::Json;

Json parse_request(const std::string& payload, const std::set<std::string>& allowed_keys) {
    if (payload.size() > 64U * 1024U)
        throw ValidationError("profile administration request is too large");
    try {
        Json request = Json::parse(payload);
        if (!request.is_object())
            throw ValidationError("profile administration request must be an object");
        for (const auto& [key, value] : request.items()) {
            static_cast<void>(value);
            if (!allowed_keys.contains(key))
                throw ValidationError("unsupported profile administration field: " + key);
        }
        return request;
    } catch (const Json::exception& error) {
        throw ValidationError("profile administration request is not valid JSON: " + std::string(error.what()));
    }
}

template <typename T>
T request_value(const Json& request, const char* key) {
    try {
        return request.at(key).get<T>();
    } catch (const Json::exception&) {
        throw ValidationError(std::string("invalid or missing profile administration field: ") + key);
    }
}

const EditableProfile& require_existing(const std::optional<EditableProfile>& profile) {
    if (!profile.has_value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    return *profile;
}

std::string source_id_candidate(const std::string& name) {
    std::string result;
    bool previous_separator = false;
    for (const unsigned char character : name) {
        const bool accepted = std::isalnum(character) != 0 || character == '.' || character == '_';
        if (accepted) {
            result.push_back(static_cast<char>(std::tolower(character)));
            previous_separator = false;
        } else if (!result.empty() && !previous_separator) {
            result.push_back('-');
            previous_separator = true;
        }
        if (result.size() == 48)
            break;
    }
    while (!result.empty() && result.back() == '-')
        result.pop_back();
    return result.empty() ? "source" : result;
}

std::string unique_source_id(const Json& sources, const std::string& name) {
    std::set<std::string> existing;
    for (const auto& source : sources)
        existing.insert(source.at("id").get<std::string>());
    const std::string base = source_id_candidate(name);
    if (!existing.contains(base))
        return base;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = base.substr(0, 58) + "-" + std::to_string(suffix);
        if (!existing.contains(candidate))
            return candidate;
    }
    throw ValidationError("cannot allocate a unique source identifier");
}

Json::iterator find_source(Json& sources, const std::string& source_id) {
    return std::find_if(sources.begin(), sources.end(), [&](const Json& source) {
        return source.value("id", "") == source_id;
    });
}

} // namespace

ProfileAdministrationService::ProfileAdministrationService(
    IManagerAuthorizer& authorizer,
    IProfileAdministrationBackend& backend
) : authorizer_(authorizer), backend_(backend) {
}

void ProfileAdministrationService::require_authorized(
    const std::string& caller,
    ManagerAuthorizationAction action
) {
    if (caller.empty() || !authorizer_.authorize(caller, action) || !authorizer_.caller_is_active(caller)) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "profile administration was not authorized");
    }
}

EditableProfile ProfileAdministrationService::expected_profile(
    const ProfileId& profile_id,
    const std::string& generation,
    const std::string& fingerprint
) {
    return {
        .profile_id = std::string(profile_id.value()),
        .generation = generation,
        .fingerprint = fingerprint,
        .document = {},
    };
}

void ProfileAdministrationService::require_current(
    const EditableProfile& current,
    const EditableProfile& expected
) {
    if (current.profile_id != expected.profile_id || current.generation != expected.generation ||
        current.fingerprint != expected.fingerprint) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "profile configuration changed");
    }
}

void ProfileAdministrationService::require_current(
    const std::optional<EditableProfile>& current,
    const EditableProfile& expected
) {
    if (!current.has_value()) {
        if (!expected.generation.empty() || !expected.fingerprint.empty())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "profile configuration changed");
        return;
    }
    require_current(*current, expected);
}

ProfileDetails ProfileAdministrationService::get_profile_details(const std::string& profile_id) const {
    const auto profile = backend_.find_profile(ProfileId(profile_id));
    if (!profile.has_value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    return details_from(*profile);
}

ProfileConfigurationHealth ProfileAdministrationService::configuration_health(const std::string& profile_id) const {
    const auto profile = backend_.find_profile(ProfileId(profile_id));
    if (!profile.has_value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    const Json document = Json::parse(profile->document);
    for (const auto& source : document.at("sources")) {
        const auto path = std::filesystem::path(source.at("subvolume").get<std::string>());
        switch (backend_.inspect_source_subvolume(path)) {
        case SourceSubvolumeState::Available:
            break;
        case SourceSubvolumeState::Missing:
            return {false, "configuration.source_missing"};
        case SourceSubvolumeState::NotSubvolume:
            return {false, "configuration.source_not_subvolume"};
        case SourceSubvolumeState::Unavailable:
            return {false, "configuration.source_unavailable"};
        }
    }
    return {};
}

ProfileDetails ProfileAdministrationService::details_from(const EditableProfile& profile) const {
    const auto health = configuration_health(profile.profile_id);
    const Json document = Json::parse(profile.document);
    std::set<std::string> configured_sources;
    for (const auto& source : document.at("sources")) {
        configured_sources.insert(
            std::filesystem::path(source.at("subvolume").get<std::string>()).lexically_normal().string()
        );
    }
    std::vector<std::string> candidates;
    for (const auto& candidate : backend_.source_candidates()) {
        const std::string normalized = candidate.lexically_normal().string();
        if (!configured_sources.contains(normalized))
            candidates.push_back(normalized);
    }
    std::ranges::sort(candidates);
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return {
        .profile_id = profile.profile_id,
        .generation = profile.generation,
        .fingerprint = profile.fingerprint,
        .document = profile.document,
        .configuration_valid = health.valid,
        .configuration_error_code = health.error_code,
        .source_candidates = std::move(candidates),
    };
}

void ProfileAdministrationService::require_available_subvolume(const std::filesystem::path& path) const {
    switch (backend_.inspect_source_subvolume(path)) {
    case SourceSubvolumeState::Available:
        return;
    case SourceSubvolumeState::Missing:
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceMissing, "source subvolume does not exist");
    case SourceSubvolumeState::NotSubvolume:
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceNotSubvolume, "source path is not a Btrfs subvolume");
    case SourceSubvolumeState::Unavailable:
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceUnavailable, "source subvolume cannot be inspected");
    }
}

ProfileDetails ProfileAdministrationService::save_document(
    const std::string& caller,
    const EditableProfile& current,
    const std::string& document,
    const std::optional<std::filesystem::path>& source_to_recheck
) {
    const ProfileId id(current.profile_id);
    const ProfileDraftResult draft = backend_.validate_draft(id, document);
    require_authorized(caller, ManagerAuthorizationAction::ManageProfileConfiguration);
    require_current(backend_.find_profile(id), current);
    if (source_to_recheck.has_value())
        require_available_subvolume(*source_to_recheck);
    const ProfileDraftResult saved = backend_.save_profile(current, draft, false);
    return details_from({saved.profile_id, saved.generation, saved.fingerprint, saved.document});
}

ProfileDetails ProfileAdministrationService::update_profile_settings(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint,
    const std::string& request_payload
) {
    const ProfileId id(profile_id);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    const auto current = backend_.find_profile(id);
    require_current(current, expected);
    const EditableProfile& existing = require_existing(current);
    const Json request = parse_request(request_payload, {"name", "dailyLimit", "autoEject"});
    Json document = Json::parse(existing.document);
    document["name"] = request_value<std::string>(request, "name");
    document["settings"]["dailyLimit"] = request_value<bool>(request, "dailyLimit");
    document["settings"]["autoEject"] = request_value<bool>(request, "autoEject");
    return save_document(caller, existing, config::json::dump_json(document));
}

ProfileDetails ProfileAdministrationService::add_profile_source(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint,
    const std::string& request_payload
) {
    const ProfileId id(profile_id);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    const auto current = backend_.find_profile(id);
    require_current(current, expected);
    const EditableProfile& existing = require_existing(current);
    const Json request = parse_request(request_payload, {"name", "subvolume", "localRetention", "remoteRetention"});
    Json document = Json::parse(existing.document);
    Json& sources = document["sources"];
    const std::string name = request_value<std::string>(request, "name");
    const std::filesystem::path subvolume = request_value<std::string>(request, "subvolume");
    require_available_subvolume(subvolume);
    const std::string source_id = unique_source_id(sources, name);
    sources.push_back({
        {"id", source_id},
        {"name", name},
        {"enabled", true},
        {"subvolume", subvolume.lexically_normal().string()},
        {"localSnapshotDir", (subvolume.parent_path() / ".snapshots" / "btrfs-backup" / source_id).lexically_normal().string()},
        {"remoteSubdir", source_id},
        {"localRetention", request_value<int>(request, "localRetention")},
        {"remoteRetention", request_value<int>(request, "remoteRetention")},
    });
    return save_document(caller, existing, config::json::dump_json(document), subvolume);
}

ProfileDetails ProfileAdministrationService::update_profile_source(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint,
    const std::string& request_payload
) {
    const ProfileId id(profile_id);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    const auto current = backend_.find_profile(id);
    require_current(current, expected);
    const EditableProfile& existing = require_existing(current);
    const Json request = parse_request(request_payload, {"name", "localRetention", "remoteRetention"});
    Json document = Json::parse(existing.document);
    Json& sources = document["sources"];
    const auto source = find_source(sources, source_id);
    if (source == sources.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile source does not exist");
    (*source)["name"] = request_value<std::string>(request, "name");
    (*source)["localRetention"] = request_value<int>(request, "localRetention");
    (*source)["remoteRetention"] = request_value<int>(request, "remoteRetention");
    return save_document(caller, existing, config::json::dump_json(document));
}

ProfileDetails ProfileAdministrationService::remove_profile_source(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& source_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint
) {
    const ProfileId id(profile_id);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    const auto current = backend_.find_profile(id);
    require_current(current, expected);
    const EditableProfile& existing = require_existing(current);
    Json document = Json::parse(existing.document);
    Json& sources = document["sources"];
    const auto source = find_source(sources, source_id);
    if (source == sources.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile source does not exist");
    sources.erase(source);
    return save_document(caller, existing, config::json::dump_json(document));
}

void ProfileAdministrationService::delete_profile(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint
) {
    const ProfileId id(profile_id);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    require_current(backend_.find_profile(id), expected);
    require_authorized(caller, ManagerAuthorizationAction::DeleteProfileConfiguration);
    require_current(backend_.find_profile(id), expected);
    backend_.delete_profile(expected);
}

void ProfileAdministrationService::set_profile_enabled(
    const std::string& caller,
    const std::string& profile_id,
    bool enabled
) {
    const ProfileId id(profile_id);
    const auto current = backend_.find_profile(id);
    if (!current.has_value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    require_authorized(caller, ManagerAuthorizationAction::SetProfileEnabled);
    require_current(backend_.find_profile(id), *current);
    backend_.set_profile_enabled(*current, enabled);
}

} // namespace btrfsbackup::daemon::control
