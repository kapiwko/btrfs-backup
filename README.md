# btrfs-backup

Unattended backup of Btrfs subvolumes to an encrypted removable disk. The project combines read-only snapshots, `btrfs send/receive`, incremental transfers, systemd, udev, retention, and controlled unmounting with LUKS closure.

## Key Features

1. multiple backup sources through separate profile source files;
2. full and incremental `btrfs send/receive`, with the incremental parent selected by UUID rather than by name;
3. receives into `.incoming`, verifies `Received UUID`, then commits a read-only target snapshot;
4. `pending` markers so the next run can resolve interrupted transfers or power loss;
5. at most one successful backup per local day for the same target UUID and configuration fingerprint;
6. independent local and remote retention, globally configured or overridden per source;
7. `flock` locking, target LUKS validation, mapper validation, source/target separation, symlink escape checks, and free-space checks;
8. automatic startup only when the exact configured LUKS partition appears;
9. automatic `sync`, unmount, and LUKS closure after the service finishes;
10. CLI configurator with render, install, and validation modes.

## Requirements

The project primarily targets Arch Linux and derivatives. It requires Bash, Btrfs, systemd, udev, cryptsetup, and tools from `coreutils`, `findutils`, and `util-linux`. `pv` and `libnotify` are optional.

Each source must be a Btrfs subvolume. The local snapshot directory must be on the same Btrfs filesystem as its source. The target must be a separate Btrfs filesystem inside LUKS; the script rejects a source that belongs to the same filesystem as the target, even if it is available through another mount point.

## Arch Package Installation

```bash
sudo pacman -U btrfs-backup-1.1.0-1-any.pkg.tar.zst
```

The package does not enable the service at boot and does not create active configuration without an explicit user action.

## First Configuration

The safest flow is to render files into a normal directory first and review them:

```bash
btrfs-backup-configure \
  --render-only \
  --output-dir "$PWD/generated"
```

The configurator detects connected LUKS devices and mounted Btrfs sources. Installation mode requires root:

```bash
sudo btrfs-backup-configure --apply
```

`--apply` installs:

```text
/etc/btrfs-backup/backup.env
/etc/btrfs-backup/profiles.d/default.env
/etc/btrfs-backup/profiles/default/profile.json
/etc/btrfs-backup/profiles/default/sources.d/*.conf
/var/lib/btrfs-backup/public/profiles/default.json
/etc/systemd/system/btrfs-backup.service
/etc/systemd/system/btrfs-backup@.service
/etc/udev/rules.d/99-btrfs-backup.rules
```

The configurator intentionally does not edit `/etc/crypttab` or `/etc/fstab` automatically. Merge the generated fragments into those files, then run:

```bash
sudo systemctl daemon-reload
sudo udevadm control --reload
sudo btrfs-backup --validate
```

If the configuration does not use a keyfile, systemd may ask for the LUKS passphrase through ask-password. Fully unattended operation requires a properly protected keyfile or another non-interactive mechanism supported by crypttab.

## Usage

After installation and after merging the generated fragments, do not run `systemctl enable btrfs-backup.service`. The unit has no `[Install]` section; udev starts it when the exact configured device appears.

Manual commands:

```bash
sudo btrfs-backup
sudo btrfs-backup --force
sudo btrfs-backup --validate
sudo btrfs-backup --no-eject
sudo btrfs-backup --profile default --validate
sudo btrfs-backup-mount
sudo btrfs-backup-eject
sudo btrfs-backup-migrate-profile --profile default
btrfs-backup-profile validate --file profile.json
btrfs-backup-profile render --file profile.json --output-dir ./generated-profile
btrfs-backupctl status --profile default --human
btrfs-backupctl history --profile default --limit 10
btrfs-backupctl list-profiles
```

Logs:

```bash
journalctl -u btrfs-backup.service
journalctl -u btrfs-backup.service -f
```

## Configuration Layout

The canonical format for tools is JSON. `btrfs-backup-configure` writes `profile.json` first and uses `btrfs-backup-profile save` to materialize the trusted runtime files consumed by the backup runner.

Each runtime source has a separate file under `/etc/btrfs-backup/profiles/<profile>/sources.d`:

```bash
ENABLED=true
SOURCE_NAME=root
SOURCE_SUBVOLUME=/
LOCAL_SNAPSHOT_DIR=/.snapshots/btrfs-backup/root
REMOTE_SUBDIR=root
SOURCE_RETENTION_COUNT=30
SOURCE_LOCAL_RETENTION_COUNT=30
```

Active runtime configuration files are trusted Bash code executed as root. They must be owned by root and use mode `0600`; the script refuses files that are accessible by group or other users.

Profiles can also be stored directly as `/etc/btrfs-backup/profiles.d/<profile>.env` and selected with `--profile <profile>` or `BTRFS_BACKUP_PROFILE=<profile>`. The legacy `/etc/btrfs-backup/backup.env` remains supported as the fallback for the `default` profile in 1.x, but it is deprecated and will be removed in 2.0. To create the default profile file from an existing legacy configuration:

```bash
sudo btrfs-backup-migrate-profile --profile default
```

## Recovery

Target snapshots remain read-only. You can copy individual files from them or send a whole snapshot to another Btrfs filesystem. See [docs/recovery.md](docs/recovery.md) for the detailed procedure.

Backups should be checked regularly with a restore test. A successful transfer exit code is not a substitute for proving that data can be recovered.

## Documentation

1. [architecture and runtime flow](docs/architecture.md);
2. [configuration](docs/configuration.md);
3. [migration to profiles](docs/migration-to-profiles.md);
4. [recovery](docs/recovery.md);
5. [testing](docs/testing.md);
6. [status API](docs/status-api.md);
7. [release and packaging](docs/packaging.md).

## Security and Limits

The project verifies the target device at several levels, but it is not a replacement for a 3-2-1 backup strategy. A disk connected only during backups reduces exposure, but it does not protect against every hardware failure, theft, administrator mistake, or corruption of both copies.

Automated tests use controlled mocks for system commands. Before production use, run a test on real Btrfs filesystems and perform a restore drill.

## License

GPL-3.0-or-later. The full text is in [LICENSE](LICENSE).
