// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <config/ports/ConfigurationActivator.hpp>
#include <daemon/control/ProfileAdministrationService.hpp>

namespace btrfsbackup::daemon::control {

struct ProfileAdministrationRoots {
    std::filesystem::path etc_root;
    std::filesystem::path udev_root;
    std::filesystem::path systemd_root;
    std::filesystem::path public_root;
};

class SystemProfileAdministrationBackend final : public IProfileAdministrationBackend {
  public:
    SystemProfileAdministrationBackend(
        ProfileAdministrationRoots roots,
        std::filesystem::path target_mount_root,
        config::IConfigurationActivator& activator
    );

    [[nodiscard]] std::optional<EditableProfile> find_profile(const ProfileId& profile_id) const override;
    [[nodiscard]] ProfileDraftResult validate_draft(
        const ProfileId& profile_id,
        const std::string& document
    ) const override;
    ProfileDraftResult save_profile(
        const EditableProfile& expected,
        const ProfileDraftResult& draft,
        bool allow_hook_changes
    ) override;
    void delete_profile(const EditableProfile& expected) override;

  private:
    [[nodiscard]] config::Profile parse_draft(const ProfileId& profile_id, const std::string& document) const;
    void require_current(const EditableProfile& expected) const;
    [[nodiscard]] EditableProfile require_profile(const ProfileId& profile_id) const;

    ProfileAdministrationRoots roots_;
    std::filesystem::path target_mount_root_;
    config::IConfigurationActivator& activator_;
};

} // namespace btrfsbackup::daemon::control
