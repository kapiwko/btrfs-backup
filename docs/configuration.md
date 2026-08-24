# Configuration

## Canonical Profile JSON

The canonical format for tooling and runtime is a JSON profile document matching
`config/profile.schema.json`.

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
/etc/systemd/system/btrfs-backup@<profile>.service.d/target-mount.conf
/var/lib/btrfs-backup/public/profiles/<profile>.json
```

The systemd drop-in orders the profile's target mount before the sandboxed
service starts. Run `systemctl daemon-reload` after saving a profile.

The public profile contains only `profileId`, the profile display name, the
target label derived from `target.mapperName`, and source ids/display names.
It excludes devices, UUIDs, mount points, paths, retention settings, and hooks.

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
| `target.mountPoint` | target mount point |
| `target.mountUnit` | `.mount` unit matching the mount point |
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
SOURCES_ROOT=/etc/btrfs-backup/profiles
STATE_ROOT=/var/lib/btrfs-backup
STATUS_ROOT=/run/btrfs-backup/profiles
HISTORY_ROOT=/var/lib/btrfs-backup/history
```

The parser accepts only these `KEY=VALUE` entries and comments beginning with
`#`; it never executes the file as shell code. The file must be owned by root,
must not be a symbolic link, and must not be writable by group or other users
(`0644` is accepted). If it is absent, the values shown above are used.

`STATUS_ROOT` contains reduced current status for unprivileged readers.
`HISTORY_ROOT` contains root-only diagnostic history; its directories use mode
`0700` and its JSON files use mode `0600`. Private recovery markers remain in
`STATE_ROOT/profiles/<profileId>`. `SOURCES_ROOT` is reserved for per-profile
source-definition migration and storage managed by the application.

## JSON Schema

`config/profile.example.json` and `config/profile.schema.json` define a
versioned, machine-readable profile model for tooling.

The profile model defines encrypted target identity, target-relative paths, retention
settings, application-consistency hooks, and source definitions. Core publishes
reduced current status and writes private history and logs, while desktop
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
  --mount-point /mnt/backup \
  --source home home /home /.snapshots/btrfs-backup/home home 30 30

btrfs-backupctl profile render --file ./profile.json --output-dir ./generated-profile
btrfs-backupctl installation render \
  --file ./profile.json \
  --output-dir ./generated \
  --keyfile none
```

For manual setup, use `btrfs-backupctl profile wizard --render-only` and review
the generated files before applying them.

## crypttab

The generated fragment looks like:

```text
backupdisk UUID=<LUKS-UUID> /root/keys/backupdisk.key luks,noauto,nofail,x-systemd.device-timeout=30s
```

`noauto` prevents opening the device on every boot. The mount unit requires the corresponding `systemd-cryptsetup@...` unit, so opening happens on demand.

The keyfile should be owned by root and use mode `0600`. Using `none` may require interactive passphrase entry through systemd ask-password.

## fstab

The generated entry uses `/dev/mapper/<name>`, `noauto`, `nodev`, `nosuid`,
`noexec`, `nosymfollow`, and a dependency on the cryptsetup unit. The runtime
rejects a target mount missing any of these security flags. After manually
merging the fragments, run:

```bash
sudo systemctl daemon-reload
sudo systemctl reset-failed btrfs-backup@default.service
```

## udev Matching

The profile wizard builds match conditions from the LUKS UUID and, when
available, node type, PARTUUID, and serial number. The goal is to match a
specific partition, not every node on the same disk.

The rule does not execute scripts through `RUN+=`; it delegates work to systemd through `SYSTEMD_WANTS`.

## Incremental Chain Changes

With `settings.incrementalRequired` set to `true`, remote snapshots without a
matching local parent UUID cause an error. This is the conservative behavior.

If all local parents were intentionally lost, you can temporarily set
`settings.incrementalRequired` to `false`, perform one full transfer, and then
set it back to `true`.

## Output Directory Safety

Render commands remove the previous contents of `--output-dir`, so they reject the repository root, template root, system directories, and any path containing active project configuration. The default root output directory, `/etc/btrfs-backup/generated`, is separate from active files.
