# Changelog

## 0.2.2 - 2026-08-24

1. native runner execution now uses separate non-blocking profile and target
   locks, preventing concurrent profiles from manipulating the same LUKS backup
   repository and preventing mount/eject races;
2. synchronous commands and asynchronous transfer processes now use a shared
   `posix_spawnp()` adapter instead of running C++ allocation and setup code
   between `fork()` and `exec()` in a multithreaded runner;
3. added concurrency, partial process-start, missing executable, lock symlink,
   and worker-thread process-spawn regression coverage;
4. the root `VERSION` file is now the single version source for native and
   Plasma builds, generated package metadata, and release artifacts;
5. aggregate progress now includes the fractional progress of the active source
   and remains monotonic through its post-transfer actions;
6. live transfer speed now uses a three-second EWMA and status updates are
   interval-limited instead of requiring one durable write per splice;
7. run-status schema version 2 removes `safeToRemove`; the Plasma widget no
   longer presents backup completion as target eject state;
8. the Plasma backend now names its CLI process state `watcherConnected`,
   reserving target connectivity for the future system backend.
9. failed verification of a committed snapshot now reports cleanup failures as
   `repository.recovery_required`; the pending marker retains the exact final
   path so the next run removes the unverified canonical snapshot explicitly.

## 0.2.1 - 2026-08-23

1. transfer status now includes byte counters, speed, ETA, current-source
   progress, aggregate run progress and progress accuracy fields;
2. transfer execution now runs through asynchronous handles so the executor can
   observe progress and cancellation without sleep-based polling;
3. runner cancellation requests are now exposed through
   `btrfs-backupctl runner cancel --profile <id>` and handled by the active
   transfer pipeline;
4. transfer failures now use side-specific stable error codes for producer,
   consumer and combined send/receive failures;
5. aggregate progress no longer resets between sources, and the runtime can
   estimate transfer totals from the local snapshot when exact send-stream
   totals are not available;
6. cancellation wakes the transfer event loop immediately instead of waiting for
   the next process-status poll;
7. `btrfs-backupctl status watch --profile <id> --json` now validates emitted
   JSON against the documented status API before writing it to stdout;
8. added the initial Plasma 6 status plasmoid backed by a C++
   `BackupStatusModel` reading the public JSON status stream;
9. added QML runtime smoke coverage for loading and instantiating the compiled
   `org.btrfsbackup.plasma` module;
10. added an optional `btrfs-backup-kde` Arch package for the Plasma status
    widget and its compiled QML backend;
11. kept the base `btrfs-backup` package independent from Qt, Kirigami and
    Plasma runtime dependencies;
12. installed the compiled Plasma QML module under the Qt 6 import path
    `/usr/lib/qt6/qml`;
13. added KDE package install hooks that refresh the service cache with
    `kbuildsycoca6` when available.

## 0.2.0 - 2026-08-23

1. removed runtime fallback to legacy source and main configuration files;
2. stopped generating legacy source configuration files for profile render/save;
3. `btrfs-backupctl profile list` now lists canonical profile JSON storage;
4. started replacing Python runtime tooling with C++ by porting profile commands
   to native `btrfs-backupctl profile` code under the `cpp/` source tree;
5. package builds now compile and install the native control helper;
6. profile wizard now creates canonical profile JSON through the native helper
   instead of embedded Python snippets;
7. generated runtime packages no longer depend on Python and the old Python
   profile helper has been removed;
8. legacy profile migration entrypoints and helpers have been removed;
9. backup, mount, and eject runtime entrypoints now load profile JSON as their
   only profile configuration source;
10. profile render, save, and wizard flows no longer generate `profiles.d/*.env`
    files;
11. `btrfs-backupctl profile create` now builds canonical profile JSON directly,
    replacing the old environment-and-TSV based profile compose path;
12. `btrfs-backupctl installation validate` now owns rendered and active
    installation validation that used to live in the shell configurator;
13. `btrfs-backupctl installation render` now renders systemd, fstab, and
    crypttab installation files that used to be templated by the shell
    configurator;
14. `btrfs-backupctl` commands are grouped by area, including `profile list`,
    `status show`, `status history`, `target mount`, `target eject`, and
    internal `state ...` runtime commands;
15. the standalone `btrfs-backup-configure` entrypoint has been removed; the
    interactive setup flow is now `btrfs-backupctl profile wizard`.
16. the main backup runner is now native C++ and performs full and incremental
    Btrfs send/receive, pending-state recovery, target validation, status and
    history updates, and local/remote retention without the legacy Bash runtime;
17. `btrfs-backup` is now a thin launcher for the native C++ entrypoint, and
    its option handling no longer shells out to `btrfs-backupctl` or parses
    profile JSON with text tools;
18. standalone `btrfs-backup-mount`, `btrfs-backup-eject`, and deprecated
    `btrfs-backup-unplug` commands have been removed; use
    `btrfs-backupctl target mount` and `btrfs-backupctl target eject`;
19. generated systemd units now run `btrfs-backupctl target eject` directly in
    `ExecStopPost`;
20. target mount inspection uses `libmount` and Btrfs filesystem UUID identity
    from `libblkid`;
21. Btrfs snapshot metadata, readonly checks, snapshot creation/deletion, and
    subvolume receive verification use `libbtrfsutil` where applicable;
22. real Btrfs integration coverage now verifies package installation, target
    validation, full transfer, incremental transfer, mismatch rejection,
    source-on-target rejection, retention, and `.incoming` cleanup;
23. release packaging installs only the public `btrfs-backup` and
    `btrfs-backupctl` commands plus native private binaries under
    `/usr/lib/btrfs-backup`.

## 0.1.1 - 2026-08-23

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
