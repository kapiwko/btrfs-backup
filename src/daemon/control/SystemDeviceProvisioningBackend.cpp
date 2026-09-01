// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/SystemDeviceProvisioningBackend.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <ranges>
#include <span>
#include <thread>
#include <unistd.h>

#include <config/json/Json.hpp>
#include <config/wizard/ProfileWizardModel.hpp>
#include <core/Errors.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/storage/MountInfo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {

namespace {

using config::json::Json;
using platform::linux::OwnedFileDescriptor;

std::string json_string(const Json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

std::uint64_t json_size(const Json& object, const char* key) {
    const auto value = object.find(key);
    if (value == object.end())
        return 0;
    if (value->is_number_unsigned())
        return value->get<std::uint64_t>();
    if (value->is_number_integer()) {
        const auto number = value->get<std::int64_t>();
        return number > 0 ? static_cast<std::uint64_t>(number) : 0;
    }
    return 0;
}

bool json_boolean(const Json& object, const char* key) {
    const auto value = object.find(key);
    if (value == object.end())
        return false;
    if (value->is_boolean())
        return value->get<bool>();
    return value->is_number_integer() && value->get<int>() != 0;
}

std::string next_operation_id() {
    static std::atomic<unsigned long long> sequence{0};
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch()
    )
                         .count();
    return "prepare-" + std::to_string(now) + "-" + std::to_string(sequence.fetch_add(1));
}

bool nonempty_mounts(const Json& node) {
    if (node.contains("mountpoints") && node.at("mountpoints").is_array()) {
        for (const auto& mount : node.at("mountpoints"))
            if (mount.is_string() && !mount.get<std::string>().empty())
                return true;
    }
    if (node.contains("children") && node.at("children").is_array())
        return std::ranges::any_of(node.at("children"), nonempty_mounts);
    return false;
}

bool contains_data(const Json& node) {
    if (!json_string(node, "fstype").empty() || !json_string(node, "pttype").empty())
        return true;
    return node.contains("children") && node.at("children").is_array() && !node.at("children").empty();
}

std::vector<ProvisioningDevice> parse_devices(const std::string& payload) {
    const Json document = Json::parse(payload);
    if (!document.is_object() || !document.contains("blockdevices") || !document.at("blockdevices").is_array())
        throw ValidationError("lsblk returned invalid device data");
    std::vector<ProvisioningDevice> result;
    for (const Json& node : document.at("blockdevices")) {
        if (!node.is_object() || json_string(node, "type") != "disk")
            continue;
        const std::string path = json_string(node, "path");
        const std::uint64_t size = json_size(node, "size");
        if (path.empty() || !fs::path(path).is_absolute() || size == 0)
            continue;
        result.push_back({
            .path = path,
            .model = json_string(node, "model"),
            .serial = json_string(node, "serial"),
            .transport = json_string(node, "tran"),
            .size_bytes = size,
            .removable = json_boolean(node, "rm"),
            .mounted = nonempty_mounts(node),
            .contains_data = contains_data(node),
        });
    }
    return result;
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
    const Json document = Json::parse(backup::capture_command(commands, {"lsblk", "--json", "--paths", "--output", "PATH,TYPE", disk.string()}));
    const auto& devices = document.at("blockdevices");
    if (devices.size() != 1 || !devices.at(0).contains("children"))
        throw ValidationError("partition table was not detected after creation");
    for (const auto& child : devices.at(0).at("children"))
        if (json_string(child, "type") == "part" && !json_string(child, "path").empty())
            return json_string(child, "path");
    throw ValidationError("created partition was not detected");
}

} // namespace

struct SystemDeviceProvisioningBackend::State {
    mutable std::mutex mutex;
    DevicePreparationStatus status;
    bool cancel_requested = false;
    std::jthread worker;
};

struct SystemDeviceProvisioningBackend::Impl {
    CredentialAdministrationRoots roots;
    fs::path target_mount_root;
    fs::path mountinfo_path;
    backup::ICommandRunner& commands;
    backup::IBtrfsOperations& btrfs;
    config::IConfigurationActivator& activator;
    ICredentialAdministrationBackend& credentials;
    mutable std::mutex jobs_mutex;
    std::map<std::string, std::shared_ptr<State>> jobs;

    Impl(
        CredentialAdministrationRoots roots_value,
        fs::path mount_root,
        fs::path mountinfo,
        backup::ICommandRunner& command_runner,
        backup::IBtrfsOperations& btrfs_operations,
        config::IConfigurationActivator& configuration_activator,
        ICredentialAdministrationBackend& credential_backend
    ) : roots(std::move(roots_value)), target_mount_root(std::move(mount_root)),
        mountinfo_path(std::move(mountinfo)), commands(command_runner), btrfs(btrfs_operations),
        activator(configuration_activator), credentials(credential_backend) {
    }

    std::vector<ProvisioningDevice> devices() {
        return parse_devices(backup::capture_command(commands, {"lsblk", "--json", "--bytes", "--paths", "--output", "PATH,TYPE,SIZE,MODEL,SERIAL,TRAN,RM,FSTYPE,PTTYPE,MOUNTPOINTS"}));
    }

    void phase(const std::shared_ptr<State>& state, const std::string& value, bool can_cancel) {
        std::lock_guard lock(state->mutex);
        state->status.state = "running";
        state->status.phase = value;
        state->status.can_cancel = can_cancel;
    }

    bool cancelled(const std::shared_ptr<State>& state) {
        std::lock_guard lock(state->mutex);
        return state->cancel_requested;
    }

    void execute(
        const std::shared_ptr<State>& state,
        DevicePreparationRequest request,
        OwnedFileDescriptor passphrase
    ) {
        std::string mapper;
        try {
            phase(state, "inspect", true);
            const auto current = devices();
            const auto selected = std::ranges::find(current, request.device_path, &ProvisioningDevice::path);
            if (selected == current.end() || selected->size_bytes != request.expected_size_bytes ||
                selected->serial != request.expected_serial)
                throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "selected device identity changed");
            if (selected->mounted)
                throw ValidationError("selected device or one of its partitions is mounted");
            if (!btrfs.is_subvolume(request.source_subvolume))
                throw ValidationError("selected source is not a Btrfs subvolume");
            if (cancelled(state)) {
                std::lock_guard lock(state->mutex);
                state->status.state = "cancelled";
                state->status.phase = "cancelled";
                state->status.can_cancel = false;
                return;
            }

            platform::linux::filesystem::FileLock device_lock(roots.lock_root / "device-provisioning.lock");
            if (!device_lock.try_acquire())
                throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "another device is being prepared");

            phase(state, "wipe-signatures", false);
            backup::ControlledCommandOptions standard;
            require_success(commands, {"wipefs", "--all", "--force", request.device_path}, standard, "wiping signatures");

            phase(state, "partition", false);
            const std::string table = "label: gpt\n, type=L\n";
            const auto bytes = std::as_bytes(std::span(table.data(), table.size()));
            OwnedFileDescriptor partition_input = platform::linux::filesystem::create_sealed_secret_file(bytes);
            backup::ControlledCommandOptions partition_options;
            partition_options.stdin_fd = partition_input.get();
            require_success(commands, {"sfdisk", "--wipe", "always", request.device_path}, partition_options, "partitioning device");
            static_cast<void>(commands.run({"udevadm", "settle"}));
            const std::string partition = first_partition(commands, request.device_path);

            phase(state, "luks-format", false);
            rewind_secret(passphrase.get());
            backup::ControlledCommandOptions secret_options;
            secret_options.inherited_fds = {passphrase.get()};
            require_success(
                commands,
                {"cryptsetup", "luksFormat", "--type", "luks2", "--batch-mode", "--key-file", descriptor_path(passphrase.get()), partition},
                secret_options,
                "formatting LUKS2"
            );
            const std::string luks_uuid = backup::capture_command(commands, {"cryptsetup", "luksUUID", partition});

            phase(state, "open", false);
            rewind_secret(passphrase.get());
            mapper = "btrfs-backup-" + request.profile_id;
            require_success(
                commands,
                {"cryptsetup", "open", "--key-file", descriptor_path(passphrase.get()), partition, mapper},
                secret_options,
                "opening new LUKS target"
            );
            const std::string mapper_path = "/dev/mapper/" + mapper;

            phase(state, "mkfs-btrfs", false);
            require_success(commands, {"mkfs.btrfs", "--force", "--label", request.profile_name, mapper_path}, standard, "creating Btrfs filesystem");
            static_cast<void>(commands.run({"udevadm", "settle"}));
            const std::string btrfs_uuid = backup::capture_command(commands, {"blkid", "--output", "value", "--match-tag", "UUID", mapper_path});
            const std::string partition_uuid = backup::capture_command(commands, {"blkid", "--output", "value", "--match-tag", "PARTUUID", partition});

            phase(state, "close", false);
            require_success(commands, {"cryptsetup", "close", mapper}, standard, "closing new LUKS target");
            mapper.clear();

            phase(state, "write-profile", false);
            config::wizard::ProfileWizardAnswers answers;
            answers.profile_id = request.profile_id;
            answers.profile_name = request.profile_name;
            answers.target_device = "/dev/disk/by-uuid/" + luks_uuid;
            answers.target_luks_uuid = luks_uuid;
            answers.target_btrfs_uuid = btrfs_uuid;
            answers.target_partition_uuid = partition_uuid;
            answers.target_serial = request.expected_serial;
            answers.target_mapper_name = "backupdisk-" + request.profile_id;
            answers.target_mount_root = target_mount_root.string();
            answers.keyfile = "none";
            answers.sources.push_back({
                .id = "source",
                .subvolume = request.source_subvolume,
                .local_snapshot_dir = "/.snapshots/btrfs-backup/" + request.profile_id,
                .remote_subdir = "source",
            });
            config::Profile profile = config::wizard::profile_from_wizard_answers(answers);
            profile.enabled = request.create_automatic_key;
            platform::linux::config::install_profile(
                profile,
                {roots.config_root, roots.udev_root, roots.systemd_root, roots.public_root},
                activator
            );
            credentials.register_initial_passphrase(ProfileId{request.profile_id}, 0, request.passphrase_label);
            if (request.create_automatic_key) {
                rewind_secret(passphrase.get());
                credentials.generate_key(ProfileId{request.profile_id}, passphrase.get(), "Automatic backup key", true);
            }

            std::lock_guard lock(state->mutex);
            state->status.state = "succeeded";
            state->status.phase = "complete";
            state->status.can_cancel = false;
        } catch (const std::exception& error) {
            if (!mapper.empty())
                static_cast<void>(commands.run({"cryptsetup", "close", mapper}));
            std::lock_guard lock(state->mutex);
            std::cerr << "Device preparation " << state->status.operation_id << " failed during "
                      << state->status.phase << ": " << error.what() << '\n';
            state->status.state = "failed";
            state->status.error_code = "device-preparation." + state->status.phase + "-failed";
            state->status.can_cancel = false;
        } catch (...) {
            if (!mapper.empty())
                static_cast<void>(commands.run({"cryptsetup", "close", mapper}));
            std::lock_guard lock(state->mutex);
            std::cerr << "Device preparation " << state->status.operation_id << " failed during "
                      << state->status.phase << " with an unknown error\n";
            state->status.state = "failed";
            state->status.error_code = "device-preparation." + state->status.phase + "-failed";
            state->status.can_cancel = false;
        }
    }
};

SystemDeviceProvisioningBackend::SystemDeviceProvisioningBackend(
    CredentialAdministrationRoots roots,
    fs::path target_mount_root,
    fs::path mountinfo_path,
    backup::ICommandRunner& commands,
    backup::IBtrfsOperations& btrfs,
    config::IConfigurationActivator& configuration_activator,
    ICredentialAdministrationBackend& credentials
) : impl_(std::make_unique<Impl>(std::move(roots), std::move(target_mount_root), std::move(mountinfo_path), commands, btrfs, configuration_activator, credentials)) {
}

SystemDeviceProvisioningBackend::~SystemDeviceProvisioningBackend() = default;

std::vector<ProvisioningDevice> SystemDeviceProvisioningBackend::list_devices() {
    return impl_->devices();
}

std::vector<std::string> SystemDeviceProvisioningBackend::list_source_candidates() {
    const auto paths = platform::linux::storage::btrfs_mount_targets(impl_->mountinfo_path);
    std::vector<std::string> result;
    result.reserve(paths.size());
    for (const auto& path : paths)
        result.push_back(path);
    return result;
}

DevicePreparationStatus SystemDeviceProvisioningBackend::start(
    const DevicePreparationRequest& request,
    int passphrase_fd
) {
    OwnedFileDescriptor secret = platform::linux::filesystem::copy_secret_to_sealed_file(passphrase_fd);
    auto state = std::make_shared<State>();
    state->status = {next_operation_id(), request.profile_id, "queued", "inspect", {}, true};
    {
        std::lock_guard lock(impl_->jobs_mutex);
        impl_->jobs.emplace(state->status.operation_id, state);
    }
    state->worker = std::jthread([this, state, request, secret = std::move(secret)]() mutable {
        impl_->execute(state, request, std::move(secret));
    });
    return status(state->status.operation_id);
}

DevicePreparationStatus SystemDeviceProvisioningBackend::status(const std::string& operation_id) const {
    std::shared_ptr<State> state;
    {
        std::lock_guard lock(impl_->jobs_mutex);
        const auto item = impl_->jobs.find(operation_id);
        if (item == impl_->jobs.end())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "device preparation operation not found");
        state = item->second;
    }
    std::lock_guard lock(state->mutex);
    return state->status;
}

void SystemDeviceProvisioningBackend::cancel(const std::string& operation_id) {
    std::shared_ptr<State> state;
    {
        std::lock_guard lock(impl_->jobs_mutex);
        const auto item = impl_->jobs.find(operation_id);
        if (item == impl_->jobs.end())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "device preparation operation not found");
        state = item->second;
    }
    std::lock_guard lock(state->mutex);
    if (!state->status.can_cancel)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "device preparation can no longer be cancelled");
    state->cancel_requested = true;
}

} // namespace btrfsbackup::daemon::control
