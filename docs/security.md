# Security Model

## Trust Boundaries

The backup runtime is a privileged system tool. It validates and writes Btrfs
snapshots, opens and closes the configured LUKS target, mounts filesystems, and
updates root-owned state. User-facing tools must treat the privileged runtime
as the only component allowed to mutate system configuration or backup state.

Direct runtime commands run as root. Globally readable manager responses are
reduced and sanitized. Provisioning discovery is a separate, active-session
interface: its device topology is sanitized, while source discovery may expose
the active user's Btrfs mount paths and filesystem UUIDs so that the user can
choose a source. The active local session may invoke already configured
operational controls through their separate polkit actions; inactive callers
still require administrator authentication. Repository browsing and local-path
coverage queries are available to the active local session. Repository reads
are checked against the snapshot's stored owner, group, mode and POSIX ACL
before the root manager opens a file. Run history, profile state, key
material, complete storage identity, and trusted runtime configuration are
private to root.

## Device Provisioning Trust Boundary

Provisioning crosses several independently enforced boundaries. No single UI
selection, identifier, authorization result, saved plan, device cgroup rule, or
device path is sufficient authority to modify storage.

| Component | Trust and responsibility |
|---|---|
| KCM and D-Bus caller | Untrusted input and presentation. It selects opaque candidates and supplies secrets by file descriptor. Its typed confirmation is a user-safety measure, not authorization. |
| System bus policy | Permits messages to reach the manager. It is transport filtering, not proof that a caller or operation is authorized. |
| Manager | Derives the unique bus name and UID from the connection, requires an active local caller for discovery, owns caller-bound candidates, and validates requests. It does not delegate destructive target selection to the client. |
| polkit | Grants the non-retained `prepare-backup-device` action to the current caller. It does not prove that a plan is fresh or that a device is unchanged. |
| systemd | Starts one helper instance and constrains its filesystem, capabilities, namespaces, and block-device access. These restrictions are defense in depth, not storage identity validation. |
| Preparation helper | Loads one root-owned transaction, revalidates its exact source and target, performs the authorized storage operations, checkpoints progress, and publishes the profile. |

Provisioning data has distinct disclosure and lifetime rules:

- sanitized status and topology responses contain presentation data, stable
  codes, geometry, safety decisions, and opaque identifiers, but no device
  nodes, hardware identity, mount paths, labels, or storage UUIDs;
- source candidates are active-session selection data. Their paths and
  filesystem UUIDs may be displayed, but `StartDevicePreparation` accepts only
  the caller-bound, expiring candidate identifier and resolves the stored values
  in the manager;
- complete topology snapshots, plans, and compatible-target inspection
  fingerprints are held only inside the manager and are bound to the unique bus
  caller and an expiry time;
- transaction records, profile reservations, private diagnostics, credentials,
  and generated keys are root-only durable state;
- passphrases and key bytes cross process boundaries through bounded file
  descriptors or the root-only helper FIFO, never through JSON, command-line
  arguments, environment variables, or logs.

The preparation sequence preserves the following order:

1. The manager scans storage, assigns random candidate identifiers, returns a
   sanitized view, and retains the complete caller-bound snapshot.
2. Plan creation rescans storage and rejects an expired or changed topology
   generation. Existing-target adoption additionally requires a matching
   read-only inspection performed between two safety checks.
3. Start resolves the stored plan and source candidate, checks topology and
   active users again, and only then requests the non-retained polkit action.
4. After authorization, the manager verifies that the caller is still active,
   consumes the single-use plan and inspection, and re-resolves the source from
   current mount information.
5. Before the helper starts, the backend re-resolves the source, seals the
   passphrase, reserves the profile identifier, persists the initial
   transaction, and configures the unit's narrow block-device allow-list.
6. The helper reloads the transaction and repeats source, block graph, stable
   identity, geometry, signature, mount, swap, and holder validation before the
   first destructive operation. Cancellation is disabled once that boundary is
   crossed.
7. Profile publication is create-only. Success installs the profile before
   releasing its reservation; failure records recovery state and releases the
   reservation only when doing so is safe.

## Browse Sessions

Browse-session directories, per-UID grouping directories and read-only bind
mount points remain owned by root with private permissions. The public session
document contains only the opaque session identifier, profile identifier,
expiry and read-only state; it never exposes a local mount path. KIO accesses
entries through confined manager operations, while restore receives a descriptor
pinned directly to the authorized file or directory inside a verified snapshot.
The private repository layout therefore remains inaccessible to the desktop
process and is not part of the path used by the restore engine.

Each active KIO or restore operation acquires an identified session lease. A
lease expires after five minutes, and a browse session closes after at most one
hour even when a client keeps renewing it. Losing the caller's bus name clears
all of its sessions and leases immediately.
Repository traversal rejects absolute paths, `..`, symlinks and special files,
and regular-file reads use already-open descriptors passed over D-Bus.
Stored ownership, group mode bits and POSIX ACLs are evaluated against the UID,
primary GID and supplementary groups captured from the actual D-Bus sender
process when the browse session opens. Account-database membership is not used
as a substitute for the caller process credentials.

Opaque identifiers prevent clients from choosing arbitrary device paths, but
they are not capabilities outside their caller binding and expiry. A topology
generation detects intervening discovery changes, but it does not replace the
helper's immediate revalidation. Similarly, `DeviceAllow` limits reachable
block devices if another check fails; it must never be used as evidence that a
reachable device is the intended target.

The preparation unit starts with an empty static device policy. For each
transaction the manager replaces, rather than appends to, the runtime allow
list with exact `major:minor` identities. Existing children of the selected
disk are initially readable for the final safety scan, then removed unless
selected. A partition created by the helper is authorized only after its block
node exists. Device-mapper control is exposed
only while opening or closing the transaction's LUKS mapping, and the resulting
mapper node is then authorized by its exact identity. Broad block-device class
rules are forbidden.

## Configuration

Privileged filesystem roots belong to the global application configuration,
not to profiles. `/etc/btrfs-backup.conf` is an optional strict
`KEY=VALUE` file read without following symlinks. In production it must be
owned by root and not writable by group or other users. A profile cannot
redirect state, status, or history writes; legacy schema v1
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
only sanitized read methods are available without polkit and operational methods
have distinct actions. Ordinary profile changes use narrow domain operations and
one retained administrator authorization. They never accept hooks, key paths or
a complete profile document; hook and device-provisioning APIs remain outside
this lower-risk boundary.

LUKS passphrases and key bytes cross D-Bus only as Unix file descriptors. The
manager copies at most 4096 bytes into a sealed anonymous file, clears temporary
buffers, and supplies them to libcryptsetup through its binary passphrase API.
Generated keys are installed atomically with mode `0600` under
`/etc/btrfs-backup/keys`; managed keyslot labels are stored separately under
`/etc/btrfs-backup/credentials`. The LUKS2 header remains authoritative for
occupied slots, unknown slots cannot be removed through the KCM, and the last
credential or the current automatic credential cannot be removed.

Credential additions track the keyslot, key file, metadata, and profile
publication as one mutation. A complete rollback preserves the primary error;
an incomplete rollback raises `credential.mutation_rollback_incomplete` with
the failed stage and explicit outcomes for every compensating action.

Credential removal first moves a managed key file into a root-only quarantine,
then removes the LUKS keyslot and atomically commits the metadata update. A
failure before keyslot removal restores the quarantined file. A failure after
the irreversible keyslot mutation raises a typed recovery-required error.
Failure to delete an already obsolete quarantined file is logged as a cleanup
warning and does not change the successful operation result.

Managed key metadata accepts only safe filenames located directly below the
configured key root. Secret files are created and managed relative to a pinned,
owner-validated directory descriptor. Publication uses
`renameat2(RENAME_NOREPLACE)`, so an existing file or symlink is never replaced
and correctness does not depend on a separate existence check.

Device preparation has its own `io.github.btrfsbackup.prepare-backup-device`
polkit action without retained authorization. The daemon binds short-lived
random candidate and plan identifiers to a caller-owned topology snapshot.
The unprivileged topology response contains only display ordinals, coarse
device properties, storage geometry, safety decisions and stable blocker
codes. It omits device nodes, serial numbers, WWNs, models, labels, filesystem
and partition UUIDs, mount paths and free-form diagnostic details. Preparation
plans and prepared-target inspections follow the same boundary; complete
storage identity remains only in the manager's caller-bound records and in the
helper transaction.
Before authorization it rescans storage and compares the planned destructive
scope with the current stable device identity, partition table, sector sizes,
partition geometry, signatures, mounts, swap and holders. Whole-device plans
inspect every child; existing-partition plans ignore unrelated siblings but are
executed only against the selected partition. The helper never invokes the
partition-table adapter for this mode. Its libblkid signature adapter compares
the expected filesystem type, version, label and UUID on the exclusively opened
partition immediately before erasing signatures.
The helper revalidates the block graph and active users again immediately before
signature erasure. Signature erasure uses libblkid on an `O_EXCL|O_NOFOLLOW`
descriptor after checking that the opened block node still has the expected
`major:minor`; it does not launch `wipefs`. Post-format filesystem and partition
identifiers are read from a descriptor-backed libblkid probe instead of a
`blkid` process. Whole-device GPT creation uses a deliberately narrow libfdisk
adapter. It verifies `major:minor` on an `O_EXCL|O_NOFOLLOW` descriptor, holds an
advisory device lock, uses libfdisk alignment and requires the kernel partition
table reread to succeed. A libudev monitor is enabled before the table is
written; completion requires a visible partition with the expected parent,
number and geometry. Filesystem creation is verified directly with a
descriptor-backed libblkid probe, so provisioning does not invoke `udevadm settle`.
Source discovery returns only writable Btrfs mount roots with an explicit
filesystem UUID. The manager exposes them through random, caller-bound,
short-lived identifiers instead of accepting a client-provided path. It derives
the profile's local snapshot directory below the selected mount, rejects a
nested mount with a different filesystem identity, and persists the source UUID,
mount root, and derived directory in the preparation transaction. The helper
repeats this mount and identity validation and verifies the chosen source as a
Btrfs subvolume before the first target write. It then disables cancellation.
The long-lived manager persists
the request and launches one
`btrfs-backup-device-preparation@<operationId>.service` instance. That
short-lived helper receives the passphrase through a root-only FIFO, executes
exactly one transaction, and checkpoints every phase. Its unit has a closed
device policy. Before starting an operation, the manager replaces the unit's
device allow-list with the exact selected disk and its existing partitions.
Operations that create a partition additionally receive only the selected
disk's kernel block-driver group because the new partition has no device number
until after the unit starts. Device-mapper access is a separate explicit group;
the blanket `block-*` grant is forbidden by tests. The unit also has a strict
filesystem sandbox and only the capabilities required for storage administration. No QML, KDE, or
long-lived D-Bus worker thread invokes `mkfs.btrfs` directly. LUKS2
metadata, keyslots and mappings are managed through libcryptsetup; protected
credential buffers use `crypt_safe_alloc` and are released with
`crypt_safe_free`.

Prepared-target inspection activates an existing LUKS2 mapping with
`CRYPT_ACTIVATE_READONLY`. It accepts only a topology candidate without mounts,
swap, holders or safety blockers and rechecks the LUKS UUID before activation.
The mapped device must probe as Btrfs, and its filesystem UUID must match the
repository identity. Inspection mounts it with libmount, external helpers
disabled, and `ro,nodev,nosuid,noexec,nologreplay`; the resulting mount flags
and mountinfo entry are verified before repository metadata is read. Both the
mount and mapper are closed before an inspection result is returned, including
failure paths. This inspection does not format storage or modify LUKS keyslots.
The D-Bus method accepts an opaque partition candidate and credential file
descriptor. The manager compares the caller-owned topology both before and
after inspection, then stores only a random, expiring inspection identifier and
the verified storage and repository identities. The identifier is issued only
for a compatible repository. Empty Btrfs filesystems, recognized legacy
`<profile>/snapshots` layouts, unsupported repository versions and foreign or
invalid contents are returned as non-adoptable classifications. A newer
topology scan removes the caller's inspections and plans. An adoption plan must
name the same caller,
topology generation, partition candidate and inspection identifier; its
destructive scope is `none` and its before/after layouts are identical.

Preparation operation identifiers contain 128 random bits supplied by
`getrandom()`. The manager persists the initiating D-Bus unique name and UID in
a root-only transaction record. Status and cancellation require the matching
owner (the UID is used after a daemon restart) or a fresh administrator
authorization. Transaction files use mode `0600` in a daemon-owned `0700`
directory. Existing directories, lock files and profile reservations with a
different owner, type or mode are rejected rather than repaired in place.
Records are updated using a same-directory atomic rename followed by file and
parent directory synchronization. Every document carries a monotonically
increasing revision. Manager and helper transitions take an exclusive
per-operation `flock()`, reload the document under that lock, compare the
expected revision, validate the state transition, and only then publish the
next revision.
Terminal transactions are immutable. Cancellation is persisted separately as
`cancelRequested`, so a manager request cannot replace the helper's recorded
phase or artifact UUIDs.

Transaction recovery reads each record independently through the bounded
no-symlink document reader and requires a regular file owned by the daemon UID
with mode `0600`. A malformed, oversized, unsafe, or unsupported record is
preserved for diagnostics and exposed only as a sanitized
`manual-intervention-required` status. It does not prevent valid transactions
from being restored or pruned.

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
directories and potentially multiple filesystems. Save operations must reject
paths that redirect active configuration outside its fixed installation roots.
Save failures use the stable diagnostic code `configuration.save_failed`. The
rollback remains best-effort so that it cannot replace the primary failure, but
every failed removal, previous-version restore, directory `fsync`, legacy-source
restore, or rules reactivation is collected. An incomplete rollback is reported
as `configuration.rollback_incomplete`, includes both the primary failure and
the rollback diagnostics, and preserves any `.previous-*` artifact that could
not be restored for operator recovery. The generation check keeps such a mixed
installation fail-closed.

Offline profile and wizard rendering never calls `remove_all()` on the supplied
output path. Profiles are validated first, then rendered and validated in a
new sibling staging directory. An existing output is replaceable only when it
is empty or contains the strict `.btrfs-backup-render-root` ownership marker;
both the directory and marker must belong to the effective user. Publication
uses an atomic directory exchange, rechecks the old directory identity and
marker after the exchange, and only then removes that known render tree. Thus a
home directory, repository, or active configuration tree without the marker is
preserved rather than inferred from a path blacklist. The complete parent chain
must contain only real directories owned by root or the effective user;
group/other-writable parents require the sticky bit. This prevents a root-run
renderer's staging path from being replaced below another user's directory.

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

Provisioning discovery is not part of globally readable public status. Device
topology, plans and inspection results remain sanitized even for an active
caller. Source discovery is the narrow exception: it requires the active local
session and exposes eligible Btrfs source mount paths and filesystem UUIDs for
selection, while subsequent mutating requests refer to the stored source only
by its random, caller-bound identifier.

## Privileged Actions

Manual start, force, validate, cancel, eject, save-profile, and delete-profile
operations must re-check authorization and profile identity in the privileged
component that performs the action. User-facing tools must not rely on local UI
validation as the final decision.

Eject must not run while a backup is active unless the privileged component can
prove that the target is idle and that the operation applies to the expected
mapper. Manual eject closes idle browse sessions for the selected profile before
checking the target mount. New browse sessions for that profile remain blocked
until the eject command finishes, so a desktop client cannot reopen the target
during teardown. An active browse or restore lease blocks eject instead of
interrupting an in-progress read. Cancellation must target the matching backup unit or runner transaction,
not arbitrary processes. The runner cancellation command requires both the
profile and run identifiers. It writes a root-owned request file under the
selected profile state directory only while that exact run is registered as
active; the active runner consumes that request and clears it after handling.

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

The runner's `OnSuccess` and `OnFailure` dependencies queue
`btrfs-backup-eject@<profile>` only after a successful or failed runner has
reached its final state. The runner's private mount namespace is gone before the
eject unit operates on the host mount and closes the mapper. It retains
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
