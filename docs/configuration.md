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
/var/lib/btrfs-backup/public/profiles/<profile>.json
```

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
| `paths.stateDir` | base state directory; per-profile state lives under `profiles/<profileId>` |
| `paths.statusRoot` | root directory for per-profile `current.json` runtime status |
| `paths.historyRoot` | root directory for per-profile run history JSON |
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
`paths.statusRoot` and `paths.historyRoot` are intended for unprivileged status
readers. Private recovery markers remain in
`paths.stateDir/profiles/<profileId>`.

## JSON Schema

`config/profile.example.json` and `config/profile.schema.json` define a
versioned, machine-readable profile model for tooling.

The profile model mirrors the shell configuration: encrypted target identity,
state paths, retention settings, notification policy, and source definitions.
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

The generated entry uses `/dev/mapper/<name>`, `noauto`, and a dependency on the cryptsetup unit. After manually merging the fragments, run:

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
