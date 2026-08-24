# Security Model

## Trust Boundaries

The backup runtime is a privileged system tool. It validates and writes Btrfs
snapshots, opens and closes the configured LUKS target, mounts filesystems, and
updates root-owned state. User-facing tools must treat the privileged runtime
as the only component allowed to mutate system configuration or backup state.

Current runtime commands may run as root. Ordinary status readers can inspect
public status and history JSON, but they must not read private profile state,
key material, or trusted runtime configuration directly.

## Configuration

The active profile is trusted root-owned JSON:

```text
/etc/btrfs-backup/profiles/<profile>/profile.json
```

It must be owned by root and must not be readable or writable by group or other
users. Runtime code rejects unsafe ownership, unsafe permissions, directories,
and symbolic links before loading the profile.

Configuration is data, not executable code. The runtime must not `source` active
profile JSON, interpolate shell commands, or execute arbitrary text from the
profile. Application hooks pass an explicit program path and argument array to
the process runner.

Profile writes must use same-directory temporary files, strict permissions,
`fsync`, atomic rename, and parent-directory `fsync` where practical. Render and
save operations must reject output paths that point at the repository root,
system directories, or active project configuration.

## Target Identity

The runtime must re-check the target after mount. A udev match is only a startup
hint, not proof that the correct backup target is mounted.

Target validation includes:

1. expected mapper name;
2. expected LUKS UUID;
3. expected Btrfs filesystem UUID;
4. expected filesystem type;
5. read-write mount state when a backup will modify the target;
6. remote and incoming paths staying inside the target mount point;
7. source filesystem being different from the target filesystem.

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

Public status and history files may expose profile id, profile name, run id,
source name, target state, phase, timing, byte counters, and structured error
codes. They must not expose key contents, passphrases, or private recovery
markers.

If public metadata contains administrative paths, clients should treat them as
locally visible operational metadata rather than secrets.

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
