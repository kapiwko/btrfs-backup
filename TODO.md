# TODO

## C++ Runtime Migration

- Move the main backup flow from Bash to C++ after parity tests pass:
  - keep Bash wrappers for mount/eject compatibility while needed;
  - keep existing integration tests as regression coverage;
  - remove Bash runtime code only after the C++ runner completes full,
    incremental, failure and recovery scenarios.

- Add application-consistency hooks to the C++ run plan:
  - support controlled `beforeSnapshot` and `afterSnapshot` hook phases;
  - model each hook as an explicit executable path plus an argument array;
  - never execute arbitrary text through a shell;
  - record hook start, success, failure, timeout and cancellation as stable
    runner events with structured error codes;
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
