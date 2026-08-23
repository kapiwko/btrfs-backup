# Configuration

## Canonical Profile JSON

The canonical format for tooling is a JSON profile document matching
`config/profile.schema.json`. The runner does not read this JSON directly.
Instead, `btrfs-backup-profile` validates the JSON and materializes trusted
runtime files for the Bash runner.

```bash
btrfs-backup-profile validate --file profile.json
btrfs-backup-profile render --file profile.json --output-dir ./generated-profile
sudo btrfs-backup-profile save --file profile.json
btrfs-backup-profile show --profile default
btrfs-backup-profile export --profile default --output profile.json
```

`save` writes:

```text
/etc/btrfs-backup/profiles.d/<profile>.env
/etc/btrfs-backup/profiles/<profile>/sources.d/*.conf
/etc/udev/rules.d/99-btrfs-backup-<profile>.rules
/var/lib/btrfs-backup/public/profiles/<profile>.json
```

`btrfs-backup-configure` follows the same model: it renders `profile.json`
first and then materializes the runtime files from that JSON.

## Runtime Profile Files

The preferred active profile file is
`/etc/btrfs-backup/profiles.d/<profile>.env`. Commands select a profile with
`--profile <profile>` or `BTRFS_BACKUP_PROFILE=<profile>`.

For compatibility, `/etc/btrfs-backup/backup.env` remains supported as the
fallback configuration for the `default` profile when
`/etc/btrfs-backup/profiles.d/default.env` does not exist. This legacy fallback
is deprecated in 1.1 and will be removed in 2.0.

Important fields:

| Field | Meaning |
|---|---|
| `PROFILE_ID` | stable profile identifier used for state and future profile-aware units |
| `PROFILE_NAME` | human-readable profile name |
| `BACKUP_MAPPER_NAME` | mapper name under `/dev/mapper` |
| `BACKUP_DEVICE` | stable path to the LUKS partition, preferably `/dev/disk/by-uuid/...` |
| `BACKUP_LUKS_UUID` | expected UUID of the LUKS container |
| `BACKUP_BTRFS_UUID` | optional Btrfs UUID inside LUKS |
| `BACKUP_MOUNTPOINT` | target mount point |
| `BACKUP_MOUNT_UNIT` | `.mount` unit matching the mount point |
| `SOURCES_DIR` | directory containing source definitions |
| `REMOTE_ROOT` | directory for committed snapshots on the target |
| `INCOMING_ROOT` | directory for uncommitted receives |
| `RETENTION_COUNT` | default number of remote snapshots; `0` means unlimited |
| `LOCAL_RETENTION_COUNT` | default number of local snapshots |
| `DAILY_LIMIT` | at most one successful backup per local day for unchanged configuration |
| `INCREMENTAL_REQUIRED` | refuse a full transfer when a remote chain exists without a common parent |
| `KEEP_FAILED_LOCAL_SNAPSHOT` | keep a local snapshot after an ordinary transfer error |
| `AUTO_EJECT` | automatically unmount and close LUKS |
| `MIN_TARGET_FREE_BYTES` | minimum free space required on the target |
| `MIN_LOCAL_FREE_BYTES` | minimum free space required for local snapshots |
| `STATE_DIR` | base state directory; per-profile state lives under `profiles/<PROFILE_ID>` |
| `STATUS_ROOT` | root directory for per-profile `current.json` runtime status |
| `HISTORY_ROOT` | root directory for per-profile run history JSON |
| `LOCK_FILE` | lock shared by backup, eject, and configurator operations |

A retention value of `0` disables automatic pruning for that scope.

If `PROFILE_ID` is missing, the runner uses `default`. Legacy files directly
under `STATE_DIR`, such as `last-success` and `pending-*`, are migrated to the
profile state directory on the next run.

To convert an existing legacy file into the preferred default profile:

```bash
sudo btrfs-backup-migrate-profile --profile default
```

The migrator creates canonical profile JSON, materializes
`/etc/btrfs-backup/profiles.d/default.env`, writes profile-local source
definitions, and keeps `/etc/btrfs-backup/backup.env` in place unless
`--remove-legacy` is used.

## JSON Schema

`config/profile.example.json` and `config/profile.schema.json` define a
versioned, machine-readable profile model for tooling.

The profile model mirrors the shell configuration: encrypted target identity,
state paths, retention settings, notification policy, and source definitions.
It is intended for generators, validators, and future migration tooling.

## Source Definitions

Each `/etc/btrfs-backup/profiles/<profile>/sources.d/*.conf` file describes
one source:

```bash
ENABLED=true
SOURCE_NAME=home
SOURCE_SUBVOLUME=/home
LOCAL_SNAPSHOT_DIR=/.snapshots/btrfs-backup/home
REMOTE_SUBDIR=home
SOURCE_RETENTION_COUNT=45
SOURCE_LOCAL_RETENTION_COUNT=20
```

`SOURCE_NAME` must be unique. `REMOTE_SUBDIR` is a relative path under `REMOTE_ROOT`. The local snapshot directory must be on the same Btrfs filesystem as the source. The source must not belong to the Btrfs filesystem used as the backup target. This check is based on filesystem UUIDs, so it still works when the same Btrfs filesystem is mounted at multiple locations.

For the `/` source, use a dedicated subvolume for snapshots, such as `/.snapshots`. Placing the snapshot repository in a regular directory inside the source subvolume creates unnecessary empty nested-subvolume mount points in future snapshots.

## Non-Interactive Answers

An example is available at `config/configurator-answers.example`. The file is sourced by Bash, so it must be private:

```bash
chmod 0600 answers.conf
btrfs-backup-configure \
  --answers ./answers.conf \
  --output-dir ./generated \
  --render-only
```

When the configurator is run through `sudo`, the answers file should still belong to the invoking user; the configurator checks `SUDO_UID`. This lets you prepare a private file as a normal user, review the render output, and then use the same file with `--apply`.

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
sudo systemctl reset-failed btrfs-backup.service
```

## udev Matching

The configurator builds match conditions from the LUKS UUID and, when available, node type, PARTUUID, and serial number. The goal is to match a specific partition, not every node on the same disk. With non-interactive input, the `ID_FS_UUID` condition must contain exactly the configured `BACKUP_LUKS_UUID`.

The rule does not execute scripts through `RUN+=`; it delegates work to systemd through `SYSTEMD_WANTS`.

## Incremental Chain Changes

With `INCREMENTAL_REQUIRED=true`, remote snapshots without a matching local parent UUID cause an error. This is the conservative behavior.

If all local parents were intentionally lost, you can temporarily set `INCREMENTAL_REQUIRED=false`, perform one full transfer, and then set it back to `true`.

## Output Directory Safety

The configurator removes the previous contents of `--output-dir`, so it rejects the repository root, template root, system directories, and any path containing active project configuration. The default root output directory, `/etc/btrfs-backup/generated`, is separate from active files.
