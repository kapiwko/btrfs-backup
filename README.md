# btrfs-backup

Unattended backup of Btrfs subvolumes to an encrypted removable disk. The project combines read-only snapshots, `btrfs send/receive`, incremental transfers, systemd, udev, retention, and controlled unmounting with LUKS closure.

## Key Features

1. multiple backup sources defined in one profile JSON document;
2. full and incremental `btrfs send/receive`, with the incremental parent selected by UUID rather than by name;
3. receives into `.incoming`, verifies `Received UUID`, then commits a read-only target snapshot;
4. `pending` markers so the next run can resolve interrupted transfers or power loss;
5. at most one successful backup per local day for the same target UUID and configuration fingerprint;
6. independent local and remote retention, globally configured or overridden per source;
7. `flock` locking, target LUKS validation, mapper validation, source/target separation, symlink escape checks, and free-space checks;
8. automatic startup only when the exact configured LUKS partition appears;
9. automatic `sync`, unmount, and LUKS closure after the service finishes;
10. native CLI tooling for profile wizards, rendering, installation files, and validation.

## Requirements

The project primarily targets Arch Linux and derivatives. It requires Bash,
Btrfs, systemd, udev, cryptsetup, and tools from `coreutils`, `findutils`, and
`util-linux`. `pv` and `libnotify` are optional runtime integrations.

Source builds require:

- CMake 3.20 or newer;
- a C++20 compiler;
- `pkgconf` / `pkg-config`;
- `nlohmann-json`;
- `libmount` development files from `util-linux`;
- `libudev` development files from `systemd`;
- `libbtrfsutil` development files from `btrfs-progs`.

On Arch Linux, the build dependencies are provided by packages such as
`base-devel`, `cmake`, `pkgconf`, `nlohmann-json`, `util-linux`, `systemd`, and
`btrfs-progs`.

Each source must be a Btrfs subvolume. The local snapshot directory must be on the same Btrfs filesystem as its source. The target must be a separate Btrfs filesystem inside LUKS; the script rejects a source that belongs to the same filesystem as the target, even if it is available through another mount point.

## Arch Package Installation

```bash
sudo pacman -U btrfs-backup-0.2.0-1-x86_64.pkg.tar.zst
```

The package installs the systemd template unit used by udev, but it does not
enable a service at boot and does not create active backup configuration without
an explicit user action.

## First Configuration

The safest flow is to render files into a normal directory first and review them:

```bash
btrfs-backupctl profile wizard \
  --render-only \
  --output-dir "$PWD/generated"
```

The wizard detects connected LUKS devices and mounted Btrfs sources. Apply mode requires root:

```bash
sudo btrfs-backupctl profile wizard --apply
```

`--apply` installs:

```text
/etc/btrfs-backup/profiles/default/profile.json
/var/lib/btrfs-backup/public/profiles/default.json
/etc/systemd/system/btrfs-backup.service
/etc/systemd/system/btrfs-backup@.service
/etc/udev/rules.d/99-btrfs-backup-default.rules
```

The wizard intentionally does not edit `/etc/crypttab` or `/etc/fstab` automatically. Merge the generated fragments into those files, then run:

```bash
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
sudo btrfs-backup --validate
```

If the configuration does not use a keyfile, systemd may ask for the LUKS passphrase through ask-password. Fully unattended operation requires a properly protected keyfile or another non-interactive mechanism supported by crypttab.

## Usage

After installation and after merging the generated fragments, do not run `systemctl enable btrfs-backup.service` or `systemctl enable btrfs-backup@default.service`. The units have no `[Install]` section; udev starts the profile instance when the exact configured device appears.

Manual commands:

```bash
sudo btrfs-backup
sudo btrfs-backup --force
sudo btrfs-backup --validate
sudo btrfs-backup --no-eject
sudo btrfs-backup --profile default --validate
sudo btrfs-backup-mount
sudo btrfs-backup-eject
btrfs-backupctl profile validate --file profile.json
btrfs-backupctl profile render --file profile.json --output-dir ./generated-profile
btrfs-backupctl profile show --profile default
btrfs-backupctl profile export --profile default --output profile.json
btrfs-backupctl status show --profile default --human
btrfs-backupctl status history --profile default --limit 10
btrfs-backupctl profile list
```

Logs:

```bash
journalctl -u btrfs-backup@default.service
journalctl -u btrfs-backup@default.service -f
```

## Configuration Layout

The canonical format for tools and source definitions is JSON.
`btrfs-backupctl profile wizard` uses the native profile model to write
`profile.json` and materialize the trusted runtime files consumed by the backup
runner.

Active runtime profile JSON is trusted root-owned configuration. It must use
mode `0600`; the script refuses files that are accessible by group or other
users.

Profiles are selected with `--profile <profile>` or `BTRFS_BACKUP_PROFILE=<profile>`.

## Recovery

Target snapshots remain read-only. You can copy individual files from them or send a whole snapshot to another Btrfs filesystem. See [docs/recovery.md](docs/recovery.md) for the detailed procedure.

Backups should be checked regularly with a restore test. A successful transfer exit code is not a substitute for proving that data can be recovered.

## Documentation

1. [architecture and runtime flow](docs/architecture.md);
2. [configuration](docs/configuration.md);
3. [recovery](docs/recovery.md);
4. [testing](docs/testing.md);
5. [status API](docs/status-api.md);
6. [engine contract](docs/engine-contract.md);
7. [C++ source layout](docs/cpp-layout.md);
8. [release and packaging](docs/packaging.md).

## Security and Limits

The project verifies the target device at several levels, but it is not a replacement for a 3-2-1 backup strategy. A disk connected only during backups reduces exposure, but it does not protect against every hardware failure, theft, administrator mistake, or corruption of both copies.

Automated tests use controlled mocks for system commands. Before production use, run a test on real Btrfs filesystems and perform a restore drill.

## License

GPL-0.3-or-later. The full text is in [LICENSE](LICENSE).
