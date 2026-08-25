# Restore Engine

Status: proposed.

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

## Safety Rules

- Dry-run is available before every copying or subvolume-creation operation.
- Relative source paths cannot escape the selected snapshot.
- Destination traversal rejects symlinks and unexpected mount boundaries.
- Active `/`, `/home` and configured source subvolumes are never overwritten.
- Existing destination data is preserved unless a narrowly scoped overwrite
  policy is explicitly selected.
- Ownership, mode, ACL and xattr preservation failures are reported, not hidden.
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

## Open Questions

- copy implementation and sparse/reflink behavior across filesystems;
- ACL/xattr portability policy;
- checksum storage and performance budget;
- resumable file restore versus deterministic restart;
- representation of partial consistency-group restore.
