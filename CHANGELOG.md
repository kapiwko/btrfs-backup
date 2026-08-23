# Changelog

## 2.0.0 - unreleased

1. removed runtime fallback to legacy source and main configuration files;
2. stopped generating legacy source configuration files for profile render/save;
3. `btrfs-backupctl list-profiles` now lists canonical profile JSON storage;
4. started replacing Python runtime tooling with C++ by porting profile commands
   to native `btrfs-backupctl profile` code under the `cpp/` source tree;
5. package builds now compile and install the native control helper;
6. configurator and profile migrator now create canonical profile JSON through
   the native helper instead of embedded Python snippets;
7. generated runtime packages no longer depend on Python and the old Python
   profile helper has been removed;
8. profile migration is now handled natively by `btrfs-backupctl migrate-profile`,
   and the standalone migration wrapper and shell implementation have been
   removed;
9. backup, mount, and eject runtime entrypoints now load profile JSON as their
   only profile configuration source;
10. profile render, save, migration, and configurator flows no longer generate
    `profiles.d/*.env` files;
11. `btrfs-backupctl profile create` now builds canonical profile JSON directly,
    replacing the old environment-and-TSV based profile compose path.

## 1.1.0 - 2026-08-23

1. profile-aware configuration loading from `/etc/btrfs-backup/profiles.d/<profile>.env`;
2. `--profile` support for backup, mount, and eject commands;
3. compatibility fallback to `/etc/btrfs-backup/backup.env` for the `default` profile;
4. `btrfs-backup-migrate-profile` for converting an existing legacy configuration and source definitions into canonical profile JSON and runtime profile files;
5. `btrfs-backupctl` for status, history, and watch access to the file-based status API;
6. `btrfs-backup-mount` for mounting and validating the configured backup target without starting a backup;
7. profile JSON examples and schema for future tooling;
8. package contents updated for all generated packaging backends;
9. configurator renders and installs profile files and a templated systemd unit;
10. udev starts the profile-specific systemd unit;
11. `btrfs-backup-profile list` lists profile files;
12. `btrfs-backup-migrate-profile --remove-legacy` moves the legacy configuration, source directory, and old udev rule aside after migration;
13. profile tooling validates canonical JSON profiles and materializes runtime `.env`, source, udev, and public manifest files;
14. `btrfs-backup-configure` now renders canonical `profile.json` first, then materializes the runtime profile files from that JSON;
15. profile `show` and `export` can read the active canonical profile or reconstruct it from runtime profile files;
16. `docs/engine-contract.md` defines the stable profile, status, history, phase, and recovery contract for future engine implementations;
17. installable packages include the profile systemd template unit used by udev;
18. package install and upgrade hooks reload systemd and udev rules without triggering devices;
19. status and history JSON are readable by unprivileged local users while private recovery state remains root-only;
20. `btrfs-backupctl status` falls back to the last history entry after the oneshot service exits;
21. `btrfs-backupctl history` returns `[]` when no history exists yet and renders cleaner JSON arrays.

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
