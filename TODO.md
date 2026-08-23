# TODO

## C++ Runtime Migration

- Add command execution adapters for the runner:
  - introduce testable interfaces for process execution and filesystem effects;
  - run external commands without shell parsing;
  - keep `btrfs send` and `btrfs receive` as external programs in the first C++
    implementation.

- Port snapshot operations behind an interface:
  - check subvolume existence and readonly state;
  - read UUID and received UUID;
  - create readonly local snapshots;
  - delete snapshots;
  - keep a fake implementation for unit tests and Bash compatibility tests for
    behavior.

- Port receive-to-incoming and commit logic:
  - receive into `.incoming`;
  - verify received UUID against the local snapshot UUID;
  - atomically commit on the same filesystem;
  - preserve the existing recovery guarantees after interrupted runs.

- Add a C++ runner entrypoint in shadow mode:
  - build the plan from the real profile and filesystem state;
  - print or write the planned actions;
  - compare against current Bash runtime behavior in tests before enabling it as
    the default executor.

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
