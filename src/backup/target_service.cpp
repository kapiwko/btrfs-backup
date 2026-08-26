// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/target_service.hpp>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <backup/target_mount_validation.hpp>
#include <config/errors.hpp>
#include <config/profile.hpp>
#include <config/profile_loader.hpp>
#include <config/validation.hpp>
#include <platform/linux/command_runner.hpp>
#include <platform/linux/device_info.hpp>
#include <platform/linux/file_lock.hpp>
#include <platform/linux/mount_info.hpp>
#include <platform/linux/process.hpp>
#include <platform/linux/trusted_directory.hpp>

namespace fs = std::filesystem;

namespace {

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
    btrfsbackup::ICommandRunner& commands,
    const std::vector<std::string>& argv,
    const std::string& message
) {
    if (commands.run(argv).exit_code != 0) {
        throw btrfsbackup::ValidationError(message);
    }
}

void run_checked_controlled(
    btrfsbackup::ICommandRunner& commands,
    const std::vector<std::string>& argv,
    const std::string& message,
    std::chrono::milliseconds timeout
) {
    btrfsbackup::ControlledCommandOptions options;
    options.timeout = timeout;
    btrfsbackup::CommandResult result = commands.run_controlled(argv, options);
    if (result.exit_code != 0 || result.timed_out || result.cancelled) {
        throw btrfsbackup::ValidationError(message);
    }
}

void run_ignored(btrfsbackup::ICommandRunner& commands, const std::vector<std::string>& argv) {
    (void)commands.run(argv);
}

std::string cryptsetup_unit_name(btrfsbackup::ICommandRunner& commands, const std::string& mapper_name) {
    return btrfsbackup::capture_command(
        commands,
        {"systemd-escape", "--template=systemd-cryptsetup@.service", mapper_name}
    );
}

void validate_luks_uuid(btrfsbackup::ICommandRunner& commands, const btrfsbackup::Profile& profile) {
    std::string actual = btrfsbackup::capture_command(commands, {"cryptsetup", "luksUUID", profile.target.device});
    if (actual.empty() || lower(actual) != lower(profile.target.luks_uuid)) {
        throw btrfsbackup::ValidationError("LUKS UUID mismatch for " + profile.target.device);
    }
}

std::string mapper_underlying_device(btrfsbackup::ICommandRunner& commands, const std::string& mapper_name) {
    std::string status = btrfsbackup::capture_command(commands, {"cryptsetup", "status", mapper_name});
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

bool mapper_identity_matches(btrfsbackup::ICommandRunner& commands, const btrfsbackup::Profile& profile) {
    fs::path configured = btrfsbackup::canonical_device(profile.target.device);
    fs::path actual = btrfsbackup::canonical_device(mapper_underlying_device(commands, profile.target.mapper_name));
    if (configured.empty() || actual.empty() || configured != actual) {
        return false;
    }
    try {
        validate_luks_uuid(commands, profile);
    } catch (const btrfsbackup::ValidationError&) {
        return false;
    }
    return true;
}

bool mapper_has_mounts(
    const btrfsbackup::Profile& profile,
    const std::vector<btrfsbackup::MountEntry>& mounts,
    btrfsbackup::TargetOperationResult& result
) {
    fs::path mapper = fs::path("/dev/mapper") / profile.target.mapper_name;
    for (const btrfsbackup::MountEntry& mount : mounts) {
        if (btrfsbackup::normalized_path(btrfsbackup::strip_subvolume_suffix(mount.source))
            == btrfsbackup::normalized_path(mapper)) {
            result.events.push_back({
                .kind = btrfsbackup::TargetEventKind::MapperStillMounted,
                .detail = mount.target,
            });
            return true;
        }
    }
    return false;
}

struct ResolvedDependencies {
    std::unique_ptr<btrfsbackup::PosixCommandRunner> system_commands;
    btrfsbackup::ICommandRunner* commands = nullptr;
    std::function<std::vector<btrfsbackup::MountEntry>()> read_mounts;
    fs::path lock_root;
    fs::path mount_point_trust_root;
};

ResolvedDependencies resolve_dependencies(btrfsbackup::TargetServiceDependencies* dependencies) {
    ResolvedDependencies resolved;
    if (dependencies == nullptr) {
        resolved.system_commands = std::make_unique<btrfsbackup::PosixCommandRunner>();
        resolved.commands = resolved.system_commands.get();
    } else {
        resolved.commands = &dependencies->commands;
    }
    resolved.read_mounts = dependencies == nullptr || !dependencies->read_mounts
        ? std::function<std::vector<btrfsbackup::MountEntry>()>([] { return btrfsbackup::read_mount_table(); })
        : dependencies->read_mounts;
    resolved.lock_root = dependencies == nullptr || dependencies->lock_root.empty()
        ? btrfsbackup::default_lock_root()
        : dependencies->lock_root;
    resolved.mount_point_trust_root = dependencies == nullptr || dependencies->mount_point_trust_root.empty()
        ? fs::path("/")
        : dependencies->mount_point_trust_root;
    return resolved;
}

std::optional<btrfsbackup::FileLock> acquire_target_lock(
    const btrfsbackup::Profile& profile,
    const fs::path& lock_root,
    const std::string& operation,
    btrfsbackup::TargetOperationResult& result
) {
    std::optional<btrfsbackup::FileLock> lock;
    lock.emplace(btrfsbackup::target_lock_path(lock_root, profile.target.luks_uuid));
    if (!lock->try_acquire()) {
        result.busy = true;
        result.events.push_back({
            .kind = btrfsbackup::TargetEventKind::Busy,
            .detail = operation,
        });
        return std::nullopt;
    }
    return lock;
}

} // namespace

namespace btrfsbackup {

TargetOperationResult mount_target(
    const MountTargetRequest& request,
    TargetServiceDependencies* dependencies
) {
    require_root();
    Profile profile = load_profile_by_id(request.profile_config_dir, std::string(request.profile_id.value()));
    ResolvedDependencies resolved = resolve_dependencies(dependencies);
    TargetOperationResult result;
    std::optional<FileLock> lock = acquire_target_lock(profile, resolved.lock_root, "mount", result);
    if (!lock.has_value()) {
        return result;
    }

    validate_luks_uuid(*resolved.commands, profile);
    std::vector<MountEntry> mounts = resolved.read_mounts();
    if (mount_at(mounts, profile.target.mount_point).has_value()) {
        validate_trusted_directory(profile.target.mount_point, resolved.mount_point_trust_root, geteuid());
    } else {
        ensure_trusted_directory(profile.target.mount_point, 0755, resolved.mount_point_trust_root, geteuid());
        result.events.push_back({.kind = TargetEventKind::Mounting, .detail = {}});
        run_checked(
            *resolved.commands,
            {"systemctl", "start", profile.target.mount_unit},
            "could not start target mount unit " + profile.target.mount_unit
        );
    }

    validate_target_mount(profile, resolved.read_mounts());
    result.events.push_back({
        .kind = TargetEventKind::Mounted,
        .detail = profile.target.mount_point,
    });
    return result;
}

TargetOperationResult eject_target(
    const EjectTargetRequest& request,
    TargetServiceDependencies* dependencies
) {
    require_root();
    Profile profile = load_profile_by_id(request.profile_config_dir, std::string(request.profile_id.value()));
    TargetOperationResult result;
    if (request.automatic && !profile.settings.auto_eject) {
        result.skipped = true;
        result.events.push_back({.kind = TargetEventKind::AutomaticEjectDisabled, .detail = {}});
        return result;
    }

    ResolvedDependencies resolved = resolve_dependencies(dependencies);
    std::optional<FileLock> lock = acquire_target_lock(profile, resolved.lock_root, "eject", result);
    if (!lock.has_value()) {
        return result;
    }

    result.events.push_back({.kind = TargetEventKind::Synchronizing, .detail = {}});
    run_checked_controlled(*resolved.commands, {"sync"}, "sync failed", std::chrono::minutes(5));

    std::vector<MountEntry> mounts = resolved.read_mounts();
    if (mount_at(mounts, profile.target.mount_point).has_value()) {
        if (!request.force
            && !mount_uses_mapper(
                mounts,
                profile.target.mount_point,
                fs::path("/dev/mapper") / profile.target.mapper_name
            )) {
            throw ValidationError(
                "Refusing to unmount " + profile.target.mount_point
                + " because it is not backed by /dev/mapper/" + profile.target.mapper_name
            );
        }
        result.events.push_back({
            .kind = TargetEventKind::Unmounting,
            .detail = profile.target.mount_point,
        });
        run_checked(
            *resolved.commands,
            {"umount", "--", profile.target.mount_point},
            "could not unmount " + profile.target.mount_point
        );
    }

    std::string crypt_unit = cryptsetup_unit_name(*resolved.commands, profile.target.mapper_name);
    result.events.push_back({
        .kind = TargetEventKind::StoppingCryptUnit,
        .detail = crypt_unit,
    });
    run_ignored(*resolved.commands, {"systemctl", "stop", crypt_unit});

    fs::path mapper = fs::path("/dev/mapper") / profile.target.mapper_name;
    if (fs::exists(mapper)) {
        if (!request.force && !mapper_identity_matches(*resolved.commands, profile)) {
            throw ValidationError(
                "Refusing to close mapper " + profile.target.mapper_name
                + " because its underlying device does not match configuration."
            );
        }
        if (mapper_has_mounts(profile, resolved.read_mounts(), result)) {
            throw ValidationError(
                "Refusing to close mapper " + profile.target.mapper_name
                + " while it still has mounted filesystems."
            );
        }
        result.events.push_back({
            .kind = TargetEventKind::ClosingMapper,
            .detail = profile.target.mapper_name,
        });
        run_checked(
            *resolved.commands,
            {"cryptsetup", "close", profile.target.mapper_name},
            "could not close mapper " + profile.target.mapper_name
        );
    }

    if (fs::exists(profile.target.device)) {
        run_ignored(*resolved.commands, {"blockdev", "--flushbufs", profile.target.device});
    }
    run_ignored(*resolved.commands, {"udevadm", "settle", "--timeout=10"});

    if (request.automatic && !request.service_succeeded) {
        result.events.push_back({.kind = TargetEventKind::EjectedAfterFailedBackup, .detail = {}});
    } else {
        result.events.push_back({.kind = TargetEventKind::Ejected, .detail = {}});
    }
    return result;
}

} // namespace btrfsbackup
