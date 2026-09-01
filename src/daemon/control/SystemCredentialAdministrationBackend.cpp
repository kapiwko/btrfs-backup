// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemCredentialAdministrationBackend.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string_view>
#include <unistd.h>
#include <utility>

#include <config/json/JsonIo.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {

namespace {

using btrfsbackup::config::json::Json;
using btrfsbackup::platform::linux::OwnedFileDescriptor;

struct ManagedCredential {
    std::string id;
    std::string label;
    std::string type;
    int keyslot = -1;
    fs::path key_file;
    bool automatic = false;
};

fs::path metadata_path(const CredentialAdministrationRoots& roots, const config::LuksUuid& uuid) {
    return roots.metadata_root / (uuid.value() + ".json");
}

std::vector<ManagedCredential> load_metadata(
    const CredentialAdministrationRoots& roots,
    const config::LuksUuid& uuid
) {
    const fs::path path = metadata_path(roots, uuid);
    std::error_code error;
    if (!fs::exists(path, error))
        return {};
    const Json document = config::json::load_json_file(path);
    if (!document.is_object() || document.value("schemaVersion", 0) != 1 ||
        document.value("luksUuid", "") != uuid.value() || !document.contains("credentials") ||
        !document.at("credentials").is_array()) {
        throw ValidationError("managed credential metadata is invalid");
    }
    std::vector<ManagedCredential> result;
    for (const Json& item : document.at("credentials")) {
        if (!item.is_object())
            throw ValidationError("managed credential entry is invalid");
        ManagedCredential credential{
            .id = item.value("id", ""),
            .label = item.value("label", ""),
            .type = item.value("type", ""),
            .keyslot = item.value("keyslot", -1),
            .key_file = item.value("keyFile", ""),
            .automatic = item.value("automatic", false),
        };
        if (credential.id != "slot-" + std::to_string(credential.keyslot) || credential.label.empty() ||
            (credential.type != "passphrase" && credential.type != "keyFile") ||
            (credential.type == "keyFile" &&
             (!credential.key_file.is_absolute() || credential.key_file.lexically_normal() != credential.key_file)) ||
            (credential.type == "passphrase" && !credential.key_file.empty())) {
            throw ValidationError("managed credential entry is invalid");
        }
        result.push_back(std::move(credential));
    }
    return result;
}

void save_metadata(
    const CredentialAdministrationRoots& roots,
    const config::LuksUuid& uuid,
    const std::vector<ManagedCredential>& credentials
) {
    Json items = Json::array();
    for (const ManagedCredential& credential : credentials) {
        Json item{
            {"id", credential.id},
            {"label", credential.label},
            {"type", credential.type},
            {"keyslot", credential.keyslot},
            {"automatic", credential.automatic},
        };
        if (!credential.key_file.empty())
            item["keyFile"] = credential.key_file.string();
        items.push_back(std::move(item));
    }
    platform::linux::filesystem::atomic_write(
        metadata_path(roots, uuid),
        config::json::dump_json({
            {"schemaVersion", 1},
            {"luksUuid", uuid.value()},
            {"credentials", std::move(items)},
        }),
        0600
    );
}

config::Profile load_profile(const CredentialAdministrationRoots& roots, const ProfileId& id) {
    return platform::linux::config::FileProfileRepository(roots.config_root).get(id).profile;
}

platform::linux::storage::LuksHeader inspect_target(
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    const config::Profile& profile
) {
    const auto header = cryptsetup.inspect_luks2(profile.target.device.value());
    if (header.uuid != profile.target.luks_uuid.value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "LUKS target identity changed");
    return header;
}

platform::linux::filesystem::FileLock lock_target(
    const CredentialAdministrationRoots& roots,
    const config::Profile& profile
) {
    platform::linux::filesystem::FileLock lock(
        platform::linux::filesystem::target_lock_path(roots.lock_root, profile.target.luks_uuid)
    );
    if (!lock.try_acquire())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "backup target is active");
    return lock;
}

int added_keyslot(const std::vector<int>& before, const std::vector<int>& after) {
    std::vector<int> added;
    std::ranges::set_difference(after, before, std::back_inserter(added));
    if (added.size() != 1)
        throw ValidationError("could not identify the new LUKS keyslot");
    return added.front();
}

std::vector<TargetCredential> merge_credentials(
    const std::vector<int>& slots,
    const std::vector<ManagedCredential>& managed
) {
    std::vector<TargetCredential> result;
    for (const int slot : slots) {
        const auto item = std::ranges::find(managed, slot, &ManagedCredential::keyslot);
        result.push_back(item == managed.end() ? TargetCredential{"slot-" + std::to_string(slot), "Other credential", "unknown", slot, false, false} : TargetCredential{item->id, item->label, item->type, slot, true, item->automatic});
    }
    return result;
}

void set_automatic_key(
    const CredentialAdministrationRoots& roots,
    config::Profile profile,
    const fs::path& key_file,
    config::IConfigurationActivator& activator
) {
    profile.target.activation = config::KeyFileActivation{config::KeyFilePath{key_file}};
    profile.configuration_generation = config::ConfigurationGeneration{""};
    platform::linux::config::install_profile(
        profile,
        {roots.config_root, roots.udev_root, roots.systemd_root, roots.public_root},
        activator
    );
}

std::string_view stage_name(CredentialMutationStage stage) {
    switch (stage) {
    case CredentialMutationStage::AddKeyslot:
        return "add-keyslot";
    case CredentialMutationStage::VerifyKey:
        return "verify-key";
    case CredentialMutationStage::InspectKeyslot:
        return "inspect-keyslot";
    case CredentialMutationStage::InstallKeyFile:
        return "install-key-file";
    case CredentialMutationStage::SaveMetadata:
        return "save-metadata";
    case CredentialMutationStage::PublishProfile:
        return "publish-profile";
    }
    return "unknown";
}

std::string exception_message(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& exception) {
        return exception.what();
    } catch (...) {
        return "unknown failure";
    }
}

void rewind_secret(int descriptor) {
    if (::lseek(descriptor, 0, SEEK_SET) < 0)
        throw ValidationError("cannot rewind credential authorization secret");
}

std::string mutation_failure_message(
    CredentialMutationStage stage,
    const std::string& primary_error,
    bool keyslot_added,
    bool keyslot_rollback_succeeded,
    bool key_file_removed,
    bool metadata_restored,
    bool profile_restored
) {
    std::ostringstream message;
    message << "credential mutation failed during " << stage_name(stage) << ": " << primary_error
            << "; rollback incomplete"
            << "; keyslotAdded=" << keyslot_added
            << "; keyslotRollbackSucceeded=" << keyslot_rollback_succeeded
            << "; keyFileRemoved=" << key_file_removed
            << "; metadataRestored=" << metadata_restored
            << "; profileRestored=" << profile_restored;
    return message.str();
}

void add_managed_credential(
    const CredentialAdministrationRoots& roots,
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    config::IConfigurationActivator& activator,
    const ProfileId& profile_id,
    int authorization_secret_fd,
    OwnedFileDescriptor new_secret,
    const std::string& label,
    const std::string& type,
    bool automatic
) {
    config::Profile profile = load_profile(roots, profile_id);
    auto target_lock = lock_target(roots, profile);
    static_cast<void>(target_lock);
    const auto before = inspect_target(cryptsetup, profile);
    OwnedFileDescriptor authorization =
        platform::linux::filesystem::copy_secret_to_sealed_file(authorization_secret_fd);
    std::vector<ManagedCredential> metadata = load_metadata(roots, profile.target.luks_uuid);
    const std::vector<ManagedCredential> previous_metadata = metadata;
    fs::path key_file;

    CredentialMutationStage stage = CredentialMutationStage::AddKeyslot;
    bool keyslot_added = false;
    bool keyslot_state_known = false;
    int slot = -1;
    bool key_file_install_attempted = false;
    bool metadata_restore_required = false;
    bool profile_restore_required = false;
    try {
        cryptsetup.add_key(profile.target.device.value(), authorization.get(), new_secret.get());
        keyslot_added = true;
        stage = CredentialMutationStage::VerifyKey;
        cryptsetup.test_key(profile.target.device.value(), new_secret.get());
        stage = CredentialMutationStage::InspectKeyslot;
        const auto after = inspect_target(cryptsetup, profile);
        slot = added_keyslot(before.keyslots, after.keyslots);
        keyslot_state_known = true;
        const std::string id = "slot-" + std::to_string(slot);
        if (type == "keyFile") {
            key_file =
                (roots.key_root / (std::string(profile.id.value()) + "-" + id + ".key")).lexically_normal();
            stage = CredentialMutationStage::InstallKeyFile;
            if (fs::exists(key_file))
                throw ValidationError("managed key file already exists");
            key_file_install_attempted = true;
            platform::linux::filesystem::install_secret_file(new_secret.get(), key_file);
        }
        if (automatic) {
            for (ManagedCredential& credential : metadata)
                credential.automatic = false;
        }
        metadata.push_back({id, label, type, slot, key_file, automatic});
        stage = CredentialMutationStage::SaveMetadata;
        metadata_restore_required = true;
        save_metadata(roots, profile.target.luks_uuid, metadata);
        if (automatic) {
            stage = CredentialMutationStage::PublishProfile;
            profile_restore_required = true;
            set_automatic_key(roots, profile, key_file, activator);
        }
    } catch (...) {
        const std::exception_ptr primary = std::current_exception();
        if (slot < 0) {
            try {
                const auto current = inspect_target(cryptsetup, profile);
                std::vector<int> added;
                std::ranges::set_difference(
                    current.keyslots,
                    before.keyslots,
                    std::back_inserter(added)
                );
                if (added.empty()) {
                    keyslot_added = false;
                    keyslot_state_known = true;
                } else if (added.size() == 1) {
                    slot = added.front();
                    keyslot_added = true;
                    keyslot_state_known = true;
                }
            } catch (...) {
            }
        }

        bool profile_restored = !profile_restore_required;
        if (profile_restore_required) {
            try {
                platform::linux::config::install_profile(
                    profile,
                    {roots.config_root, roots.udev_root, roots.systemd_root, roots.public_root},
                    activator
                );
                profile_restored = true;
            } catch (...) {
                profile_restored = false;
            }
        }

        bool metadata_restored = !metadata_restore_required;
        if (metadata_restore_required) {
            try {
                save_metadata(roots, profile.target.luks_uuid, previous_metadata);
                metadata_restored = true;
            } catch (...) {
                metadata_restored = false;
            }
        }

        std::error_code error;
        if (key_file_install_attempted)
            fs::remove(key_file, error);
        std::error_code exists_error;
        const bool key_file_removed = !key_file_install_attempted ||
            (!fs::exists(key_file, exists_error) && !exists_error && !error);

        bool keyslot_rollback_succeeded = keyslot_state_known && !keyslot_added;
        if (keyslot_added && slot >= 0) {
            try {
                rewind_secret(authorization.get());
                cryptsetup.remove_keyslot(profile.target.device.value(), slot, authorization.get());
                keyslot_rollback_succeeded = true;
            } catch (...) {
                keyslot_rollback_succeeded = false;
            }
        }

        if (!keyslot_rollback_succeeded || !key_file_removed || !metadata_restored ||
            !profile_restored) {
            throw CredentialMutationFailure(
                stage,
                exception_message(primary),
                keyslot_added,
                keyslot_rollback_succeeded,
                key_file_removed,
                metadata_restored,
                profile_restored
            );
        }
        std::rethrow_exception(primary);
    }
}

} // namespace

CredentialMutationFailure::CredentialMutationFailure(
    CredentialMutationStage failed_stage_value,
    std::string primary_error_value,
    bool keyslot_added_value,
    bool keyslot_rollback_succeeded_value,
    bool key_file_removed_value,
    bool metadata_restored_value,
    bool profile_restored_value
)
    : RecoveryRequiredError(
          ErrorCode::CredentialMutationRollbackIncomplete,
          mutation_failure_message(
              failed_stage_value,
              primary_error_value,
              keyslot_added_value,
              keyslot_rollback_succeeded_value,
              key_file_removed_value,
              metadata_restored_value,
              profile_restored_value
          )
      ),
      failed_stage(failed_stage_value),
      primary_error(std::move(primary_error_value)),
      keyslot_added(keyslot_added_value),
      keyslot_rollback_succeeded(keyslot_rollback_succeeded_value),
      key_file_removed(key_file_removed_value),
      metadata_restored(metadata_restored_value),
      profile_restored(profile_restored_value) {
}

SystemCredentialAdministrationBackend::SystemCredentialAdministrationBackend(
    CredentialAdministrationRoots roots,
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    config::IConfigurationActivator& configuration_activator
)
    : roots_(std::move(roots)), cryptsetup_(cryptsetup), configuration_activator_(configuration_activator) {
}

std::vector<TargetCredential> SystemCredentialAdministrationBackend::list_credentials(
    const ProfileId& profile_id
) const {
    const config::Profile profile = load_profile(roots_, profile_id);
    const auto header = inspect_target(cryptsetup_, profile);
    return merge_credentials(header.keyslots, load_metadata(roots_, profile.target.luks_uuid));
}

void SystemCredentialAdministrationBackend::add_passphrase(
    const ProfileId& profile_id,
    int authorization_secret_fd,
    int new_secret_fd,
    const std::string& label
) {
    add_managed_credential(
        roots_,
        cryptsetup_,
        configuration_activator_,
        profile_id,
        authorization_secret_fd,
        platform::linux::filesystem::copy_secret_to_sealed_file(new_secret_fd),
        label,
        "passphrase",
        false
    );
}

void SystemCredentialAdministrationBackend::add_key(
    const ProfileId& profile_id,
    int authorization_secret_fd,
    int key_fd,
    const std::string& label,
    bool automatic
) {
    add_managed_credential(
        roots_,
        cryptsetup_,
        configuration_activator_,
        profile_id,
        authorization_secret_fd,
        platform::linux::filesystem::copy_secret_to_sealed_file(key_fd),
        label,
        "keyFile",
        automatic
    );
}

void SystemCredentialAdministrationBackend::generate_key(
    const ProfileId& profile_id,
    int authorization_secret_fd,
    const std::string& label,
    bool automatic
) {
    add_managed_credential(
        roots_,
        cryptsetup_,
        configuration_activator_,
        profile_id,
        authorization_secret_fd,
        platform::linux::filesystem::generate_random_secret_file(64),
        label,
        "keyFile",
        automatic
    );
}

void SystemCredentialAdministrationBackend::remove_credential(
    const ProfileId& profile_id,
    const std::string& credential_id,
    int authorization_secret_fd
) {
    const config::Profile profile = load_profile(roots_, profile_id);
    auto target_lock = lock_target(roots_, profile);
    static_cast<void>(target_lock);
    const auto header = inspect_target(cryptsetup_, profile);
    if (header.keyslots.size() <= 1)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "the last LUKS credential cannot be removed"
        );
    std::vector<ManagedCredential> metadata = load_metadata(roots_, profile.target.luks_uuid);
    const auto item = std::ranges::find(metadata, credential_id, &ManagedCredential::id);
    if (item == metadata.end())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "managed credential does not exist");
    if (item->automatic)
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "automatic credential cannot be removed"
        );
    ManagedCredential removed = std::move(*item);
    metadata.erase(item);
    save_metadata(roots_, profile.target.luks_uuid, metadata);
    try {
        OwnedFileDescriptor authorization =
            platform::linux::filesystem::copy_secret_to_sealed_file(authorization_secret_fd);
        cryptsetup_.remove_keyslot(profile.target.device.value(), removed.keyslot, authorization.get());
    } catch (...) {
        metadata.push_back(removed);
        save_metadata(roots_, profile.target.luks_uuid, metadata);
        throw;
    }
    if (!removed.key_file.empty()) {
        std::error_code error;
        fs::remove(removed.key_file, error);
        if (error)
            throw ValidationError("credential was removed but its key file could not be deleted");
    }
}

void SystemCredentialAdministrationBackend::register_initial_passphrase(
    const ProfileId& profile_id,
    int keyslot,
    const std::string& label
) {
    const config::Profile profile = load_profile(roots_, profile_id);
    auto target_lock = lock_target(roots_, profile);
    static_cast<void>(target_lock);
    const auto header = inspect_target(cryptsetup_, profile);
    if (!std::ranges::contains(header.keyslots, keyslot))
        throw ValidationError("initial LUKS keyslot does not exist");
    auto metadata = load_metadata(roots_, profile.target.luks_uuid);
    if (std::ranges::find(metadata, keyslot, &ManagedCredential::keyslot) != metadata.end())
        throw ValidationError("initial LUKS keyslot is already registered");
    metadata.push_back({"slot-" + std::to_string(keyslot), label, "passphrase", keyslot, {}, false});
    save_metadata(roots_, profile.target.luks_uuid, metadata);
}

} // namespace btrfsbackup::daemon::control
