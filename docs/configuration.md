# Configuration

The canonical-format decision and its compatibility consequences are recorded
in [ADR 0003](adr/0003-json-canonical-config.md).

## Canonical Profile JSON

The canonical format for tooling and runtime is a JSON profile document matching
`data/schemas/profile.schema.json`.

```bash
btrfs-backupctl profile validate --file profile.json
btrfs-backupctl profile render --file profile.json --output-dir ./generated-profile
sudo btrfs-backupctl profile save --file profile.json
btrfs-backupctl profile show --profile default
btrfs-backupctl profile export --profile default --output profile.json
```

`save` writes:

```text
/etc/btrfs-backup/profiles/<profile>/profile.json
/etc/udev/rules.d/99-btrfs-backup-<profile>.rules
/etc/systemd/system/<escaped-target-path>.mount
/etc/systemd/system/btrfs-backup@<profile>.service.d/target-mount.conf
/var/lib/btrfs-backup/public/profiles/<profile>.json
```

The systemd drop-in orders the profile's target mount before the sandboxed
service starts. A save stages and validates all managed artifacts, holds the profile
lock while publishing them, reloads systemd and udev, and publishes the public
profile last. A failure restores the previous files and attempts to reload
their rules.

The public profile contains only `profileId`, the profile display name, the
target label derived from `target.mapperName`, source ids/display names, and the
non-secret `configurationGeneration` commit marker. It excludes devices, UUIDs,
mount points, paths, retention settings, and hooks.

Installed private profiles also contain `configurationGeneration`. The udev
rule records it in a comment and the systemd drop-in passes it to the runner as
`BTRFS_BACKUP_CONFIGURATION_GENERATION`. A runner started by a mismatched
drop-in rejects the profile before performing backup work. Older profiles
without this installation metadata remain loadable when no generation is
provided by the service.

A failed transactional save reports `configuration.save_failed`. If restoring
the previous artifacts, synchronizing their directories, restoring legacy
source configuration, or reactivating the old rules also fails, the diagnostic
code is `configuration.rollback_incomplete`. Its message retains the original
save failure and lists the rollback errors. Unrestored `.previous-*` files are
left in place for recovery; `configurationGeneration` prevents a partially
restored installation from being accepted by the automatic runner.

`btrfs-backupctl profile wizard` follows the same model: it renders
`profile.json` first and then materializes derived files from that JSON.

## Active Profile JSON

The active profile file is
`/etc/btrfs-backup/profiles/<profile>/profile.json`. Commands select a profile
with `--profile <profile>` or `BTRFS_BACKUP_PROFILE=<profile>`.

Important fields:

| Field | Meaning |
|---|---|
| `profileId` | stable profile identifier used for state, status, history, and systemd unit instances |
| `name` | human-readable profile name |
| `enabled` | whether the profile may be used by runtime commands |
| `target.device` | stable path to the LUKS partition, preferably `/dev/disk/by-uuid/...` |
| `target.luksUuid` | expected UUID of the LUKS container |
| `target.btrfsUuid` | expected Btrfs filesystem UUID inside LUKS |
| `target.mapperName` | mapper name under `/dev/mapper` |
| `target.activation.mode` | `askPassword` or `keyFile` target activation mode |
| `target.activation.keyFile` | absolute root-only key path, required only for `keyFile` mode |
| `paths.remoteRoot` | directory for committed snapshots on the target |
| `paths.incomingRoot` | directory for uncommitted receives |
| `settings.remoteRetention` | default number of remote snapshots; `0` means unlimited |
| `settings.localRetention` | default number of local snapshots |
| `settings.dailyLimit` | at most one successful backup per local day for unchanged configuration |
| `settings.incrementalRequired` | refuse a full transfer when a remote chain exists without a common parent |
| `settings.keepFailedLocalSnapshot` | keep a local snapshot after an ordinary transfer error |
| `settings.autoEject` | automatically unmount and close LUKS |
| `settings.minimumTargetFreeBytes` | minimum free space required on the target |
| `settings.minimumLocalFreeBytes` | minimum free space required for local snapshots |
| `sources[].subvolume` | source Btrfs subvolume to snapshot |
| `sources[].localSnapshotDir` | local snapshot directory for the source |
| `sources[].remoteSubdir` | source-specific directory under `paths.remoteRoot` |
| `sources[].remoteRetention` | source-specific remote retention override |
| `sources[].localRetention` | source-specific local retention override |

A retention value of `0` disables automatic pruning for that scope.

## Global Application Configuration

Privileged filesystem roots are not profile properties. They are loaded from
the optional root-owned file `/etc/btrfs-backup.conf`:

```ini
CONFIG_VERSION=1
STATE_ROOT=/var/lib/btrfs-backup
STATUS_ROOT=/run/btrfs-backup/profiles
HISTORY_ROOT=/var/lib/btrfs-backup/history
TARGET_MOUNT_ROOT=/mnt/btrfs-backup
```

The parser accepts only these `KEY=VALUE` entries and comments beginning with
`#`; it never executes the file as shell code. The file must be owned by root,
must not be a symbolic link, and must not be writable by group or other users
(`0644` is accepted). If it is absent, the values shown above are used.

`STATUS_ROOT` contains reduced current status for unprivileged readers.
`HISTORY_ROOT` contains root-only diagnostic history; its directories use mode
`0700` and its JSON files use mode `0600`. Private recovery markers remain in
`STATE_ROOT/profiles/<profileId>`.
`TARGET_MOUNT_ROOT` controls the trusted namespace for target mounts. A profile
with id `laptop` is always mounted at `TARGET_MOUNT_ROOT/laptop`; neither the
mount point nor its systemd mount unit is stored in profile JSON.

Profile schema version 4 adds structured target activation and retains the
version 3 removal of `target.mountPoint` and `target.mountUnit`.
Legacy profiles are accepted only when their stored mount point already equals
`TARGET_MOUNT_ROOT/profileId`; other profiles must be migrated deliberately so
the target cannot silently move to a different path.

## JSON Schema

`data/examples/profile.example.json` and `data/schemas/profile.schema.json` define a
versioned, machine-readable profile model for tooling.

The profile model defines encrypted target identity, target-relative paths, retention
settings, application-consistency hooks, and source definitions. Core publishes
The state domain publishes reduced current status and writes private history and logs, while desktop
presentation belongs to the KDE integration.
It is intended for generators, validators, and future migration tooling.

## Source Definitions

Each entry in `profile.json` under `sources` describes one source:

```json
{
  "id": "home",
  "name": "home",
  "enabled": true,
  "subvolume": "/home",
  "localSnapshotDir": "/.snapshots/btrfs-backup/home",
  "remoteSubdir": "home",
  "remoteRetention": 45,
  "localRetention": 20
}
```

Source ids must be unique. `remoteSubdir` is a relative path under `remoteRoot`. The local snapshot directory must be on the same Btrfs filesystem as the source. The source must not belong to the Btrfs filesystem used as the backup target. This check is based on filesystem UUIDs, so it still works when the same Btrfs filesystem is mounted at multiple locations.

For the `/` source, use a dedicated subvolume for snapshots, such as `/.snapshots`. Placing the snapshot repository in a regular directory inside the source subvolume creates unnecessary empty nested-subvolume mount points in future snapshots.

## Application Hooks

Filesystem snapshots do not always provide application-level consistency. A
profile may define controlled hook phases around snapshot creation:

```json
{
  "hooks": {
    "beforeSnapshot": [
      {
        "type": "program",
        "program": "/etc/btrfs-backup/hooks.d/prepare-postgresql-backup",
        "arguments": [],
        "timeoutSeconds": 60
      }
    ],
    "afterSnapshot": []
  }
}
```

Hook commands must name a direct child of `/etc/btrfs-backup/hooks.d` and pass
an explicit argument array. The runtime does not execute arbitrary command text
through a shell. Install each hook as a regular, non-symlink file owned by root,
for example:

```bash
sudo install -o root -g root -m 0755 ./prepare-postgresql-backup \
  /etc/btrfs-backup/hooks.d/prepare-postgresql-backup
```

The hook file and every parent directory must be owned by root and must not be
writable by group or other users. The runtime checks this chain immediately
before execution, opens the hook without following symlinks, and executes the
pinned descriptor rather than resolving the configured path again.
`timeoutSeconds` is required and accepts values from 1 through 86400. Hook
failures and timeouts stop the run. Cancellation terminates the
hook's complete process group and finishes the run through the normal cancelled
state.

Hook environments contain only `PATH=/usr/bin`, `LANG=C.UTF-8`,
`LC_ALL=C.UTF-8`, `HOME=/root`, and the current
`BTRFS_BACKUP_PROFILE_ID` and `BTRFS_BACKUP_SOURCE_ID`. They do not inherit
variables from the runner process.

Hooks inherit the systemd service sandbox. In automatic runs they have a private
`/tmp`, can use only Unix and netlink sockets, cannot gain privileges through
setuid/file capabilities, and cannot create writable-executable memory. A hook
that needs a network connection or a JIT runtime should call a separately
managed, narrowly authorized local service over a Unix socket.

This model can cover PostgreSQL, MariaDB, libvirt, containers, virtual
machines, and administrator-provided programs without hard-coding those
integrations into the profile format first.

## Non-Interactive Configuration

Automation should create the canonical JSON profile directly:

```bash
btrfs-backupctl profile create \
  --output ./profile.json \
  --profile default \
  --name 'Default backup' \
  --device /dev/disk/by-uuid/<LUKS-UUID> \
  --luks-uuid <LUKS-UUID> \
  --mapper-name backupdisk \
  --keyfile /root/keys/backupdisk.key \
  --source home home /home /.snapshots/btrfs-backup/home home 30 30

btrfs-backupctl profile render --file ./profile.json --output-dir ./generated-profile
btrfs-backupctl installation render \
  --file ./profile.json \
  --output-dir ./generated
```

For manual setup, use `btrfs-backupctl profile wizard --render-only` and review
the generated files before applying them.

## Target Activation

The canonical profile contains one of these mode-specific objects:

```json
{"mode": "askPassword"}
{"mode": "keyFile", "keyFile": "/root/keys/backupdisk.key"}
```

`btrfs-backup-target@<profile>.service` activates the configured LUKS UUID on
demand. It records ownership in `/run/btrfs-backup/target-activation` and only
deactivates a mapper owned by that activation. A compatible mapper that was
already open is validated and preserved.

The key file and its parent path must pass the trusted-file checks; use root
ownership and mode `0600`. `askPassword` delegates prompting to systemd's
password agent and is not suitable for unattended operation without an agent.

## Migration From 3.0

Version 3.x can import the password field from the exact mapper and LUKS UUID
entry in an existing crypttab. The first command is a JSON preview; the second
publishes the migrated profile and generated units transactionally:

```bash
sudo btrfs-backupctl profile migrate-activation --profile default
sudo btrfs-backupctl profile migrate-activation --profile default --apply
```

The migrator accepts only the legacy options emitted by btrfs-backup 3.0 and
never changes crypttab. Existing fstab and crypttab lines are not required by
the new units and may remain unused. The target activation template can come
from the package's systemd unit load path and does not need to be copied into
`/etc/systemd/system`. The compatibility command is scheduled for removal in
4.0.

## udev Matching

The profile wizard builds match conditions from the LUKS UUID and, when
available, node type, PARTUUID, and serial number. The goal is to match a
specific partition, not every node on the same disk.

The rule does not execute scripts through `RUN+=`; it delegates work to systemd through `SYSTEMD_WANTS`.
Because its device identity and service instance are profile-specific, the rule
is generated by `src/config/profile_render.cpp` rather than installed as a
global static file. `data/udev/README.md` documents this ownership boundary.

## Incremental Chain Changes

With `settings.incrementalRequired` set to `true`, remote snapshots without a
matching local parent UUID cause an error. This is the conservative behavior.

If all local parents were intentionally lost, you can temporarily set
`settings.incrementalRequired` to `false`, perform one full transfer, and then
set it back to `true`.

## Output Directory Safety

Render commands validate the profile before touching `--output-dir`. A target
directory is accepted only when it does not exist, is empty, or contains the
root marker `.btrfs-backup-render-root` created by an earlier successful render.
The directory and marker must belong to the current user, and the marker must
not be writable by group or others. A non-empty unmarked directory is never
removed, including a home directory or active configuration below `/etc` or
`/var`. Every parent component must be a real directory owned by root or the
current user. Group/other-writable parents are accepted only with the sticky
bit, which permits normal rendering below `/tmp` without exposing a root-run
staging directory to pathname replacement by another user.

Rendering takes place in a sibling `.OUTPUT.stage-XXXXXX` directory. The staged
tree is validated and receives its marker before `renameat2(RENAME_EXCHANGE)`
publishes it. Therefore an invalid profile or generated tree leaves the previous
valid render intact. A render directory created by an older version without the
marker must be moved or removed explicitly once; it is not adopted
automatically. The default root output directory,
`/etc/btrfs-backup/generated`, remains separate from active files.
