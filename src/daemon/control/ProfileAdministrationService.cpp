// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProfileAdministrationService.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ranges>
#include <set>

#include <sys/random.h>

#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {

using Json = config::json::Json;

namespace {

std::string target_filesystem_uuid(const Json& document) {
    if (!document.contains("target") || !document.at("target").is_object())
        return {};
    return document.at("target").value("btrfsUuid", "");
}

bool same_source_candidate(
    const ProfileSourceCandidate& expected,
    const ProfileSourceCandidate& current
) {
    return expected.id == current.id && expected.subvolume == current.subvolume &&
        expected.filesystem_uuid == current.filesystem_uuid && expected.mount_root == current.mount_root &&
        expected.local_snapshot_root == current.local_snapshot_root &&
        (expected.subvolume_uuid.empty() || expected.subvolume_uuid == current.subvolume_uuid);
}

} // namespace

ProfileAdministrationService::ProfileAdministrationService(
    IManagerAuthorizer& authorizer,
    IProfileAdministrationBackend& backend,
    std::chrono::seconds candidate_lifetime,
    ProfileCandidateIdGenerator candidate_ids,
    ProfileCandidateClock clock
) : authorizer_(authorizer), backend_(backend), candidate_lifetime_(candidate_lifetime),
    candidate_ids_(candidate_ids ? std::move(candidate_ids) : ProfileCandidateIdGenerator{random_candidate_id}),
    clock_(clock ? std::move(clock) : ProfileCandidateClock{[] { return std::chrono::steady_clock::now(); }}) {
    if (candidate_lifetime_ <= std::chrono::seconds::zero())
        throw std::invalid_argument("profile source candidate lifetime must be positive");
}

std::string ProfileAdministrationService::random_candidate_id() {
    std::array<unsigned char, 16> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        throw ValidationError("cannot generate a profile source candidate identifier");
    }
    std::ostringstream value;
    value << "profile-source-" << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        value << std::setw(2) << static_cast<unsigned>(byte);
    return value.str();
}

void ProfileAdministrationService::expire_source_candidates(std::chrono::steady_clock::time_point now) {
    std::erase_if(stored_source_candidates_, [&](const auto& item) {
        return item.second.expires_at <= now;
    });
}

ProfileSourceCandidate ProfileAdministrationService::register_source_candidate(
    const std::string& caller,
    const std::string& profile_id,
    int descriptor,
    const BrowseAccessIdentity& identity
) {
    if (caller.empty() || !authorizer_.caller_is_active(caller))
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "source selection requires an active caller");
    const auto existing_profile = backend_.find_profile(ProfileId(profile_id));
    const EditableProfile& profile = require_existing(existing_profile);
    const std::string target_uuid = target_filesystem_uuid(Json::parse(profile.document));
    ProfileSourceCandidate candidate = backend_.source_candidate_from_descriptor(descriptor, identity);
    if (!target_uuid.empty() && candidate.filesystem_uuid == target_uuid)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceUnavailable, "backup target cannot be used as a source");
    const auto now = clock_();
    std::lock_guard lock(source_candidates_mutex_);
    expire_source_candidates(now);
    const auto caller_candidate_count = std::ranges::count_if(
        stored_source_candidates_,
        [&](const auto& item) {
            return item.second.caller == caller && item.second.profile_id == profile_id;
        }
    );
    if (caller_candidate_count >= 16) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "too many active profile source candidates"
        );
    }
    if (stored_source_candidates_.size() >= 1024) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "too many active profile source candidates"
        );
    }
    for (int attempt = 0; attempt < 16; ++attempt) {
        std::string id = candidate_ids_();
        if (id.empty() || stored_source_candidates_.contains(id))
            continue;
        candidate.id = id;
        stored_source_candidates_.insert_or_assign(
            id,
            StoredSourceCandidate{candidate, caller, profile_id, now + candidate_lifetime_}
        );
        return candidate;
    }
    throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "cannot allocate a profile source candidate identifier");
}

Json ProfileAdministrationService::parse_request(
    const std::string& payload,
    const std::set<std::string>& allowed_keys
) {
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
T ProfileAdministrationService::request_value(const Json& request, const char* key) {
    try {
        return request.at(key).template get<T>();
    } catch (const Json::exception&) {
        throw ValidationError(std::string("invalid or missing profile administration field: ") + key);
    }
}

const EditableProfile& ProfileAdministrationService::require_existing(
    const std::optional<EditableProfile>& profile
) {
    if (!profile.has_value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    return *profile;
}

std::string ProfileAdministrationService::source_id_candidate(const std::string& name) {
    std::string result;
    bool previous_separator = false;
    for (const char raw_character : name) {
        const auto character = static_cast<unsigned char>(raw_character);
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

std::string ProfileAdministrationService::unique_source_id(
    const Json& sources,
    const std::string& name
) {
    std::set<std::string> existing;
    for (const auto& source : sources)
        existing.insert(source.at("id").template get<std::string>());
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

Json::iterator ProfileAdministrationService::find_source(
    Json& sources,
    const std::string& source_id
) {
    return std::find_if(sources.begin(), sources.end(), [&](const auto& source) {
        return source.value("id", "") == source_id;
    });
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
    std::vector<ProfileSourceCandidate> candidates;
    const std::string target_uuid = target_filesystem_uuid(document);
    for (const auto& candidate : backend_.source_candidates()) {
        const std::string normalized = candidate.subvolume.lexically_normal().string();
        if (!configured_sources.contains(normalized) &&
            (target_uuid.empty() || candidate.filesystem_uuid != target_uuid)) {
            candidates.push_back(candidate);
        }
    }
    std::ranges::sort(candidates, {}, [](const ProfileSourceCandidate& candidate) {
        return candidate.subvolume.lexically_normal().string();
    });
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

ProfileSourceCandidate ProfileAdministrationService::require_source_candidate(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& candidate_id,
    const std::string& excluded_filesystem_uuid
) {
    {
        const auto now = clock_();
        std::lock_guard lock(source_candidates_mutex_);
        expire_source_candidates(now);
        const auto stored = stored_source_candidates_.find(candidate_id);
        if (stored != stored_source_candidates_.end()) {
            if (stored->second.caller != caller || stored->second.profile_id != profile_id) {
                throw dbus::ManagerOperationError(dbus::ManagerErrorCode::SourceUnavailable, "source candidate is unavailable");
            }
            return stored->second.candidate;
        }
    }
    const auto candidates = backend_.source_candidates();
    const auto candidate = std::ranges::find(candidates, candidate_id, &ProfileSourceCandidate::id);
    if (candidate == candidates.end() ||
        (!excluded_filesystem_uuid.empty() && candidate->filesystem_uuid == excluded_filesystem_uuid)) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::SourceUnavailable,
            "source candidate is no longer available"
        );
    }
    return *candidate;
}

void ProfileAdministrationService::consume_source_candidate(
    const std::string& caller,
    const std::string& candidate_id
) {
    std::lock_guard lock(source_candidates_mutex_);
    const auto candidate = stored_source_candidates_.find(candidate_id);
    if (candidate != stored_source_candidates_.end() && candidate->second.caller == caller)
        stored_source_candidates_.erase(candidate);
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
    const Json request = parse_request(request_payload, {"name", "candidateId", "localRetention", "remoteRetention"});
    Json document = Json::parse(existing.document);
    Json& sources = document["sources"];
    const std::string name = request_value<std::string>(request, "name");
    const std::string candidate_id = request_value<std::string>(request, "candidateId");
    const std::string target_uuid = target_filesystem_uuid(document);
    const ProfileSourceCandidate candidate = require_source_candidate(caller, profile_id, candidate_id, target_uuid);
    require_available_subvolume(candidate.subvolume);
    const std::string source_id = unique_source_id(sources, name);
    sources.push_back({
        {"id", source_id},
        {"name", name},
        {"enabled", true},
        {"subvolume", candidate.subvolume.lexically_normal().string()},
        {"localSnapshotDir", (candidate.local_snapshot_root / source_id).lexically_normal().string()},
        {"remoteSubdir", source_id},
        {"localRetention", request_value<int>(request, "localRetention")},
        {"remoteRetention", request_value<int>(request, "remoteRetention")},
    });
    const ProfileDraftResult draft = backend_.validate_draft(id, config::json::dump_json(document));
    require_authorized(caller, ManagerAuthorizationAction::ManageProfileConfiguration);
    require_current(backend_.find_profile(id), existing);
    ProfileSourceCandidate current_candidate = backend_.resolve_source_candidate(candidate.subvolume);
    current_candidate.id = candidate.id;
    if (!same_source_candidate(candidate, current_candidate) ||
        (!target_uuid.empty() && current_candidate.filesystem_uuid == target_uuid)) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "source candidate changed"
        );
    }
    require_available_subvolume(current_candidate.subvolume);
    const ProfileDraftResult saved = backend_.save_profile(existing, draft, false);
    consume_source_candidate(caller, candidate_id);
    return details_from({saved.profile_id, saved.generation, saved.fingerprint, saved.document});
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

void ProfileAdministrationService::retire_unsupported_profile(
    const std::string& caller,
    const std::string& profile_id
) {
    const ProfileId id(profile_id);
    const UnsupportedProfileIdentity expected = backend_.inspect_unsupported_profile(id);
    require_authorized(caller, ManagerAuthorizationAction::DeleteProfileConfiguration);
    if (backend_.inspect_unsupported_profile(id) != expected) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "profile configuration changed");
    }
    backend_.retire_unsupported_profile(expected);
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
