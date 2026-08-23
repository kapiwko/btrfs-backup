# Changelog

## 0.1.1 - 2026-08-23

1. profile-aware configuration loading from `/etc/btrfs-backup/profiles.d/<profile>.env`;
2. `--profile` support for backup, mount, and eject commands;
3. compatibility fallback to `/etc/btrfs-backup/backup.env` for the `default` profile;
4. `btrfs-backup-migrate-profile` for converting an existing legacy configuration and source definitions into canonical profile JSON and runtime profile files;
5. `btrfs-backupctl` for status, history, and watch access to the file-based status API;
6. `btrfs-backup-mount` for mounting and validating the configured backup target without starting a backup;
7. profile JSON examples and schema for future tooling;
8. package contents updated for all generated packaging backends.
9. configurator renders and installs profile files and a templated systemd unit;
10. udev starts the profile-specific systemd unit;
11. `btrfs-backupctl list-profiles` lists profile files and the legacy fallback;
12. `btrfs-backup-migrate-profile --remove-legacy` moves the legacy file aside after migration.
13. `btrfs-backup-profile` validates canonical JSON profiles and materializes runtime `.env`, source, udev, and public manifest files;
14. `btrfs-backup-configure` now renders canonical `profile.json` first, then materializes the runtime profile files from that JSON;
15. `btrfs-backup-profile show` and `export` can read the active canonical profile or reconstruct it from runtime profile files.

The legacy `/etc/btrfs-backup/backup.env` fallback is deprecated in 0.1.1 and
will be removed in 0.2. Use `btrfs-backup-migrate-profile --profile default`
to create `/etc/btrfs-backup/profiles.d/default.env`.

## 0.1.0 - 2026-08-22

1. multi-source backup through `sources.d`;
2. read-only snapshots, `.incoming` receives, and verified same-filesystem commits;
3. incremental parents verified by UUID;
4. `Received UUID` checks after receive;
5. daily limit based on target UUID and configuration fingerprint;
6. pending-state recovery after interruptions;
7. separate local and remote retention;
8. explicit `btrfs-backup-eject` command;
9. udev startup through systemd without a device-removal handler;
10. CLI configurator with render, apply, and validation modes;
11. deterministic Arch package and test suite.
