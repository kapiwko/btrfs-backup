# TODO

## C++ Runtime Migration

- Harden the C++ runtime now that the main backup flow, target mount and target
  eject are native:
  - keep full, incremental and retention real Btrfs tests passing;
  - add broader failure tests for every backup phase;
  - expand interrupted-transfer, interruption-during-commit and
    interruption-after-commit recovery coverage;
  - keep status/history formats and error taxonomy documented;
  - add restore verification to the real Btrfs suite.

- Add an asynchronous process runner for long-running backup work:
  - build on the existing POSIX transfer pump that already passes program and
    arguments separately;
  - keep the existing POSIX runner for short administrative operations and
    tests;
  - add a later event-loop driven runner for richer progress, cancellation and
    multi-process coordination;
  - do not route `btrfs send` or `btrfs receive` through shell command strings.

- Improve transfer progress beyond the current native pump:
  - estimate total bytes and ETA without relying on `pv`;
  - expose source progress and aggregate run progress that does not reset
    between sources;
  - keep producer and consumer failures separately classified in stable error
    codes;
  - add cancellation tests for a live external transfer and `.incoming`
    recovery after cancellation.

- Add application-consistency hooks to the C++ run plan:
  - add hook timeout and cancellation handling with stable structured error
    codes;
  - make hook effects checkpoint-aware so pending recovery can distinguish
    failure before snapshot creation from failure after snapshot creation;
  - leave integration-specific helpers, such as PostgreSQL, MariaDB, libvirt,
    containers and virtual machines, as ordinary administrator-provided
    programs until a typed integration is justified.

- Add a backup request queue:
  - represent reasons such as `device-connected`, `manual`, `scheduled`,
    `pre-upgrade`, `stale-backup`, `restore-drill` and `retry-after-failure`;
  - merge concurrent requests for the same profile instead of starting
    competing runs;
  - persist pending requests so reconnect and retry behavior is deterministic.

- Add pre-upgrade local snapshots:
  - provide a package-manager hook that creates a readonly local snapshot before
    a system upgrade;
  - tag the snapshot with reason, package list and timestamp;
  - enqueue later transfer instead of blocking the upgrade on target presence;
  - retain pre-upgrade snapshots longer than ordinary automatic snapshots.

- Add power and resource policy to the runner:
  - optionally require AC power or a minimum battery percentage;
  - inhibit sleep during critical transfer and commit phases;
  - cancel or defer safely on critical battery;
  - support CPU and I/O weight configuration;
  - bound shutdown inhibition so the system is not blocked indefinitely.

- Preserve conservative dependency boundaries:
  - keep `btrfs send` and `btrfs receive` as `btrfs-progs` processes instead of
    implementing the send stream format;
  - use `libbtrfsutil` for subvolume metadata, snapshots, readonly state and
    subvolume deletion where available;
  - use `libmount` for mount inspection and safety checks, while systemd
    remains responsible for mount and cryptsetup units;
  - use `libblkid` for filesystem type, labels and UUID identity;
  - postpone `libcryptsetup` until the state machine, D-Bus/control API,
    cancellation and recovery behavior are stable.

## Repository And Restore Roadmap

- Add repository metadata on the target:
  - write `/btrfs-backup/repository.json`;
  - store repository id, format version, filesystem UUID, host id and known
    profiles;
  - make the backup medium self-describing enough for recovery without the
    original `/etc/btrfs-backup` directory.

- Add a snapshot catalog:
  - record profile id, source id, local UUID, received UUID, parent UUID,
    creation time, engine version and verification status;
  - use the catalog as an accelerator, not as the only source of truth;
  - provide repair/rebuild logic from actual Btrfs state.

- Add optional catalog signing:
  - support `catalog.json` plus `catalog.json.sig`;
  - use signatures to detect metadata tampering, missing catalog entries and
    divergence between history and catalog;
  - keep Btrfs state as the storage-level truth, not the signature alone.

- Add `btrfs-backupctl repository discover`:
  - identify a backup repository from a mounted target path first;
  - later support block-device discovery;
  - report enough metadata to choose a restore source.

- Add `btrfs-backupctl repository verify`:
  - check readonly target snapshots, received UUIDs, parent chains, `.incoming`,
    orphaned snapshots, required local parents and free space;
  - compare catalog metadata with actual Btrfs state;
  - include Btrfs device stats and scrub freshness when available;
  - return stable machine-readable errors;
  - write verification summaries to history.

- Add restore listing commands:
  - list sources and snapshots available for a profile or discovered repository;
  - browse a snapshot tree read-only;
  - find all versions of a file path across snapshots.

- Add file and directory restore commands:
  - support dry-run;
  - preserve ownership, permissions, ACL and xattr where possible;
  - refuse dangerous destinations unless explicitly overridden;
  - verify restored data after copy.

- Add subvolume restore:
  - restore a selected snapshot into a new destination subvolume;
  - never overwrite active `/` or `/home`;
  - document the manual bootloader/fstab steps separately from the data restore.

- Add periodic restore drill support:
  - restore a selected snapshot into a test location;
  - create a writable test snapshot when needed;
  - validate file count, ownership, ACL, xattr and representative reads;
  - optionally compare stored checksums;
  - store the result in history for later diagnostics;
  - clean up the drill environment.

## Configuration And Media Preparation

- Add a subvolume coverage analyzer:
  - scan mounted Btrfs layouts and classify subvolumes as backed up,
    intentionally ignored or probably missed;
  - suggest source entries for uncovered subvolumes;
  - store intentional exclusions explicitly in JSON.

- Add a destructive media preparation workflow:
  - identify device model, serial, size and current mount state;
  - reject likely system disks and mounted devices;
  - require typed confirmation before partitioning or formatting;
  - create partition table, LUKS2 container, Btrfs filesystem and label;
  - re-check target identity immediately before destructive writes;
  - finish with profile creation, trial backup and trial restore.

- Add unlock-method planning for LUKS2 targets:
  - keep root-owned keyfile and manual passphrase as baseline modes;
  - later support TPM2, FIDO2 and PKCS#11 enrollment where the platform
    supports it;
  - model unlock policy explicitly in profile/repository metadata.

## Retention And Multiple Targets

- Add calendar retention after count-based retention is fully ported:
  - support keep-last, daily, weekly, monthly and yearly policies;
  - add retention preview before destructive apply;
  - support snapshot pin, unpin and note metadata;
  - include minimum free-space driven pruning as a policy input.

- Add support for multiple rotated targets:
  - model each target with independent state, catalog and retention;
  - keep one profile capable of tracking several backup media;
  - store per-target last success, incremental chain, health state, connection
    history and udev identity;
  - make freshness warnings target-aware.

- Add support for multiple hosts on one repository:
  - distinguish `hostId`, `profileId` and `sourceId`;
  - prevent two hosts from accidentally writing to the same namespace;
  - make repository discovery and restore work without the original host.

## Transfer Improvements

- Add transfer estimation and preview:
  - estimate changed, new and deleted paths before large transfers where
    feasible;
  - estimate bytes, progress, speed and ETA;
  - show per-source contribution to a run;
  - warn when a full transfer is unexpectedly required or unusually large.

- Add automatic send protocol selection:
  - detect kernel and `btrfs-progs` support before enabling send v2;
  - support `btrfs send --proto 2 --compressed-data` when compatible;
  - keep `auto` as the default policy and fall back safely;
  - add optional transfer rate limiting.

- Add a transport abstraction after the local runner is stable:
  - keep local encrypted removable targets as the first implementation;
  - later support local mounted targets, SSH-backed Btrfs targets and stream
    archive targets;
  - for SSH, use a constrained remote helper that receives into `.incoming`,
    verifies, commits, catalogs and prunes without arbitrary shell execution.

## Diagnostics And Future Hardening

- Add stable error codes for runtime, repository and restore failures:
  - keep status/history fields structured with `errorCode`, `errorMessage`,
    `details`, `recoverable` and `suggestedAction`;
  - build a documented taxonomy for target, source, transfer, hook, repository,
    restore, retention, power and configuration failures.

- Add `btrfs-backupctl doctor`:
  - validate profile files, helper availability, mount state, target identity,
    free space, status/history paths and repository metadata;
  - include sanitized profile, unit state, selected journal lines, recent
    history and health counters in diagnostics;
  - make it useful before a user starts changing configuration;
  - avoid collecting secrets, passwords or private key material.

- Add health checks for the target filesystem:
  - inspect scrub state and Btrfs device stats;
  - track `write_errs`, `read_errs`, `flush_errs`, `corruption_errs` and
    `generation_errs`;
  - warn when integrity checks are stale or device counters show errors;
  - optionally integrate SMART/NVMe health data when `smartmontools` is
    installed.

- Add report export:
  - provide `btrfs-backupctl report --profile ... --since ... --format ...`;
  - support machine-readable JSON first, then HTML if useful;
  - include successes, failures, durations, transferred bytes, scrub results,
    backup age, restore drill results, media state and open warnings.

- Add QEMU system tests with real hotplug:
  - boot a minimal Arch Linux guest with systemd and udev;
  - attach a virtual USB target disk dynamically instead of pre-mounting it;
  - verify udev-triggered service startup, full transfer, incremental transfer,
    dynamic detach, reconnect and pending-state recovery;
  - cover failures that mocks and Docker cannot model cleanly: killed
    `btrfs send`, killed `btrfs receive`, ENOSPC, read-only target remount,
    mapper loss, corrupted active JSON, stale pending marker, interruption
    during commit, interruption after commit before history, and suspend during
    transfer;
  - make this an explicit system-test target, separate from the default suite.

- Add fuzzing, sanitizers and broader CI:
  - fuzz profile JSON parsing, identifier validation, path canonicalization,
    symlink escape detection, legacy import, external command result parsing,
    status/history parsing and repository catalog parsing;
  - run ASan, UBSan, clang-tidy, clang-format and coverage checks;
  - test with GCC, Clang and multiple `btrfs-progs` versions where practical.

## System API And Desktop Integration

- Add a stable system API for status and control:
  - expose profile status, history, freshness, progress, cancellation,
    safe-removal state and suggested recovery actions;
  - avoid making desktop tools parse journal text or private state files;
  - keep status messages translatable by relying on stable codes and structured
    details.

- Add a system daemon after the C++ engine boundary is stable:
  - own active run state, history updates, cancellation, privileged operations
    and communication with systemd;
  - expose a versioned system D-Bus API for profile listing, status, current
    run, history, validation, start, cancel, eject, save and delete operations;
  - require authorization for mutating or privileged operations while allowing
    unprivileged reads of status and history;
  - treat `/run/btrfs-backup` and history JSON as recovery/fallback state, not
    as the primary live communication channel when the daemon is active;
  - recover visible run state after daemon restart by reading current status and
    history files;
  - expose safe-removal state after eject so clients can distinguish a finished
    backup from a target that is safe to disconnect;
  - use polkit for daemon authorization rather than a short-lived privileged
    helper model;
  - consider `libsystemd` only for `sd_notify`, watchdog or structured journal
    needs, not as a competing application D-Bus layer.

- Version the system control API:
  - expose capabilities covering API version, profile schema version, status
    schema version and optional features;
  - make clients check capabilities before interpreting unknown formats;
  - document read-only operations separately from operations that require
    authorization.

- Add privileged action safety tests:
  - verify that start, force, validate, cancel and eject actions re-check the
    profile id and active unit state server-side;
  - reject eject while a backup unit is active unless an explicit safe path
    proves the target is idle;
  - reject starting conflicting profile units for the same target or lock;
  - ensure cancellation requests stop the system unit or runner transaction
    without killing unrelated processes.

- Add backup freshness policy:
  - configure warning and critical age thresholds;
  - classify current data safety from last success, last failure, target
    absence, restore drill freshness and repository health;
  - make the policy available to status, reports and future UI clients.

- Add history and statistics views through CLI/API first:
  - expose success/failure counts, durations, transferred bytes, average speed,
    full vs incremental ratio, data growth, backup age, error frequency, scrub
    results and predicted target fill time.

- Add read-only previous-version browsing:
  - support opening a snapshot location for a selected file or directory;
  - provide restore-as behavior rather than overwriting live data by default;
  - keep the underlying restore commands usable without a graphical client.

- Add quick commands for status, manual backup, eject, history and restore:
  - route them through the same stable control API as other clients;
  - avoid launching competing backup processes by using the request queue.

## Security And Migration Completion

- Preserve explicit trust boundaries:
  - runner and future system manager may run privileged;
  - ordinary CLI invocations and user-facing clients must not write directly to
    `/etc/btrfs-backup`;
  - profile JSON remains root-owned configuration with strict permissions;
  - configuration is data, not executable code.

- Harden systemd units only with integration coverage:
  - evaluate `CapabilityBoundingSet`, `ProtectSystem`, `ProtectHome`,
    `PrivateTmp`, `NoNewPrivileges`, `RestrictAddressFamilies`,
    `SystemCallFilter` and `DeviceAllow`;
  - add each hardening option only after real mount, cryptsetup and Btrfs tests
    prove it does not break supported workflows.

- Define migration completion:
  - backup starts from device events without a logged-in user;
  - base package remains independent from any graphical session;
  - canonical profile JSON is the only runtime configuration source;
  - active backup survives user logout and manager/client restarts;
  - progress is reported as exact, estimated or indeterminate without false
    precision;
  - interrupted runs are recoverable and documented;
  - restore has been executed and documented on real Btrfs;
  - API, profile, status and history formats are documented and versioned.
