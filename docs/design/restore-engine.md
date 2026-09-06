# Restore Engine

Status: implemented for repository format v1 and local POSIX/Btrfs restores.

## Goals

Restore must be a first-class, CLI-usable workflow rather than a desktop-only
feature. It must work from a discovered repository when the original machine,
profile directory and manager are unavailable.

## Operations

The engine should support these layers:

1. discover a repository and validate its format;
2. list hosts, profiles, sources, captures and snapshots;
3. browse a selected snapshot read-only;
4. find versions of a relative path across snapshots;
5. restore files or directories to a new destination;
6. restore a snapshot into a new subvolume;
7. run and record a disposable restore drill.

The core request/result API is independent of CLI, D-Bus and KDE. Those
adapters expose the same operation and stable error codes.

The base package exposes the engine through `btrfs-backupctl restore` with
`catalog`, `list`, `versions`, `plan`, `execute` and `drill` commands. Discovery
starts only from an already mounted repository path and never activates or
mounts a target. `plan` performs no mutation. The POSIX backend rejects
symlinks, special files and nested mount boundaries and preserves mode,
ownership when privileged, timestamps and extended attributes (including POSIX
ACL attributes) before publishing the staging tree.

## Safety Rules

- Dry-run is available before every copying or subvolume-creation operation.
- Relative source paths cannot escape the selected snapshot.
- Destination traversal rejects symlinks and unexpected mount boundaries.
- Active `/`, `/home` and configured source subvolumes are never overwritten.
- Existing destination data is preserved unless a narrowly scoped overwrite
  policy is explicitly selected.
- Ownership, mode, ACL and xattr preservation failures are reported, not hidden.
- File data is copied with checked `open`, `read`, `write`, `fsync` and `close`
  calls so a late `ENOSPC` is reported as insufficient space.
- Before copying, the engine walks the selected entry to estimate required
  destination space and compares it with the currently available space. This
  is an estimate rather than a reservation: concurrent writes can still exhaust
  the filesystem, so every data and metadata write remains authoritative.
- Restore never applies retention or modifies the source snapshot.

Whole-system recovery remains a guided administrative procedure: the engine
restores data into a new subvolume and reports required fstab/bootloader steps,
but does not rewrite the boot setup automatically.

## Verification

File restore verifies copied metadata and content when a reliable comparison is
available. A restore drill records selected snapshot identity, file counts,
representative reads, metadata checks, duration, cleanup result and final
status. Optional stored checksums can strengthen verification but must not be a
prerequisite for repositories created before checksums exist.

## Failure And Cancellation

Restore has a dedicated transaction id and staging location. Cancellation
leaves the original repository untouched, cleans only owned staging data and
reports any incomplete cleanup. Re-running the request must either resume a
defined checkpoint or safely restart; it must not infer success from a partial
destination.

Space estimation and copying are separate observable phases. Both receive the
same cancellation token, allowing the desktop client to show indeterminate
progress while walking a directory and to cancel before any staging data is
created.

## Remaining Work

- sparse/reflink and hard-link preservation across filesystems;
- ACL/xattr portability policy across filesystems without compatible namespaces;
- checksum storage and performance budget;
- resumable file restore versus deterministic restart;
- representation of partial consistency-group restore.
