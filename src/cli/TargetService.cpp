// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/TargetService.hpp>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <backup/BackupPreflightValidation.hpp>
#include <core/Errors.hpp>
#include <config/model/JsonIo.hpp>
#include <config/model/Profile.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <config/model/Validation.hpp>
#include <platform/linux/process/PosixCommandRunner.hpp>
#include <platform/linux/storage/DeviceInfo.hpp>
#include <platform/linux/filesystem/FileIo.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/storage/MountInfo.hpp>
#include <platform/linux/process/Process.hpp>
#include <platform/linux/filesystem/TrustedDirectory.hpp>
#include <platform/linux/SystemdUnit.hpp>
#include <platform/linux/filesystem/TrustedFile.hpp>

namespace fs = std::filesystem;

namespace {

#ifndef BTRFSBACKUP_SYSTEMD_CRYPTSETUP
#error "BTRFSBACKUP_SYSTEMD_CRYPTSETUP must be defined by the build system"
#endif

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool rootless_tests_allowed() {
    const char* value = std::getenv("BTRFS_BACKUP_ALLOW_ROOTLESS_TESTS");
    return value != nullptr && std::string(value) == "true";
}

void require_root() {
    if (geteuid() != 0 && !rootless_tests_allowed()) {
        throw btrfsbackup::ValidationError("target operations require root privileges");
    }
}

void run_checked(
    btrfsbackup::backup::ICommandRunner& commands,
    const std::vector<std::string>& argv,
    const std::string& message
) {
    if (commands.run(argv).exit_code != 0) {
        throw btrfsbackup::ValidationError(message);
    }
}

void run_checked_controlled(
    btrfsbackup::backup::ICommandRunner& commands,
    const std::vector<std::string>& argv,
    const std::string& message,
    std::chrono::milliseconds timeout
) {
    btrfsbackup::backup::ControlledCommandOptions options;
    options.timeout = timeout;
    btrfsbackup::backup::CommandResult result = commands.run_controlled(argv, options);
    if (result.exit_code != 0 || result.timed_out || result.cancelled) {
        throw btrfsbackup::ValidationError(message);
    }
}

void run_ignored(btrfsbackup::backup::ICommandRunner& commands, const std::vector<std::string>& argv) {
    (void)commands.run(argv);
}

void validate_luks_uuid(btrfsbackup::backup::ICommandRunner& commands, const btrfsbackup::config::Profile& profile) {
    std::string actual = btrfsbackup::backup::capture_command(
        commands,
        {"cryptsetup", "luksUUID", profile.target.device.value().string()}
    );
    if (actual.empty() || lower(actual) != profile.target.luks_uuid.value()) {
        throw btrfsbackup::ValidationError("LUKS UUID mismatch for " + profile.target.device.value().string());
    }
}

std::string mapper_underlying_device(btrfsbackup::backup::ICommandRunner& commands, const std::string& mapper_name) {
    std::string status = btrfsbackup::backup::capture_command(commands, {"cryptsetup", "status", mapper_name});
    const std::string marker = "device:";
    std::size_t pos = status.find(marker);
    if (pos == std::string::npos) {
        return {};
    }
    pos += marker.size();
    while (pos < status.size() && std::isspace(static_cast<unsigned char>(status[pos]))) {
        ++pos;
    }
    std::size_t end = pos;
    while (end < status.size() && !std::isspace(static_cast<unsigned char>(status[end]))) {
        ++end;
    }
    return status.substr(pos, end - pos);
}

bool mapper_identity_matches(
    btrfsbackup::backup::ICommandRunner& commands,
    const btrfsbackup::config::Profile& profile,
    const std::function<fs::path(const fs::path&)>& canonical_device
) {
    fs::path configured = canonical_device(profile.target.device);
    fs::path actual = canonical_device(
        mapper_underlying_device(commands, profile.target.mapper_name.value())
    );
    if (configured.empty() || actual.empty() || configured != actual) {
        return false;
    }
    validate_luks_uuid(commands, profile);
    return true;
}

bool mapper_has_mounts(
    const btrfsbackup::config::Profile& profile,
    const std::vector<btrfsbackup::backup::MountEntry>& mounts,
    std::vector<btrfsbackup::cli::TargetEvent>& events,
    const fs::path& mapper_root = "/dev/mapper"
) {
    fs::path mapper = mapper_root / profile.target.mapper_name.value();
    for (const btrfsbackup::backup::MountEntry& mount : mounts) {
        if (btrfsbackup::config::normalized_path(btrfsbackup::platform::linux::storage::strip_subvolume_suffix(mount.source)) == btrfsbackup::config::normalized_path(mapper)) {
            events.push_back({
                .kind = btrfsbackup::cli::TargetEventKind::MapperStillMounted,
                .detail = mount.target,
            });
            return true;
        }
    }
    return false;
}

struct ResolvedDependencies {
    btrfsbackup::backup::ICommandRunner& commands;
    std::function<std::vector<btrfsbackup::backup::MountEntry>()> read_mounts;
    fs::path lock_root;
    fs::path mount_point_trust_root;
    fs::path mapper_root;
    fs::path activation_state_root;
    fs::path keyfile_trust_root;
    std::string systemd_cryptsetup_command;
    std::function<fs::path(const fs::path&)> canonical_device;
};

ResolvedDependencies resolve_dependencies(btrfsbackup::cli::TargetServiceDependencies& dependencies) {
    return {
        .commands = dependencies.commands,
        .read_mounts = !dependencies.read_mounts
            ? std::function<std::vector<btrfsbackup::backup::MountEntry>()>([] { return btrfsbackup::platform::linux::storage::read_mount_table(); })
            : dependencies.read_mounts,
        .lock_root = dependencies.lock_root.empty()
            ? btrfsbackup::platform::linux::filesystem::default_lock_root()
            : dependencies.lock_root,
        .mount_point_trust_root = dependencies.mount_point_trust_root.empty()
            ? fs::path("/")
            : dependencies.mount_point_trust_root,
        .mapper_root = dependencies.mapper_root.empty()
            ? fs::path("/dev/mapper")
            : dependencies.mapper_root,
        .activation_state_root = dependencies.activation_state_root.empty()
            ? fs::path("/run/btrfs-backup/target-activation")
            : dependencies.activation_state_root,
        .keyfile_trust_root = dependencies.keyfile_trust_root.empty()
            ? fs::path("/")
            : dependencies.keyfile_trust_root,
        .systemd_cryptsetup_command = dependencies.systemd_cryptsetup_command.empty()
            ? std::string(BTRFSBACKUP_SYSTEMD_CRYPTSETUP)
            : dependencies.systemd_cryptsetup_command,
        .canonical_device = !dependencies.canonical_device
            ? std::function<fs::path(const fs::path&)>(btrfsbackup::platform::linux::storage::canonical_device)
            : dependencies.canonical_device,
    };
}

btrfsbackup::cli::TargetServiceDependencies production_dependencies(
    btrfsbackup::backup::ICommandRunner& commands
) {
    return {
        .commands = commands,
        .read_mounts = {},
        .lock_root = {},
        .mount_point_trust_root = {},
        .mapper_root = {},
        .activation_state_root = {},
        .keyfile_trust_root = {},
        .systemd_cryptsetup_command = {},
        .canonical_device = {},
    };
}

fs::path activation_marker_path(const ResolvedDependencies& resolved, const btrfsbackup::config::Profile& profile) {
    return resolved.activation_state_root / (std::string(profile.id.value()) + ".json");
}

btrfsbackup::platform::linux::filesystem::FileLock acquire_activation_lock(
    const ResolvedDependencies& resolved,
    const btrfsbackup::config::Profile& profile
) {
    btrfsbackup::platform::linux::filesystem::FileLock lock(
        btrfsbackup::platform::linux::filesystem::target_lock_path(
            resolved.activation_state_root / ".locks",
            profile.target.luks_uuid
        )
    );
    if (!lock.try_acquire()) {
        throw btrfsbackup::ValidationError("target activation is already in progress");
    }
    return lock;
}

bool activation_is_owned(
    const ResolvedDependencies& resolved,
    const btrfsbackup::config::Profile& profile
) {
    const fs::path marker_path = activation_marker_path(resolved, profile);
    std::error_code error;
    const fs::file_status status = fs::symlink_status(marker_path, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !fs::exists(status))) {
        return false;
    }
    if (error || fs::is_symlink(status) || !fs::is_regular_file(status)) {
        throw btrfsbackup::ValidationError("invalid target activation marker: " + marker_path.string());
    }
    try {
        const btrfsbackup::config::Json marker = btrfsbackup::config::Json::parse(
            btrfsbackup::platform::linux::filesystem::read_trusted_config_file(
                marker_path,
                {.allow_current_user_owner = rootless_tests_allowed()}
            )
        );
        if (!marker.is_object() || marker.size() != 5 || marker.value("schemaVersion", 0) != 1 ||
            marker.value("profileId", "") != profile.id.value() ||
            marker.value("luksUuid", "") != profile.target.luks_uuid.value() ||
            marker.value("mapperName", "") != profile.target.mapper_name.value() ||
            marker.value("device", "") != profile.target.device.value().string()) {
            throw btrfsbackup::ValidationError("target activation marker does not match profile");
        }
    } catch (const btrfsbackup::ValidationError&) {
        throw;
    } catch (const std::exception&) {
        throw btrfsbackup::ValidationError("invalid target activation marker: " + marker_path.string());
    }
    return true;
}

void write_activation_marker(
    const ResolvedDependencies& resolved,
    const btrfsbackup::config::Profile& profile
) {
    btrfsbackup::platform::linux::filesystem::atomic_write(
        activation_marker_path(resolved, profile),
        btrfsbackup::config::dump_json({
            {"schemaVersion", 1},
            {"profileId", profile.id.value()},
            {"luksUuid", profile.target.luks_uuid.value()},
            {"mapperName", profile.target.mapper_name.value()},
            {"device", profile.target.device.value().string()},
        }),
        0600
    );
}

void remove_activation_marker(
    const ResolvedDependencies& resolved,
    const btrfsbackup::config::Profile& profile
) {
    const fs::path marker_path = activation_marker_path(resolved, profile);
    std::error_code error;
    fs::remove(marker_path, error);
    if (error) {
        throw btrfsbackup::ValidationError("cannot remove target activation marker: " + error.message());
    }
    btrfsbackup::platform::linux::filesystem::fsync_dir(marker_path.parent_path());
}

std::optional<btrfsbackup::platform::linux::filesystem::FileLock> acquire_target_lock(
    const btrfsbackup::config::Profile& profile,
    const fs::path& lock_root,
    const std::string& operation,
    std::vector<btrfsbackup::cli::TargetEvent>& events
) {
    std::optional<btrfsbackup::platform::linux::filesystem::FileLock> lock;
    lock.emplace(btrfsbackup::platform::linux::filesystem::target_lock_path(lock_root, profile.target.luks_uuid));
    if (!lock->try_acquire()) {
        events.push_back({
            .kind = btrfsbackup::cli::TargetEventKind::Busy,
            .detail = operation,
        });
        return std::nullopt;
    }
    return lock;
}

} // namespace

namespace btrfsbackup::cli {

const std::vector<TargetEvent>& target_operation_events(const TargetOperationResult& result) noexcept {
    return std::visit([](const auto& outcome) -> const std::vector<TargetEvent>& { return outcome.events; }, result);
}

TargetOperationResult activate_target(
    const ActivateTargetRequest& request,
    TargetServiceDependencies& dependencies
) {
    require_root();
    const btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::load_profile_by_id(
        request.profile_config_dir,
        std::string(request.profile_id.value())
    );
    ResolvedDependencies resolved = resolve_dependencies(dependencies);
    btrfsbackup::platform::linux::filesystem::FileLock activation_lock = acquire_activation_lock(resolved, profile);
    (void)activation_lock;
    std::vector<TargetEvent> events;
    const fs::path mapper = resolved.mapper_root / profile.target.mapper_name.value();

    validate_luks_uuid(resolved.commands, profile);
    if (fs::exists(mapper)) {
        if (!mapper_identity_matches(resolved.commands, profile, resolved.canonical_device)) {
            throw ValidationError(
                "Refusing to use mapper " + profile.target.mapper_name.value() +
                " because its underlying device does not match configuration"
            );
        }
        (void)activation_is_owned(resolved, profile);
        events.push_back({.kind = TargetEventKind::Activated, .detail = profile.target.mapper_name.value()});
        return TargetOperationCompleted{std::move(events)};
    }

    std::string key_file = "-";
    if (const auto* activation = std::get_if<btrfsbackup::config::KeyFileActivation>(&profile.target.activation)) {
        const fs::path& configured_key_file = activation->key_file.value();
        btrfsbackup::platform::linux::filesystem::validate_trusted_directory(
            configured_key_file.parent_path(),
            resolved.keyfile_trust_root,
            rootless_tests_allowed() ? geteuid() : 0
        );
        btrfsbackup::platform::linux::filesystem::assert_trusted_config_file(
            configured_key_file,
            {.allow_current_user_owner = rootless_tests_allowed()}
        );
        key_file = configured_key_file.string();
    }

    events.push_back({.kind = TargetEventKind::Activating, .detail = profile.target.mapper_name.value()});
    run_checked_controlled(
        resolved.commands,
        {
            resolved.systemd_cryptsetup_command,
            "attach",
            profile.target.mapper_name.value(),
            profile.target.device.value().string(),
            key_file,
            "luks",
        },
        "could not activate LUKS mapper " + profile.target.mapper_name.value(),
        std::chrono::seconds(80)
    );
    try {
        run_checked_controlled(
            resolved.commands,
            {"udevadm", "settle", "--timeout=10"},
            "udev did not publish the activated LUKS mapper",
            std::chrono::seconds(15)
        );
        if (!fs::exists(mapper) || !mapper_identity_matches(resolved.commands, profile, resolved.canonical_device)) {
            const fs::path reported = mapper_underlying_device(
                resolved.commands,
                profile.target.mapper_name.value()
            );
            throw ValidationError(
                "activated LUKS mapper identity does not match configuration: configured=" +
                profile.target.device.value().string() + " (canonical=" +
                resolved.canonical_device(profile.target.device).string() + "), reported=" +
                reported.string() + " (canonical=" + resolved.canonical_device(reported).string() + ")"
            );
        }
        write_activation_marker(resolved, profile);
    } catch (...) {
        run_ignored(
            resolved.commands,
            {resolved.systemd_cryptsetup_command, "detach", profile.target.mapper_name.value()}
        );
        throw;
    }
    events.push_back({.kind = TargetEventKind::Activated, .detail = profile.target.mapper_name.value()});
    return TargetOperationCompleted{std::move(events)};
}

TargetOperationResult deactivate_target(
    const DeactivateTargetRequest& request,
    TargetServiceDependencies& dependencies
) {
    require_root();
    const btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::load_profile_by_id(
        request.profile_config_dir,
        std::string(request.profile_id.value())
    );
    ResolvedDependencies resolved = resolve_dependencies(dependencies);
    btrfsbackup::platform::linux::filesystem::FileLock activation_lock = acquire_activation_lock(resolved, profile);
    (void)activation_lock;
    std::vector<TargetEvent> events;
    if (!activation_is_owned(resolved, profile)) {
        events.push_back({.kind = TargetEventKind::Deactivated, .detail = profile.target.mapper_name.value()});
        return TargetOperationCompleted{std::move(events)};
    }

    const fs::path mapper = resolved.mapper_root / profile.target.mapper_name.value();
    if (!fs::exists(mapper)) {
        remove_activation_marker(resolved, profile);
        events.push_back({.kind = TargetEventKind::Deactivated, .detail = profile.target.mapper_name.value()});
        return TargetOperationCompleted{std::move(events)};
    }
    if (!mapper_identity_matches(resolved.commands, profile, resolved.canonical_device)) {
        throw ValidationError("Refusing to deactivate a mapper that does not match configuration");
    }
    if (mapper_has_mounts(profile, resolved.read_mounts(), events, resolved.mapper_root)) {
        throw ValidationError("Refusing to deactivate LUKS mapper while it still has mounted filesystems");
    }
    run_checked_controlled(
        resolved.commands,
        {resolved.systemd_cryptsetup_command, "detach", profile.target.mapper_name.value()},
        "could not deactivate LUKS mapper " + profile.target.mapper_name.value(),
        std::chrono::seconds(80)
    );
    if (fs::exists(mapper)) {
        throw ValidationError("LUKS mapper remains active after deactivation");
    }
    remove_activation_marker(resolved, profile);
    events.push_back({.kind = TargetEventKind::Deactivated, .detail = profile.target.mapper_name.value()});
    return TargetOperationCompleted{std::move(events)};
}

TargetOperationResult mount_target(
    const MountTargetRequest& request,
    TargetServiceDependencies& dependencies
) {
    require_root();
    btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::load_profile_by_id(request.profile_config_dir, std::string(request.profile_id.value()));
    ResolvedDependencies resolved = resolve_dependencies(dependencies);
    std::vector<TargetEvent> events;
    std::optional<btrfsbackup::platform::linux::filesystem::FileLock> lock = acquire_target_lock(profile, resolved.lock_root, "mount", events);
    if (!lock.has_value()) {
        return TargetOperationBusy{std::move(events)};
    }

    validate_luks_uuid(resolved.commands, profile);
    std::vector<btrfsbackup::backup::MountEntry> mounts = resolved.read_mounts();
    if (btrfsbackup::backup::mount_at(mounts, profile.target.mount_point).has_value()) {
        btrfsbackup::platform::linux::filesystem::validate_trusted_directory(profile.target.mount_point, resolved.mount_point_trust_root, geteuid());
    } else {
        btrfsbackup::platform::linux::filesystem::ensure_trusted_directory(profile.target.mount_point, 0755, resolved.mount_point_trust_root, geteuid());
        events.push_back({.kind = TargetEventKind::Mounting, .detail = {}});
        const std::string mount_unit = btrfsbackup::platform::linux::systemd_mount_unit_name(profile.target.mount_point);
        run_checked(
            resolved.commands,
            {"systemctl", "start", mount_unit},
            "could not start target mount unit " + mount_unit
        );
    }

    btrfsbackup::backup::validate_backup_target_mount(profile, resolved.read_mounts());
    events.push_back({
        .kind = TargetEventKind::Mounted,
        .detail = profile.target.mount_point.value().string(),
    });
    return TargetOperationCompleted{std::move(events)};
}

TargetOperationResult eject_target(
    const EjectTargetRequest& request,
    TargetServiceDependencies& dependencies
) {
    require_root();
    btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::load_profile_by_id(request.profile_config_dir, std::string(request.profile_id.value()));
    std::vector<TargetEvent> events;
    if (request.automatic && !profile.settings.auto_eject) {
        events.push_back({.kind = TargetEventKind::AutomaticEjectDisabled, .detail = {}});
        return TargetOperationSkipped{std::move(events)};
    }

    ResolvedDependencies resolved = resolve_dependencies(dependencies);
    std::optional<btrfsbackup::platform::linux::filesystem::FileLock> lock = acquire_target_lock(profile, resolved.lock_root, "eject", events);
    if (!lock.has_value()) {
        return TargetOperationBusy{std::move(events)};
    }

    events.push_back({.kind = TargetEventKind::Synchronizing, .detail = {}});
    run_checked_controlled(resolved.commands, {"sync"}, "sync failed", std::chrono::minutes(5));

    std::vector<btrfsbackup::backup::MountEntry> mounts = resolved.read_mounts();
    if (btrfsbackup::backup::mount_at(mounts, profile.target.mount_point).has_value()) {
        if (!request.force && !btrfsbackup::backup::mount_uses_mapper(mounts, profile.target.mount_point, resolved.mapper_root / profile.target.mapper_name.value())) {
            throw ValidationError(
                "Refusing to unmount " + profile.target.mount_point.value().string() +
                " because it is not backed by /dev/mapper/" + profile.target.mapper_name.value()
            );
        }
        events.push_back({
            .kind = TargetEventKind::Unmounting,
            .detail = profile.target.mount_point.value().string(),
        });
        const std::string mount_unit = btrfsbackup::platform::linux::systemd_mount_unit_name(
            profile.target.mount_point
        );
        run_checked(resolved.commands, {"systemctl", "stop", mount_unit}, "could not stop target mount unit " + mount_unit);
    }

    const std::string target_unit = btrfsbackup::platform::linux::target_activation_unit_name(
        profile.id.value()
    );
    events.push_back({
        .kind = TargetEventKind::StoppingTargetUnit,
        .detail = target_unit,
    });
    run_checked(
        resolved.commands,
        {"systemctl", "stop", target_unit},
        "could not stop target activation unit " + target_unit
    );

    fs::path mapper = resolved.mapper_root / profile.target.mapper_name.value();
    if (fs::exists(mapper)) {
        if (!request.force && !mapper_identity_matches(resolved.commands, profile, resolved.canonical_device)) {
            throw ValidationError(
                "Refusing to close mapper " + profile.target.mapper_name.value() +
                " because its underlying device does not match configuration."
            );
        }
        if (mapper_has_mounts(profile, resolved.read_mounts(), events, resolved.mapper_root)) {
            throw ValidationError(
                "Refusing to close mapper " + profile.target.mapper_name.value() +
                " while it still has mounted filesystems."
            );
        }
        events.push_back({
            .kind = TargetEventKind::ClosingMapper,
            .detail = profile.target.mapper_name.value(),
        });
        run_checked(
            resolved.commands,
            {"cryptsetup", "close", profile.target.mapper_name.value()},
            "could not close mapper " + profile.target.mapper_name.value()
        );
    }

    if (fs::exists(profile.target.device)) {
        run_ignored(resolved.commands, {"blockdev", "--flushbufs", profile.target.device.value().string()});
    }
    run_ignored(resolved.commands, {"udevadm", "settle", "--timeout=10"});

    if (request.automatic && !request.service_succeeded) {
        events.push_back({.kind = TargetEventKind::EjectedAfterFailedBackup, .detail = {}});
    } else {
        events.push_back({.kind = TargetEventKind::Ejected, .detail = {}});
    }
    return TargetOperationCompleted{std::move(events)};
}

TargetOperationResult activate_target(const ActivateTargetRequest& request) {
    btrfsbackup::platform::linux::process::PosixCommandRunner commands;
    TargetServiceDependencies dependencies = production_dependencies(commands);
    return activate_target(request, dependencies);
}

TargetOperationResult deactivate_target(const DeactivateTargetRequest& request) {
    btrfsbackup::platform::linux::process::PosixCommandRunner commands;
    TargetServiceDependencies dependencies = production_dependencies(commands);
    return deactivate_target(request, dependencies);
}

TargetOperationResult mount_target(const MountTargetRequest& request) {
    btrfsbackup::platform::linux::process::PosixCommandRunner commands;
    TargetServiceDependencies dependencies = production_dependencies(commands);
    return mount_target(request, dependencies);
}

TargetOperationResult eject_target(const EjectTargetRequest& request) {
    btrfsbackup::platform::linux::process::PosixCommandRunner commands;
    TargetServiceDependencies dependencies = production_dependencies(commands);
    return eject_target(request, dependencies);
}

} // namespace btrfsbackup::cli
