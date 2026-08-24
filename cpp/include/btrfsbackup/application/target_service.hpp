#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <btrfsbackup/application/runtime_adapters.hpp>
#include <btrfsbackup/system/mount_info.hpp>

namespace btrfsbackup {

struct TargetServiceDependencies {
    ICommandRunner& commands;
    std::function<std::vector<MountEntry>()> read_mounts;
    std::filesystem::path lock_root;
};

struct MountTargetRequest {
    std::filesystem::path profile_config_dir;
    std::string profile_id = "default";
};

struct EjectTargetRequest {
    std::filesystem::path profile_config_dir;
    std::string profile_id = "default";
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
