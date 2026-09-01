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
#include <sstream>
#include <thread>
#include <unistd.h>

#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/IBtrfsOperations.hpp>
#include <config/json/Json.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <config/wizard/ProfileWizardModel.hpp>
#include <core/Errors.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>
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

Json device_graph_node(const Json& node) {
    Json result{
        {"path", json_string(node, "path")},
        {"type", json_string(node, "type")},
        {"majorMinor", json_string(node, "maj:min")},
        {"kernelName", json_string(node, "kname")},
        {"parentKernelName", json_string(node, "pkname")},
        {"sizeBytes", json_size(node, "size")},
        {"filesystemType", json_string(node, "fstype")},
        {"partitionTableType", json_string(node, "pttype")},
    };
    result["children"] = Json::array();
    if (node.contains("children") && node.at("children").is_array())
        for (const auto& child : node.at("children"))
            result["children"].push_back(device_graph_node(child));
    return result;
}

std::map<std::string, std::string> parse_udev_properties(const std::string& payload) {
    std::map<std::string, std::string> result;
    std::istringstream lines(payload);
    std::string line;
    while (std::getline(lines, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos && separator != 0)
            result.insert_or_assign(line.substr(0, separator), line.substr(separator + 1));
    }
    return result;
}

std::string property(const std::map<std::string, std::string>& properties, const char* key) {
    const auto value = properties.find(key);
    return value == properties.end() ? std::string{} : value->second;
}

bool same_device_identity(const ProvisioningDevice& expected, const ProvisioningDevice& current) {
    return expected.path == current.path && expected.model == current.model &&
        expected.serial == current.serial && expected.transport == current.transport &&
        expected.size_bytes == current.size_bytes && expected.removable == current.removable &&
        expected.mounted == current.mounted && expected.contains_data == current.contains_data &&
        expected.major_minor == current.major_minor && expected.sysfs_devpath == current.sysfs_devpath &&
        expected.wwn == current.wwn && expected.serial_id == current.serial_id &&
        expected.serial_short == current.serial_short && expected.device_graph == current.device_graph;
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
            .candidate_id = {},
            .path = path,
            .model = json_string(node, "model"),
            .serial = json_string(node, "serial"),
            .transport = json_string(node, "tran"),
            .size_bytes = size,
            .removable = json_boolean(node, "rm"),
            .mounted = nonempty_mounts(node),
            .contains_data = contains_data(node),
            .major_minor = json_string(node, "maj:min"),
            .sysfs_devpath = {},
            .wwn = json_string(node, "wwn"),
            .serial_id = {},
            .serial_short = {},
            .device_graph = device_graph_node(node).dump(),
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
        std::vector<ProvisioningDevice> result = parse_devices(backup::capture_command(commands, {"lsblk", "--json", "--tree", "--bytes", "--paths", "--output", "PATH,TYPE,SIZE,MODEL,SERIAL,WWN,TRAN,RM,FSTYPE,PTTYPE,MOUNTPOINTS,MAJ:MIN,KNAME,PKNAME"}));
        for (auto& device : result) {
            const auto properties = parse_udev_properties(backup::capture_command(commands, {"udevadm", "info", "--query=property", "--name", device.path}));
            const std::string udev_major = property(properties, "MAJOR");
            const std::string udev_minor = property(properties, "MINOR");
            const std::string udev_major_minor = udev_major.empty() || udev_minor.empty()
                ? std::string{}
                : udev_major + ":" + udev_minor;
            if (!udev_major_minor.empty() && device.major_minor != udev_major_minor)
                device.major_minor.clear();
            device.sysfs_devpath = property(properties, "DEVPATH");
            device.serial_id = property(properties, "ID_SERIAL");
            device.serial_short = property(properties, "ID_SERIAL_SHORT");
            const std::string udev_wwn = property(properties, "ID_WWN_WITH_EXTENSION").empty()
                ? property(properties, "ID_WWN")
                : property(properties, "ID_WWN_WITH_EXTENSION");
            if (!udev_wwn.empty())
                device.wwn = udev_wwn;
            const std::string udev_transport = property(properties, "ID_BUS");
            if (!udev_transport.empty())
                device.transport = udev_transport;
            if (!device.serial_short.empty())
                device.serial = device.serial_short;
            else if (!device.serial_id.empty())
                device.serial = device.serial_id;
        }
        std::erase_if(result, [](const auto& device) {
            return device.major_minor.empty() || device.sysfs_devpath.empty() || device.transport.empty() ||
                device.device_graph.empty() ||
                (device.wwn.empty() && device.serial_id.empty() && device.serial_short.empty());
        });
        return result;
    }

    ProvisioningDevice revalidate(const ProvisioningDevice& expected) {
        const auto current = devices();
        const auto selected = std::ranges::find(current, expected.path, &ProvisioningDevice::path);
        if (selected == current.end() || !same_device_identity(expected, *selected))
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "selected device identity changed"
            );
        return *selected;
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
        ProvisioningDevice expected_device,
        OwnedFileDescriptor passphrase
    ) {
        std::string mapper;
        try {
            phase(state, "inspect", true);
            const ProvisioningDevice selected = revalidate(expected_device);
            if (selected.mounted)
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

            const ProvisioningDevice before_wipe = revalidate(expected_device);
            if (before_wipe.mounted)
                throw ValidationError("selected device or one of its partitions became mounted");
            phase(state, "wipe-signatures", false);
            backup::ControlledCommandOptions standard;
            require_success(commands, {"wipefs", "--all", "--force", expected_device.path}, standard, "wiping signatures");

            phase(state, "partition", false);
            const std::string table = "label: gpt\n, type=L\n";
            const auto bytes = std::as_bytes(std::span(table.data(), table.size()));
            OwnedFileDescriptor partition_input = platform::linux::filesystem::create_sealed_secret_file(bytes);
            backup::ControlledCommandOptions partition_options;
            partition_options.stdin_fd = partition_input.get();
            require_success(commands, {"sfdisk", "--wipe", "always", expected_device.path}, partition_options, "partitioning device");
            static_cast<void>(commands.run({"udevadm", "settle"}));
            const std::string partition = first_partition(commands, expected_device.path);

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
            answers.target_serial = expected_device.serial_short.empty()
                ? expected_device.serial
                : expected_device.serial_short;
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

SystemDeviceProvisioningBackend::~SystemDeviceProvisioningBackend() noexcept = default;

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
    const ProvisioningDevice& expected_device,
    int passphrase_fd
) {
    OwnedFileDescriptor secret = platform::linux::filesystem::copy_secret_to_sealed_file(passphrase_fd);
    auto state = std::make_shared<State>();
    state->status = {next_operation_id(), request.profile_id, "queued", "inspect", {}, true};
    {
        std::lock_guard lock(impl_->jobs_mutex);
        impl_->jobs.emplace(state->status.operation_id, state);
    }
    state->worker = std::jthread([this, state, request, expected_device, secret = std::move(secret)]() mutable {
        impl_->execute(state, request, expected_device, std::move(secret));
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
