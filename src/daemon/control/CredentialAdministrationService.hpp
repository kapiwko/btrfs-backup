// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <core/Identifiers.hpp>
#include <daemon/control/OperationalControlService.hpp>

namespace btrfsbackup::daemon::control {

struct CredentialAdministrationRoots {
    std::filesystem::path config_root{"/etc/btrfs-backup"};
    std::filesystem::path metadata_root{"/etc/btrfs-backup/credentials"};
    std::filesystem::path key_root{"/etc/btrfs-backup/keys"};
    std::filesystem::path lock_root{"/run/btrfs-backup/locks"};
    std::filesystem::path udev_root{"/etc/udev/rules.d"};
    std::filesystem::path systemd_root{"/etc/systemd/system"};
    std::filesystem::path public_root{"/var/lib/btrfs-backup/public/profiles"};
};

struct TargetCredential {
    std::string id;
    std::string label;
    std::string type;
    int keyslot = -1;
    bool managed = false;
    bool automatic = false;
};

class ICredentialAdministrationBackend {
  public:
    virtual ~ICredentialAdministrationBackend() = default;
    [[nodiscard]] virtual std::vector<TargetCredential> list_credentials(const ProfileId& profile_id) const = 0;
    virtual void add_passphrase(
        const ProfileId& profile_id,
        int authorization_secret_fd,
        int new_secret_fd,
        const std::string& label
    ) = 0;
    virtual void add_key(
        const ProfileId& profile_id,
        int authorization_secret_fd,
        int key_fd,
        const std::string& label,
        bool automatic
    ) = 0;
    virtual void generate_key(
        const ProfileId& profile_id,
        int authorization_secret_fd,
        const std::string& label,
        bool automatic
    ) = 0;
    virtual void remove_credential(
        const ProfileId& profile_id,
        const std::string& credential_id,
        int authorization_secret_fd
    ) = 0;
    virtual void register_initial_passphrase(
        const ProfileId& profile_id,
        int keyslot,
        const std::string& label
    ) = 0;
};

class CredentialAdministrationService {
  public:
    CredentialAdministrationService(IManagerAuthorizer& authorizer, ICredentialAdministrationBackend& backend);

    [[nodiscard]] std::vector<TargetCredential> list_credentials(const std::string& profile_id) const;
    [[nodiscard]] std::vector<TargetCredential> add_passphrase(
        const std::string& caller,
        const std::string& profile_id,
        int authorization_secret_fd,
        int new_secret_fd,
        const std::string& label
    );
    [[nodiscard]] std::vector<TargetCredential> add_key(
        const std::string& caller,
        const std::string& profile_id,
        int authorization_secret_fd,
        int key_fd,
        const std::string& label,
        bool automatic
    );
    [[nodiscard]] std::vector<TargetCredential> generate_key(
        const std::string& caller,
        const std::string& profile_id,
        int authorization_secret_fd,
        const std::string& label,
        bool automatic
    );
    [[nodiscard]] std::vector<TargetCredential> remove_credential(
        const std::string& caller,
        const std::string& profile_id,
        const std::string& credential_id,
        int authorization_secret_fd
    );

  private:
    void authorize(const std::string& caller) const;
    static std::string require_label(const std::string& label);

    IManagerAuthorizer& authorizer_;
    ICredentialAdministrationBackend& backend_;
};

} // namespace btrfsbackup::daemon::control
