# TODO

## C++ Runtime Migration

- Wire the C++ target mount validator into the shadow runner/runtime path:
  - keep `target.btrfsUuid` required;
  - make synthetic mount-table tests provide filesystem UUIDs without weakening
    the production `libblkid` path;
  - keep Bash as the executor until this validator has parity with existing
    Bash integration tests.

- Add an asynchronous transfer execution layer before porting
  `btrfs send/receive`:
  - keep `ICommandRunner` limited to short synchronous administrative commands;
  - introduce a dedicated `TransferPipeline` or async process runner for
    long-running stream transfers;
  - support connecting producer and consumer processes without shell pipelines;
  - preserve backpressure between `btrfs send` and `btrfs receive`;
  - report byte progress and lifecycle events while the transfer is running;
  - support cancellation and cleanup of both processes;
  - surface separate send-side and receive-side exit status and diagnostics;
  - keep the interface independent enough to later back it with a GUI/event-loop
    process implementation.

- Move the main backup flow from Bash to C++ after parity tests pass:
  - keep Bash wrappers for mount/eject compatibility while needed;
  - keep existing integration tests as regression coverage;
  - remove Bash runtime code only after the C++ runner completes full,
    incremental, failure and recovery scenarios.

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

- Add `btrfs-backupctl repository discover`:
  - identify a backup repository from a mounted target path first;
  - later support block-device discovery;
  - report enough metadata to choose a restore source.

- Add `btrfs-backupctl repository verify`:
  - check readonly target snapshots, received UUIDs, parent chains, `.incoming`,
    orphaned snapshots, required local parents and free space;
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
  - validate file count, metadata and representative reads;
  - store the result in history for later diagnostics.

## Diagnostics And Future Hardening

- Add stable error codes for runtime, repository and restore failures.

- Add `btrfs-backupctl doctor`:
  - validate profile files, helper availability, mount state, target identity,
    free space, status/history paths and repository metadata;
  - make it useful before a user starts changing configuration.

- Add health checks for the target filesystem:
  - inspect scrub state and Btrfs device stats;
  - warn when integrity checks are stale or device counters show errors.

- Add calendar retention after count-based retention is fully ported:
  - support keep-last, daily, weekly, monthly and yearly policies;
  - add retention preview before destructive apply.

- Add support for multiple rotated targets:
  - model each target with independent state, catalog and retention;
  - keep one profile capable of tracking several backup media.
