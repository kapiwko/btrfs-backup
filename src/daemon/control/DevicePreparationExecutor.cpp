// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationExecutor.hpp>

#include <chrono>
#include <iostream>
#include <span>
#include <utility>
#include <unistd.h>

#include <backup/ports/IBtrfsOperations.hpp>
#include <backup/ports/ICommandRunner.hpp>
#include <config/json/Json.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>
#include <daemon/control/ProvisioningDeviceEnumerator.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using config::json::Json;
using platform::linux::OwnedFileDescriptor;

std::int64_t system_time_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

std::string json_string(const Json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

void require_success(
    backup::ICommandRunner& commands,
    const std::vector<std::string>& argv,
    const backup::ControlledCommandOptions& options,
    const char* operation
) {
    const auto result = commands.run_controlled(argv, options);
    if (result.exit_code != 0 || result.cancelled || result.timed_out)
        throw ValidationError(std::string(operation) + " failed");
}

std::string descriptor_path(int fd) {
    return "/proc/self/fd/" + std::to_string(fd);
}

void rewind_secret(int fd) {
    if (::lseek(fd, 0, SEEK_SET) < 0)
        throw ValidationError("cannot rewind device preparation secret");
}

std::string first_partition(backup::ICommandRunner& commands, const fs::path& disk) {
    const Json document = Json::parse(backup::capture_command(
        commands,
        {"lsblk", "--json", "--tree", "--paths", "--output", "PATH,TYPE", disk.string()}
    ));
    const auto& devices = document.at("blockdevices");
    if (devices.size() != 1 || !devices.at(0).contains("children"))
        throw ValidationError("partition table was not detected after creation");
    for (const auto& child : devices.at(0).at("children"))
        if (json_string(child, "type") == "part" && !json_string(child, "path").empty())
            return json_string(child, "path");
    throw ValidationError("created partition was not detected");
}

} // namespace

DevicePreparationExecutor::DevicePreparationExecutor(
    CredentialAdministrationRoots roots,
    fs::path target_mount_root,
    backup::ICommandRunner& commands,
    backup::IBtrfsOperations& btrfs,
    config::IConfigurationActivator& configuration_activator,
    ICredentialAdministrationBackend& credentials,
    IDestructiveDeviceSafetyInspector& safety_inspector,
    DevicePreparationTransactionStore& transactions,
    ProvisioningDeviceEnumerator& devices
)
    : roots_(std::move(roots)),
      commands_(commands),
      btrfs_(btrfs),
      activator_(configuration_activator),
      credentials_(credentials),
      safety_inspector_(safety_inspector),
      transactions_(transactions),
      devices_(devices),
      plan_builder_(std::move(target_mount_root)) {
}

void DevicePreparationExecutor::update(
    const std::string& operation_id,
    const TransactionMutator& mutator
) {
    DevicePreparationTransaction transaction = transactions_.load(operation_id);
    mutator(transaction);
    transaction.updated_at = system_time_seconds();
    transactions_.save(transaction);
}

void DevicePreparationExecutor::phase(
    const std::string& operation_id,
    const std::string& value,
    bool can_cancel
) {
    update(operation_id, [&](auto& transaction) {
        transaction.status.state = "running";
        transaction.status.phase = value;
        transaction.status.can_cancel = can_cancel;
    });
}

void DevicePreparationExecutor::completed(const std::string& operation_id, const std::string& value) {
    update(operation_id, [&](auto& transaction) { transaction.last_completed_phase = value; });
}

void DevicePreparationExecutor::execute(const std::string& operation_id, int passphrase_fd) {
    const DevicePreparationTransaction initial = transactions_.load(operation_id);
    if (initial.status.state != "queued" && initial.status.state != "running")
        throw ValidationError("device preparation transaction is not executable");
    OwnedFileDescriptor passphrase = platform::linux::filesystem::copy_secret_to_sealed_file(passphrase_fd);
    std::string mapper;
    try {
        phase(operation_id, "inspect", true);
        const ProvisioningDevice selected = devices_.revalidate(initial.device);
        if (selected.mounted)
            throw ValidationError("selected device or one of its partitions is mounted");
        if (!btrfs_.is_subvolume(initial.source_subvolume))
            throw ValidationError("selected source is not a Btrfs subvolume");
        completed(operation_id, "inspect");
        platform::linux::filesystem::FileLock device_lock(roots_.lock_root / "device-provisioning.lock");
        if (!device_lock.try_acquire())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "another device is being prepared");

        const ProvisioningDevice before_wipe = devices_.revalidate(initial.device);
        if (before_wipe.mounted)
            throw ValidationError("selected device or one of its partitions became mounted");
        const std::vector<std::string> safety_reasons = safety_inspector_.inspect(initial.device);
        if (!safety_reasons.empty())
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "selected device is not safe for destructive preparation: " + safety_reasons.front()
            );
        phase(operation_id, "wipe-signatures", false);
        backup::ControlledCommandOptions standard;
        require_success(
            commands_,
            {"wipefs", "--all", "--force", initial.device.path},
            standard,
            "wiping signatures"
        );
        completed(operation_id, "wipe-signatures");

        phase(operation_id, "partition", false);
        const std::string table = "label: gpt\n, type=L\n";
        const auto bytes = std::as_bytes(std::span(table.data(), table.size()));
        OwnedFileDescriptor partition_input = platform::linux::filesystem::create_sealed_secret_file(bytes);
        backup::ControlledCommandOptions partition_options;
        partition_options.stdin_fd = partition_input.get();
        require_success(
            commands_,
            {"sfdisk", "--wipe", "always", initial.device.path},
            partition_options,
            "partitioning device"
        );
        require_success(commands_, {"udevadm", "settle", "--timeout=10"}, standard, "waiting for the new partition");
        const std::string partition = first_partition(commands_, initial.device.path);
        update(operation_id, [&](auto& transaction) {
            transaction.partition = partition;
            transaction.last_completed_phase = "partition";
        });

        phase(operation_id, "luks-format", false);
        rewind_secret(passphrase.get());
        backup::ControlledCommandOptions secret_options;
        secret_options.inherited_fds = {passphrase.get()};
        require_success(
            commands_,
            {"cryptsetup", "luksFormat", "--type", "luks2", "--batch-mode", "--key-file", descriptor_path(passphrase.get()), partition},
            secret_options,
            "formatting LUKS2"
        );
        const std::string luks_uuid = backup::capture_command(commands_, {"cryptsetup", "luksUUID", partition});
        update(operation_id, [&](auto& transaction) {
            transaction.luks_uuid = luks_uuid;
            transaction.last_completed_phase = "luks-format";
        });

        phase(operation_id, "open", false);
        rewind_secret(passphrase.get());
        mapper = "btrfs-backup-" + initial.status.profile_id;
        require_success(
            commands_,
            {"cryptsetup", "open", "--key-file", descriptor_path(passphrase.get()), partition, mapper},
            secret_options,
            "opening new LUKS target"
        );
        update(operation_id, [&](auto& transaction) {
            transaction.mapper = mapper;
            transaction.last_completed_phase = "open";
            transaction.cleanup_result = "pending";
        });
        const std::string mapper_path = "/dev/mapper/" + mapper;

        phase(operation_id, "mkfs-btrfs", false);
        require_success(
            commands_,
            {"mkfs.btrfs", "--force", "--label", initial.profile_name, mapper_path},
            standard,
            "creating Btrfs filesystem"
        );
        require_success(commands_, {"udevadm", "settle", "--timeout=10"}, standard, "waiting for the new filesystem");
        const std::string btrfs_uuid = backup::capture_command(
            commands_,
            {"blkid", "--output", "value", "--match-tag", "UUID", mapper_path}
        );
        const std::string partition_uuid = backup::capture_command(
            commands_,
            {"blkid", "--output", "value", "--match-tag", "PARTUUID", partition}
        );
        update(operation_id, [&](auto& transaction) {
            transaction.btrfs_uuid = btrfs_uuid;
            transaction.partition_uuid = partition_uuid;
            transaction.last_completed_phase = "mkfs-btrfs";
        });

        phase(operation_id, "close", false);
        require_success(commands_, {"cryptsetup", "close", mapper}, standard, "closing new LUKS target");
        mapper.clear();
        update(operation_id, [](auto& transaction) {
            transaction.mapper.clear();
            transaction.cleanup_result = "mapper-closed";
            transaction.last_completed_phase = "close";
        });

        phase(operation_id, "write-profile", false);
        update(operation_id, [](auto& transaction) { transaction.configuration_state = "in-progress"; });
        config::Profile profile = plan_builder_.build(initial, luks_uuid, btrfs_uuid, partition_uuid);
        platform::linux::config::install_profile(
            profile,
            {roots_.config_root, roots_.udev_root, roots_.systemd_root, roots_.public_root},
            activator_
        );
        update(operation_id, [](auto& transaction) {
            transaction.configuration_state = "installed";
            transaction.last_completed_phase = "write-profile";
            transaction.credentials_state = "in-progress";
        });
        credentials_.register_initial_passphrase(ProfileId{initial.status.profile_id}, 0, initial.passphrase_label);
        if (initial.create_automatic_key) {
            rewind_secret(passphrase.get());
            credentials_.generate_key(
                ProfileId{initial.status.profile_id},
                passphrase.get(),
                "Automatic backup key",
                true
            );
        }
        update(operation_id, [](auto& transaction) {
            transaction.credentials_state = "installed";
            transaction.last_completed_phase = "credentials";
            transaction.status.state = "succeeded";
            transaction.status.phase = "complete";
            transaction.status.can_cancel = false;
            transaction.status.recovery_action.clear();
        });
    } catch (const std::exception& error) {
        const bool cleanup_required = !mapper.empty();
        bool cleanup_ok = !cleanup_required;
        try {
            cleanup_ok = !cleanup_required || commands_.run({"cryptsetup", "close", mapper}).exit_code == 0;
        } catch (...) {
            cleanup_ok = false;
        }
        try {
            update(operation_id, [&](auto& transaction) {
                std::cerr << "Device preparation " << operation_id << " failed during "
                          << transaction.status.phase << ": " << error.what() << '\n';
                transaction.status.state = "failed";
                transaction.status.error_code = "device-preparation." + transaction.status.phase + "-failed";
                transaction.status.recovery_action =
                    "Inspect the recorded device artifacts and complete or remove partial structures manually.";
                transaction.status.can_cancel = false;
                transaction.cleanup_result = !cleanup_required
                    ? "not-required"
                    : (cleanup_ok ? "mapper-closed" : "mapper-close-failed");
                if (cleanup_ok)
                    transaction.mapper.clear();
            });
        } catch (const std::exception& persistence_error) {
            std::cerr << "Cannot persist failed device preparation: " << persistence_error.what() << '\n';
        }
    } catch (...) {
        const bool cleanup_required = !mapper.empty();
        bool cleanup_ok = !cleanup_required;
        try {
            cleanup_ok = !cleanup_required || commands_.run({"cryptsetup", "close", mapper}).exit_code == 0;
        } catch (...) {
            cleanup_ok = false;
        }
        try {
            update(operation_id, [&](auto& transaction) {
                transaction.status.state = "failed";
                transaction.status.error_code = "device-preparation.unknown-failed";
                transaction.status.recovery_action = "Inspect and repair the recorded device artifacts manually.";
                transaction.status.can_cancel = false;
                transaction.cleanup_result = !cleanup_required
                    ? "not-required"
                    : (cleanup_ok ? "mapper-closed" : "mapper-close-failed");
                if (cleanup_ok)
                    transaction.mapper.clear();
            });
        } catch (...) {
        }
    }
}

void DevicePreparationExecutor::recover(const std::string& operation_id) {
    DevicePreparationTransaction transaction = transactions_.load(operation_id);
    if (transaction.status.state != "interrupted")
        throw ValidationError("device preparation transaction does not require cleanup");
    if (transaction.mapper.empty())
        return;
    try {
        const auto result = commands_.run({"cryptsetup", "close", transaction.mapper});
        transaction.cleanup_result = result.exit_code == 0 ? "mapper-closed" : "mapper-close-failed";
        if (result.exit_code == 0)
            transaction.mapper.clear();
    } catch (...) {
        transaction.cleanup_result = "mapper-close-failed";
    }
    transaction.updated_at = system_time_seconds();
    transactions_.save(transaction);
}

} // namespace btrfsbackup::daemon::control
