# btrfs-backup

Unattended backup of Btrfs subvolumes to an encrypted removable disk. The project combines read-only snapshots, `btrfs send/receive`, incremental transfers, systemd, udev, retention, and controlled unmounting with LUKS closure.

## Key Features

1. multiple backup sources through separate `/etc/btrfs-backup/sources.d/*.conf` files;
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
sudo pacman -U btrfs-backup-1.0.0-1-any.pkg.tar.zst
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
/etc/btrfs-backup/sources.d/*.conf
/etc/systemd/system/btrfs-backup.service
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
sudo btrfs-backup-eject
```

Logs:

```bash
journalctl -u btrfs-backup.service
journalctl -u btrfs-backup.service -f
```

## Configuration Layout

The main file describes the target, runtime policy, and state paths. Each source has a separate file:

```bash
ENABLED=true
SOURCE_NAME=root
SOURCE_SUBVOLUME=/
LOCAL_SNAPSHOT_DIR=/.snapshots/btrfs-backup/root
REMOTE_SUBDIR=root
SOURCE_RETENTION_COUNT=30
SOURCE_LOCAL_RETENTION_COUNT=30
```

Active configuration files are trusted Bash code executed as root. They must be owned by root and use mode `0600`; the script refuses files that are accessible by group or other users.

## Recovery

Target snapshots remain read-only. You can copy individual files from them or send a whole snapshot to another Btrfs filesystem. See [docs/recovery.md](docs/recovery.md) for the detailed procedure.

Backups should be checked regularly with a restore test. A successful transfer exit code is not a substitute for proving that data can be recovered.

## Documentation

1. [architecture and runtime flow](docs/architecture.md);
2. [configuration](docs/configuration.md);
3. [recovery](docs/recovery.md);
4. [testing](docs/testing.md);
5. [release and packaging](docs/packaging.md).

## Security and Limits

The project verifies the target device at several levels, but it is not a replacement for a 3-2-1 backup strategy. A disk connected only during backups reduces exposure, but it does not protect against every hardware failure, theft, administrator mistake, or corruption of both copies.

Automated tests use controlled mocks for system commands. Before production use, run a test on real Btrfs filesystems and perform a restore drill.

## License

GPL-3.0-or-later. The full text is in [LICENSE](LICENSE).
