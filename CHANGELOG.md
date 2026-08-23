# Changelog

## 1.1.0 - 2026-08-23

1. profile-aware configuration loading from `/etc/btrfs-backup/profiles.d/<profile>.env`;
2. `--profile` support for backup, mount, and eject commands;
3. compatibility fallback to `/etc/btrfs-backup/backup.env` for the `default` profile;
4. `btrfs-backup-migrate-profile` for creating `profiles.d/default.env` from an existing legacy configuration;
5. `btrfs-backupctl` for status, history, and watch access to the file-based status API;
6. `btrfs-backup-mount` for mounting and validating the configured backup target without starting a backup;
7. profile JSON examples and schema for future tooling;
8. package contents updated for all generated packaging backends.

The legacy `/etc/btrfs-backup/backup.env` fallback is deprecated in 1.1 and
will be removed in 2.0. Use `btrfs-backup-migrate-profile --profile default`
to create `/etc/btrfs-backup/profiles.d/default.env`.

## 1.0.0 - 2026-08-22

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
