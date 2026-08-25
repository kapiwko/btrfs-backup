// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <platform/linux/command_runner.hpp>
#include <platform/linux/mount_info.hpp>
#include <config/identifiers.hpp>

namespace btrfsbackup {

struct TargetServiceDependencies {
    ICommandRunner& commands;
    std::function<std::vector<MountEntry>()> read_mounts;
    std::filesystem::path lock_root;
    std::filesystem::path mount_point_trust_root;
};

struct MountTargetRequest {
    std::filesystem::path profile_config_dir;
    ProfileId profile_id{"default"};
};

struct EjectTargetRequest {
    std::filesystem::path profile_config_dir;
    ProfileId profile_id{"default"};
    bool force = false;
    bool automatic = false;
    bool service_succeeded = true;
};

enum class TargetEventKind {
    AutomaticEjectDisabled,
    Busy,
    Mounting,
    Mounted,
    Synchronizing,
    Unmounting,
    StoppingCryptUnit,
    MapperStillMounted,
    ClosingMapper,
    EjectedAfterFailedBackup,
    Ejected,
};

struct TargetEvent {
    TargetEventKind kind;
    std::string detail;
};

struct TargetOperationResult {
    bool busy = false;
    bool skipped = false;
    std::vector<TargetEvent> events;
};

TargetOperationResult mount_target(
    const MountTargetRequest& request,
    TargetServiceDependencies* dependencies = nullptr
);

TargetOperationResult eject_target(
    const EjectTargetRequest& request,
    TargetServiceDependencies* dependencies = nullptr
);

} // namespace btrfsbackup
