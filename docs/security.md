# Security Model

## Trust Boundaries

The backup runtime is a privileged system tool. It validates and writes Btrfs
snapshots, opens and closes the configured LUKS target, mounts filesystems, and
updates root-owned state. User-facing tools must treat the privileged runtime
as the only component allowed to mutate system configuration or backup state.

Current runtime commands may run as root. Ordinary status readers can inspect
only the reduced public current-status JSON. Run history, profile state, key
material, and trusted runtime configuration are private to root.

## Configuration

Privileged filesystem roots belong to the global application configuration,
not to profiles. `/etc/btrfs-backup.conf` is an optional strict
`KEY=VALUE` file read without following symlinks. In production it must be
owned by root and not writable by group or other users. A profile cannot
redirect state, status, history, or source-definition writes; legacy schema v1
profiles are accepted only when those fields equal the historical defaults.

The active profile is trusted root-owned JSON:

```text
/etc/btrfs-backup/profiles/<profile>/profile.json
```

It must be owned by root and must not be readable or writable by group or other
users. Runtime code rejects unsafe ownership, unsafe permissions, directories,
and symbolic links before loading the profile.

Configuration is data, not executable code. The runtime must not `source` active
profile JSON, interpolate shell commands, or execute arbitrary text from the
profile. Application hooks pass an explicit program path, argument array, and
finite timeout to the process runner. Hook programs are restricted to direct
children of `/etc/btrfs-backup/hooks.d`. Before execution, the runtime rejects
symlinks, non-regular files, non-root owners, group/other-writable files, and
any non-root-owned or group/other-writable parent directory. It opens the file
with `openat2()` no-symlink resolution and executes the pinned descriptor, so a
path replacement between validation and process creation cannot select a
different inode.

Changing a profile's hook configuration is equivalent to scheduling code to
run as root. A future system API must authorize hook-bearing profile writes as
a separate high-risk operation. Its polkit policy must require an explicit
administrator decision and must not grant automatic consent merely because the
caller owns the active graphical session.

The complete method and action matrix is defined in
[system-dbus-api.md](system-dbus-api.md). The system bus policy is deny-by-default:
only sanitized read methods are available without polkit, operational methods
have distinct actions, and profile, device-preparation, and hook changes require
fresh administrator authorization. `SaveProfile` cannot be used to smuggle a
hook change through the lower-risk profile authorization.

Every external command receives a newly built environment containing only
`PATH=/usr/bin`, `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, and `HOME=/root`. Variables
from the privileged parent process, including interpreter search paths, shell
startup variables, temporary-directory paths, and desktop-session state, are
not inherited. Hooks additionally receive only the validated
`BTRFS_BACKUP_PROFILE_ID` and `BTRFS_BACKUP_SOURCE_ID` values for their current
action.

There is no unbounded synchronous process runner. Administrative commands use
the controlled nonblocking executor with a 30-second timeout, a 1 MiB output
limit, process-group termination, and bounded child reaping. Longer operations
must opt into an explicit finite timeout; target synchronization uses five
minutes.

Profile saves stage and validate every private and public JSON, udev, and
systemd artifact before acquiring the profile lock. Publication uses
same-directory temporary and rollback files, strict permissions, `fsync`,
atomic rename, and parent-directory `fsync`; the reduced public profile is the
last commit marker after systemd and udev reload. All artifacts carry the same
random `configurationGeneration`, and the service-provided generation must
match the private profile before runtime work starts. This generation check is
the crash-consistency guard for a transaction whose destinations span multiple
directories and potentially multiple filesystems. Render and save operations
must reject output paths that point at the repository root, system directories,
or active project configuration.

Target mount points are not profile-controlled. The runtime derives each one as
`TARGET_MOUNT_ROOT/profileId`, using `/mnt/btrfs-backup` as the default mount
root. Before starting a mount unit it walks the complete directory chain with
`openat2()` no-symlink resolution, requires every component to be owned by root
and not writable by group or others, creates missing components with `mkdirat()`,
and applies permissions with `fchmod()` on the opened mount-point descriptor.
It never calls path-based `chmod()` for the mount point. An already mounted
target is subjected to the same chain and ownership validation without changing
its permissions.

The shared atomic writer applies the same durable-write contract to private
runtime state and history, and to public current status: checked temporary-file
permissions, EINTR-safe writes, checked file `fsync` and close, checked rename,
and checked parent-directory `fsync`. A persistence error such as `EIO` or
`ENOSPC` is a failed operation even if the new directory entry became visible
before the failure was reported.

## Target Identity

The runtime must re-check the target after mount. A udev match is only a startup
hint, not proof that the correct backup target is mounted.

Target validation includes:

1. expected mapper name;
2. expected LUKS UUID;
3. expected Btrfs filesystem UUID;
4. expected filesystem type;
5. read-write mount state when a backup will modify the target;
6. `nodev`, `nosuid`, `noexec`, and `nosymfollow` mount restrictions;
7. remote and incoming paths staying inside the target mount point;
8. source filesystem being different from the target filesystem.

The executing runner pins the target mount point and the filesystem root with
directory descriptors. Storage paths are resolved with `openat2()` and
`RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS`. Destructive
cleanup, recovery and retention walk directories through dirfds and reject
symbolic links instead of following them. Snapshot targets are created relative
to a securely opened parent descriptor, and subvolumes are deleted with the
Btrfs destroy ioctl on that descriptor. Snapshot inventory and the external
`btrfs send/receive` processes use pinned, explicitly inherited descriptors, so
the checked paths are not resolved again after validation.

## Operation Locks

An executing runner acquires a profile lock and a target lock before mounting or
inspecting backup storage. The target key is the normalized LUKS UUID, so two
profiles cannot concurrently operate on the same encrypted repository. Mount
and eject commands participate in the same target lock namespace.

Lock files live below the root-owned `/run/btrfs-backup/locks` directory, are
opened without following a final symbolic link, and use non-blocking `flock`.
A rejected contender must not update the active runner's current status,
history, pending markers, checkpoints, or `last-success`.

## Public Data

Public current status exposes only presentation labels for the current source
and target, run state, percentage progress, speed, ETA, progress accuracy, and
a generic error code. Labels must come from sanitized profile fields, never
from paths or UUIDs. Status must not expose run ids, timestamps, paths, UUIDs,
diagnostic messages, details, suggested actions, or specific failure codes.

History directories use mode `0700` and history files use mode `0600`. Full
messages, specific error codes, paths, UUIDs, timestamps, and structured
diagnostics belong only in that private history.

## Privileged Actions

Manual start, force, validate, cancel, eject, save-profile, and delete-profile
operations must re-check authorization and profile identity in the privileged
component that performs the action. User-facing tools must not rely on local UI
validation as the final decision.

Eject must not run while a backup is active unless the privileged component can
prove that the target is idle and that the operation applies to the expected
mapper. Cancellation must target the matching backup unit or runner transaction,
not arbitrary processes. The current runner cancellation command writes a
root-owned request file under the selected profile state directory; the active
runner consumes that request and clears it after handling.

Service and terminal termination signals follow the same cancellation path.
The runner consumes SIGINT and SIGTERM through a file descriptor, requests its
active cancellation token, and terminates transfer process groups with a
SIGTERM grace period followed by SIGKILL. Installed systemd units use
`KillMode=mixed`: the initial SIGINT is delivered only to the runner, while the
90-second stop timeout and final cgroup SIGKILL remain an external bound for
processes stuck in kernel I/O.

## Service Sandbox

The systemd service remains root because it must inspect block devices and
perform Btrfs snapshot, send, receive, retention, and target lifecycle work.
The unit limits unrelated authority with:

```text
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=full
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
ProtectHostname=yes
ProtectClock=yes
ProtectProc=invisible
LockPersonality=yes
RestrictRealtime=yes
MemoryDenyWriteExecute=yes
SystemCallArchitectures=native
RestrictAddressFamilies=AF_UNIX AF_NETLINK
```

`AF_UNIX` remains available for communication with systemd and local services;
`AF_NETLINK` remains available for device information. A profile-specific
`RequiresMountsFor` drop-in makes PID 1 mount the configured target before the
filesystem sandbox is created; the runner then validates that mount normally.

The runner's `ExecStopPost` queues `btrfs-backup-eject@<profile>` for successful
and failed runs. Unit ordering delays it until the runner has left its private
mount namespace. The eject unit therefore operates on the host mount and can
close the mapper. It retains
`NoNewPrivileges`, socket-family restrictions, executable-memory protection,
personality locking, and realtime restrictions, but does not create a private
mount namespace.

These restrictions are inherited by application hooks. Hooks cannot gain
privileges through setuid binaries or file capabilities, create writable and
executable mappings, use Internet sockets, or access another process's `/tmp`.
Integrations requiring those facilities must be redesigned around a separately
managed service rather than weakening the backup unit globally.

Installed units and the shared process adapter restrict `PATH` to `/usr/bin`.
The adapter resolves bare executable names directly below `/usr/bin` and uses
`posix_spawn()` without path searching. This also applies to manually started
root commands and to subprocesses launched by trusted hooks.
