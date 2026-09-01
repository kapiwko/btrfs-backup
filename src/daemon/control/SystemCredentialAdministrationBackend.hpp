// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <core/Errors.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>

namespace btrfsbackup::config {
class IConfigurationActivator;
}

namespace btrfsbackup::platform::linux::storage {
class ICryptsetupOperations;
}

namespace btrfsbackup::daemon::control {

enum class CredentialMutationStage {
    AddKeyslot,
    VerifyKey,
    InspectKeyslot,
    InstallKeyFile,
    SaveMetadata,
    PublishProfile,
};

enum class CredentialRemovalStage {
    QuarantineKeyFile,
    RemoveKeyslot,
    CommitMetadata,
};

struct CredentialMutationFailure final : RecoveryRequiredError {
    CredentialMutationFailure(
        CredentialMutationStage failed_stage,
        std::string primary_error,
        bool keyslot_added,
        bool keyslot_rollback_succeeded,
        bool key_file_removed,
        bool metadata_restored,
        bool profile_restored
    );

    CredentialMutationStage failed_stage;
    std::string primary_error;
    bool keyslot_added;
    bool keyslot_rollback_succeeded;
    bool key_file_removed;
    bool metadata_restored;
    bool profile_restored;
};

struct CredentialRemovalFailure final : RecoveryRequiredError {
    CredentialRemovalFailure(
        CredentialRemovalStage failed_stage,
        std::string primary_error,
        bool key_file_quarantined,
        bool key_file_restored,
        bool keyslot_state_known,
        bool keyslot_removed,
        bool metadata_committed
    );

    CredentialRemovalStage failed_stage;
    std::string primary_error;
    bool key_file_quarantined;
    bool key_file_restored;
    bool keyslot_state_known;
    bool keyslot_removed;
    bool metadata_committed;
};

class SystemCredentialAdministrationBackend final : public ICredentialAdministrationBackend {
  public:
    SystemCredentialAdministrationBackend(
        CredentialAdministrationRoots roots,
        btrfsbackup::platform::linux::storage::ICryptsetupOperations& cryptsetup,
        btrfsbackup::config::IConfigurationActivator& configuration_activator
    );

    [[nodiscard]] std::vector<TargetCredential> list_credentials(const ProfileId& profile_id) const override;
    void add_passphrase(
        const ProfileId& profile_id,
        int authorization_secret_fd,
        int new_secret_fd,
        const std::string& label
    ) override;
    void add_key(
        const ProfileId& profile_id,
        int authorization_secret_fd,
        int key_fd,
        const std::string& label,
        bool automatic
    ) override;
    void generate_key(
        const ProfileId& profile_id,
        int authorization_secret_fd,
        const std::string& label,
        bool automatic
    ) override;
    void remove_credential(
        const ProfileId& profile_id,
        const std::string& credential_id,
        int authorization_secret_fd
    ) override;
    void register_initial_passphrase(
        const ProfileId& profile_id,
        int keyslot,
        const std::string& label
    ) override;

  private:
    CredentialAdministrationRoots roots_;
    btrfsbackup::platform::linux::storage::ICryptsetupOperations& cryptsetup_;
    btrfsbackup::config::IConfigurationActivator& configuration_activator_;
};

} // namespace btrfsbackup::daemon::control
