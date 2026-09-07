// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <optional>
#include <set>
#include <vector>

#include <config/json/JsonIo.hpp>
#include <daemon/control/OperationalControlService.hpp>

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

struct ProfileDetails {
    std::string profile_id;
    std::string generation;
    std::string fingerprint;
    std::string document;
    bool configuration_valid = true;
    std::string configuration_error_code;
    std::vector<std::string> source_candidates;
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
    [[nodiscard]] virtual std::vector<std::filesystem::path> source_candidates() const {
        return {};
    }
};

class ProfileAdministrationService {
  public:
    ProfileAdministrationService(IManagerAuthorizer& authorizer, IProfileAdministrationBackend& backend);

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
    void require_available_subvolume(const std::filesystem::path& path) const;

    IManagerAuthorizer& authorizer_;
    IProfileAdministrationBackend& backend_;
};

} // namespace btrfsbackup::daemon::control
