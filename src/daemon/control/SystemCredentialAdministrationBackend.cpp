// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemCredentialAdministrationBackend.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <sys/random.h>
#include <unistd.h>
#include <utility>

#include <config/json/JsonIo.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <config/domain/Validation.hpp>
#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/filesystem/TrustedDirectory.hpp>
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
        const fs::path normalized_key_root = config::normalized_path(roots.key_root);
        bool trusted_key_file = true;
        if (credential.type == "keyFile") {
            try {
                static_cast<void>(platform::linux::filesystem::SafeFilename(
                    credential.key_file.filename().string()
                ));
                trusted_key_file = credential.key_file.parent_path() == normalized_key_root;
            } catch (const ValidationError&) {
                trusted_key_file = false;
            }
        }
        if (credential.id != "slot-" + std::to_string(credential.keyslot) || credential.label.empty() ||
            (credential.type != "passphrase" && credential.type != "keyFile") ||
            (credential.type == "keyFile" &&
             (!credential.key_file.is_absolute() || credential.key_file.lexically_normal() != credential.key_file ||
              !trusted_key_file)) ||
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

std::string_view removal_stage_name(CredentialRemovalStage stage) {
    switch (stage) {
    case CredentialRemovalStage::QuarantineKeyFile:
        return "quarantine-key-file";
    case CredentialRemovalStage::RemoveKeyslot:
        return "remove-keyslot";
    case CredentialRemovalStage::CommitMetadata:
        return "commit-metadata";
    }
    return "unknown";
}

std::string removal_failure_message(
    CredentialRemovalStage stage,
    const std::string& primary_error,
    bool key_file_quarantined,
    bool key_file_restored,
    bool keyslot_state_known,
    bool keyslot_removed,
    bool metadata_committed
) {
    std::ostringstream message;
    message << "credential removal failed during " << removal_stage_name(stage) << ": " << primary_error
            << "; recovery required"
            << "; keyFileQuarantined=" << key_file_quarantined
            << "; keyFileRestored=" << key_file_restored
            << "; keyslotStateKnown=" << keyslot_state_known
            << "; keyslotRemoved=" << keyslot_removed
            << "; metadataCommitted=" << metadata_committed;
    return message.str();
}

std::string random_quarantine_suffix() {
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
        throw ValidationError("cannot generate a credential quarantine identifier");
    }
    std::ostringstream value;
    value << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        value << std::setw(2) << static_cast<unsigned>(byte);
    return value.str();
}

platform::linux::filesystem::SafeFilename quarantine_filename(
    const platform::linux::filesystem::SafeFilename& key_filename
) {
    return platform::linux::filesystem::SafeFilename(
        std::string(key_filename.value()) + "." + random_quarantine_suffix() + ".removed"
    );
}

bool restore_quarantined_key(
    const platform::linux::filesystem::TrustedDirectory& quarantine,
    const platform::linux::filesystem::SafeFilename& quarantined_filename,
    const platform::linux::filesystem::TrustedDirectory& key_directory,
    const platform::linux::filesystem::SafeFilename& key_filename
) noexcept {
    try {
        quarantine.rename_noreplace(quarantined_filename, key_directory, key_filename);
        quarantine.sync();
        key_directory.sync();
        return true;
    } catch (...) {
        return false;
    }
}

void cleanup_quarantined_key(
    const platform::linux::filesystem::TrustedDirectory& quarantine,
    const platform::linux::filesystem::SafeFilename& filename
) noexcept {
    const std::error_code error = quarantine.remove(filename);
    if (!error) {
        try {
            quarantine.sync();
            return;
        } catch (const std::exception& exception) {
            std::cerr << "btrfs-backupd: credential removal succeeded but quarantine cleanup could not be "
                         "synchronized: "
                      << exception.what() << '\n';
            return;
        }
    }
    std::cerr << "btrfs-backupd: credential removal succeeded but quarantined key file could not be deleted: "
              << quarantine.path() / filename.value() << ": " << error.message() << '\n';
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
    std::optional<platform::linux::filesystem::TrustedDirectory> key_directory;
    std::optional<platform::linux::filesystem::SafeFilename> key_filename;

    CredentialMutationStage stage = CredentialMutationStage::AddKeyslot;
    bool keyslot_added = false;
    bool keyslot_state_known = false;
    int slot = -1;
    bool key_file_installed = false;
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
            platform::linux::filesystem::ensure_trusted_directory(
                roots.key_root,
                0700,
                roots.config_root,
                roots.trusted_owner
            );
            key_directory.emplace(
                roots.key_root,
                roots.config_root,
                roots.trusted_owner
            );
            key_filename.emplace(std::string(profile.id.value()) + "-" + id + ".key");
            key_file = key_directory->path() / key_filename->value();
            stage = CredentialMutationStage::InstallKeyFile;
            platform::linux::filesystem::install_secret_file(
                *key_directory,
                *key_filename,
                new_secret.get()
            );
            key_file_installed = true;
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

        bool key_file_removed = !key_file_installed;
        if (key_file_installed && key_directory && key_filename) {
            key_file_removed = !key_directory->remove(*key_filename);
            if (key_file_removed) {
                try {
                    key_directory->sync();
                } catch (...) {
                    key_file_removed = false;
                }
            }
        }

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

CredentialRemovalFailure::CredentialRemovalFailure(
    CredentialRemovalStage failed_stage_value,
    std::string primary_error_value,
    bool key_file_quarantined_value,
    bool key_file_restored_value,
    bool keyslot_state_known_value,
    bool keyslot_removed_value,
    bool metadata_committed_value
)
    : RecoveryRequiredError(
          ErrorCode::CredentialMutationRollbackIncomplete,
          removal_failure_message(
              failed_stage_value,
              primary_error_value,
              key_file_quarantined_value,
              key_file_restored_value,
              keyslot_state_known_value,
              keyslot_removed_value,
              metadata_committed_value
          )
      ),
      failed_stage(failed_stage_value),
      primary_error(std::move(primary_error_value)),
      key_file_quarantined(key_file_quarantined_value),
      key_file_restored(key_file_restored_value),
      keyslot_state_known(keyslot_state_known_value),
      keyslot_removed(keyslot_removed_value),
      metadata_committed(metadata_committed_value) {
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
    if (!std::ranges::contains(header.keyslots, item->keyslot))
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::Conflict,
            "managed credential keyslot does not exist"
        );
    ManagedCredential removed = std::move(*item);
    metadata.erase(item);

    std::optional<platform::linux::filesystem::TrustedDirectory> key_directory;
    std::optional<platform::linux::filesystem::TrustedDirectory> quarantine_directory;
    std::optional<platform::linux::filesystem::SafeFilename> key_filename;
    std::optional<platform::linux::filesystem::SafeFilename> quarantined_filename;
    bool key_file_quarantined = false;
    bool key_file_restored = removed.key_file.empty();
    bool keyslot_state_known = true;
    bool keyslot_removed = false;
    bool metadata_committed = false;
    CredentialRemovalStage stage = CredentialRemovalStage::QuarantineKeyFile;
    try {
        if (!removed.key_file.empty()) {
            key_directory.emplace(
                roots_.key_root,
                roots_.config_root,
                roots_.trusted_owner
            );
            key_filename.emplace(std::string(profile.id.value()) + "-" + removed.id + ".key");
            const fs::path expected_key_file = key_directory->path() / key_filename->value();
            if (removed.key_file != expected_key_file)
                throw ValidationError("managed credential key file is outside its trusted location");
            const fs::path quarantine_path = roots_.key_root / ".quarantine";
            platform::linux::filesystem::ensure_trusted_directory(
                quarantine_path,
                0700,
                roots_.key_root,
                roots_.trusted_owner
            );
            quarantine_directory.emplace(
                quarantine_path,
                roots_.key_root,
                roots_.trusted_owner
            );
            quarantined_filename.emplace(quarantine_filename(*key_filename));
            key_directory->rename_noreplace(
                *key_filename,
                *quarantine_directory,
                *quarantined_filename
            );
            key_file_quarantined = true;
            key_directory->sync();
            quarantine_directory->sync();
        }

        stage = CredentialRemovalStage::RemoveKeyslot;
        OwnedFileDescriptor authorization =
            platform::linux::filesystem::copy_secret_to_sealed_file(authorization_secret_fd);
        cryptsetup_.remove_keyslot(profile.target.device.value(), removed.keyslot, authorization.get());
        keyslot_removed = true;

        stage = CredentialRemovalStage::CommitMetadata;
        save_metadata(roots_, profile.target.luks_uuid, metadata);
        metadata_committed = true;
    } catch (...) {
        const std::exception_ptr primary = std::current_exception();
        if (stage == CredentialRemovalStage::RemoveKeyslot) {
            try {
                const auto current = inspect_target(cryptsetup_, profile);
                keyslot_removed = !std::ranges::contains(current.keyslots, removed.keyslot);
                keyslot_state_known = true;
            } catch (...) {
                keyslot_state_known = false;
            }
        }

        if (!keyslot_removed && key_file_quarantined) {
            key_file_restored = restore_quarantined_key(
                *quarantine_directory,
                *quarantined_filename,
                *key_directory,
                *key_filename
            );
        }

        const bool complete_rollback = keyslot_state_known && !keyslot_removed && key_file_restored;
        if (complete_rollback)
            std::rethrow_exception(primary);

        throw CredentialRemovalFailure(
            stage,
            exception_message(primary),
            key_file_quarantined,
            key_file_restored,
            keyslot_state_known,
            keyslot_removed,
            metadata_committed
        );
    }

    if (key_file_quarantined)
        cleanup_quarantined_key(*quarantine_directory, *quarantined_filename);
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
