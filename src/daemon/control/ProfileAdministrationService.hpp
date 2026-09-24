// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <config/json/JsonIo.hpp>
#include <daemon/control/OperationalControlService.hpp>
#include <daemon/control/BrowseSessionService.hpp>

namespace btrfsbackup::daemon::control {

struct EditableProfile {
    std::string profile_id;
    std::string generation;
    std::string fingerprint;
    std::string document;
};

struct UnsupportedProfileIdentity {
    std::string profile_id;
    int detected_schema_version = 0;
    std::string fingerprint;
    std::optional<std::string> managed_artifact_manifest_fingerprint;

    bool operator==(const UnsupportedProfileIdentity&) const = default;
};

struct ProfileSourceCandidate {
    std::string id;
    std::filesystem::path subvolume;
    std::string filesystem_uuid;
    std::filesystem::path mount_root;
    std::filesystem::path local_snapshot_root;
    std::string subvolume_uuid;

    bool operator==(const ProfileSourceCandidate&) const = default;
};

struct ProfileDetails {
    std::string profile_id;
    std::string generation;
    std::string fingerprint;
    std::string document;
    bool configuration_valid = true;
    std::string configuration_error_code;
    std::vector<ProfileSourceCandidate> source_candidates;
};

enum class SourceSubvolumeState { Available,
                                  Missing,
                                  NotSubvolume,
                                  Unavailable };

struct ProfileConfigurationHealth {
    bool valid = true;
    std::string error_code;
};

struct ProfileDraftResult {
    std::string profile_id;
    std::string generation;
    std::string fingerprint;
    std::string document;
    bool valid = true;
};

class IProfileAdministrationBackend {
  public:
    virtual ~IProfileAdministrationBackend() = default;
    [[nodiscard]] virtual std::optional<EditableProfile> find_profile(const ProfileId& profile_id) const = 0;
    [[nodiscard]] virtual ProfileDraftResult validate_draft(
        const ProfileId& profile_id,
        const std::string& document
    ) const = 0;
    virtual ProfileDraftResult save_profile(
        const EditableProfile& expected,
        const ProfileDraftResult& draft,
        bool allow_hook_changes
    ) = 0;
    virtual void delete_profile(const EditableProfile& expected) = 0;
    [[nodiscard]] virtual UnsupportedProfileIdentity inspect_unsupported_profile(
        const ProfileId& profile_id
    ) const = 0;
    virtual void retire_unsupported_profile(const UnsupportedProfileIdentity& expected) = 0;
    virtual void set_profile_enabled(const EditableProfile& expected, bool enabled) = 0;
    [[nodiscard]] virtual SourceSubvolumeState inspect_source_subvolume(const std::filesystem::path&) const {
        return SourceSubvolumeState::Available;
    }
    [[nodiscard]] virtual std::vector<ProfileSourceCandidate> source_candidates() const {
        return {};
    }
    [[nodiscard]] virtual ProfileSourceCandidate source_candidate_from_descriptor(
        int,
        const BrowseAccessIdentity&
    ) const {
        throw std::logic_error("descriptor-backed source selection is unavailable");
    }
    [[nodiscard]] virtual ProfileSourceCandidate resolve_source_candidate(
        const std::filesystem::path& path
    ) const {
        for (const auto& candidate : source_candidates()) {
            if (candidate.subvolume.lexically_normal() == path.lexically_normal())
                return candidate;
        }
        throw std::logic_error("source candidate is unavailable");
    }
};

using ProfileCandidateClock = std::function<std::chrono::steady_clock::time_point()>;
using ProfileCandidateIdGenerator = std::function<std::string()>;

class ProfileAdministrationService {
  public:
    ProfileAdministrationService(
        IManagerAuthorizer& authorizer,
        IProfileAdministrationBackend& backend,
        std::chrono::seconds candidate_lifetime = std::chrono::minutes(5),
        ProfileCandidateIdGenerator candidate_ids = {},
        ProfileCandidateClock clock = {}
    );

    [[nodiscard]] ProfileSourceCandidate register_source_candidate(
        const std::string& caller,
        const std::string& profile_id,
        int descriptor,
        const BrowseAccessIdentity& identity
    );

    [[nodiscard]] ProfileDetails get_profile_details(const std::string& profile_id) const;
    [[nodiscard]] ProfileDetails update_profile_settings(
        const std::string& caller,
        const std::string& profile_id,
        const std::string& expected_generation,
        const std::string& expected_fingerprint,
        const std::string& request
    );
    [[nodiscard]] ProfileDetails add_profile_source(
        const std::string& caller,
        const std::string& profile_id,
        const std::string& expected_generation,
        const std::string& expected_fingerprint,
        const std::string& request
    );
    [[nodiscard]] ProfileDetails update_profile_source(
        const std::string& caller,
        const std::string& profile_id,
        const std::string& source_id,
        const std::string& expected_generation,
        const std::string& expected_fingerprint,
        const std::string& request
    );
    [[nodiscard]] ProfileDetails remove_profile_source(
        const std::string& caller,
        const std::string& profile_id,
        const std::string& source_id,
        const std::string& expected_generation,
        const std::string& expected_fingerprint
    );
    void delete_profile(
        const std::string& caller,
        const std::string& profile_id,
        const std::string& expected_generation,
        const std::string& expected_fingerprint
    );
    void retire_unsupported_profile(const std::string& caller, const std::string& profile_id);
    void set_profile_enabled(const std::string& caller, const std::string& profile_id, bool enabled);
    [[nodiscard]] ProfileConfigurationHealth configuration_health(const std::string& profile_id) const;

  private:
    [[nodiscard]] static config::json::Json parse_request(
        const std::string& payload,
        const std::set<std::string>& allowed_keys
    );
    template <typename T>
    [[nodiscard]] static T request_value(const config::json::Json& request, const char* key);
    [[nodiscard]] static const EditableProfile& require_existing(
        const std::optional<EditableProfile>& profile
    );
    [[nodiscard]] static std::string source_id_candidate(const std::string& name);
    [[nodiscard]] static std::string unique_source_id(
        const config::json::Json& sources,
        const std::string& name
    );
    [[nodiscard]] static config::json::Json::iterator find_source(
        config::json::Json& sources,
        const std::string& source_id
    );
    void require_authorized(const std::string& caller, ManagerAuthorizationAction action);
    static EditableProfile expected_profile(
        const ProfileId& profile_id,
        const std::string& generation,
        const std::string& fingerprint
    );
    static void require_current(const EditableProfile& current, const EditableProfile& expected);
    static void require_current(const std::optional<EditableProfile>& current, const EditableProfile& expected);
    [[nodiscard]] ProfileDetails save_document(
        const std::string& caller,
        const EditableProfile& current,
        const std::string& document,
        const std::optional<std::filesystem::path>& source_to_recheck = std::nullopt
    );
    [[nodiscard]] ProfileDetails details_from(const EditableProfile& profile) const;
    [[nodiscard]] ProfileSourceCandidate require_source_candidate(
        const std::string& caller,
        const std::string& profile_id,
        const std::string& candidate_id,
        const std::string& excluded_filesystem_uuid
    );
    static std::string random_candidate_id();
    void expire_source_candidates(std::chrono::steady_clock::time_point now);
    void consume_source_candidate(const std::string& caller, const std::string& candidate_id);
    void require_available_subvolume(const std::filesystem::path& path) const;

    IManagerAuthorizer& authorizer_;
    IProfileAdministrationBackend& backend_;
    struct StoredSourceCandidate {
        ProfileSourceCandidate candidate;
        std::string caller;
        std::string profile_id;
        std::chrono::steady_clock::time_point expires_at;
    };
    std::chrono::seconds candidate_lifetime_;
    ProfileCandidateIdGenerator candidate_ids_;
    ProfileCandidateClock clock_;
    std::mutex source_candidates_mutex_;
    std::map<std::string, StoredSourceCandidate> stored_source_candidates_;
};

} // namespace btrfsbackup::daemon::control
