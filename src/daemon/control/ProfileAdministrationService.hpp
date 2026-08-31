// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <optional>
#include <vector>

#include <daemon/control/OperationalControlService.hpp>

namespace btrfsbackup::daemon::control {

struct EditableProfile {
    std::string profile_id;
    std::string generation;
    std::string fingerprint;
    std::string document;
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

enum class SourceSubvolumeState { Available, Missing, NotSubvolume, Unavailable };

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
    virtual void set_profile_enabled(const EditableProfile& expected, bool enabled) = 0;
    [[nodiscard]] virtual SourceSubvolumeState inspect_source_subvolume(const std::filesystem::path&) const {
        return SourceSubvolumeState::Available;
    }
    [[nodiscard]] virtual std::vector<std::filesystem::path> source_candidates() const { return {}; }
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
    void set_profile_enabled(const std::string& caller, const std::string& profile_id, bool enabled);
    [[nodiscard]] ProfileConfigurationHealth configuration_health(const std::string& profile_id) const;

  private:
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
