# Engine Contract

This document defines the stable boundary between configuration tooling, the
backup engine, and status consumers. The current backup engine is implemented
in C++ and delegates Btrfs send/receive to system tools. Future implementations
must preserve this contract before they can replace the current engine.

## Inputs

The canonical profile input is JSON:

```text
/etc/btrfs-backup/profiles/<PROFILE_ID>/profile.json
```

The runtime engine reads profile metadata and source definitions from JSON:

```text
/etc/btrfs-backup/profiles/<PROFILE_ID>/profile.json
```

This file is generated from canonical JSON by `btrfs-backupctl profile` and must
be trusted root-owned runtime data.

The engine must support these operation modes:

```text
backup
force
validate
mount
eject
```

`backup` may skip work because of the daily limit. `force` must bypass the
daily limit. `validate` must run static and runtime preflight checks without
creating new snapshots or transferring data.

## Profile Identity

Every operation is scoped to one profile id. The profile id selects:

```text
profile JSON
derived runtime settings
profile and target lock files
state directory
status directory
history directory
systemd unit instance
```

An operation must fail if the selected profile file declares a different
`PROFILE_ID`.

An executing runner must acquire both the selected profile lock and the target
lock keyed by normalized LUKS UUID before mounting the target. Lock contention
is a runtime failure reported as `runner.profile_busy` or `runner.target_busy`.
The rejected process must not replace current status or history owned by the
active runner.

## Required Phases

The engine reports progress through stable `phase` values. Implementations may
add more detailed phases, but these values are part of the compatibility
contract:

```text
starting
mounting-target
validating-target
validating-source
recovering-pending
creating-snapshot
selecting-parent
transferring
committing
pruning
synchronizing
ejecting
validated
skipped
succeeded
failed
cancelled
```

Status consumers must ignore unknown phases. Engines should prefer adding a new
phase over changing the meaning of an existing one.

## Public Status Output

The engine writes current status atomically:

```text
<STATUS_ROOT>/<PROFILE_ID>/current.json
```

Schema version 3 contains only presentation-safe fields:

```json
{
  "schemaVersion": 3,
  "runId": "20260829T160000Z-1-1",
  "state": "running",
  "phase": "transferring",
  "activity": "transferring",
  "canCancel": true,
  "errorCode": "",
  "sourceName": "@home",
  "targetName": "backupdisk",
  "speedBps": 104857600,
  "etaSeconds": 42,
  "sourceProgress": 50,
  "overallProgress": 25,
  "progressAccuracy": "exact"
}
```

Consumers must ignore additional fields. The presentation-safe run identifier
exists only to bind cancellation to the active run; private identifying and
diagnostic data must not be added to the public schema.

Known `state` values are:

```text
idle
starting
running
validated
skipped
succeeded
failed
cancelled
exited
```

Status writes must be atomic: write a temporary file, flush it, rename it into
place, and avoid exposing partially written JSON.

## Private History Output

Finished runs are written atomically:

```text
<HISTORY_ROOT>/<PROFILE_ID>/<RUN_ID>.json
<HISTORY_ROOT>/<PROFILE_ID>/last.json
```

History uses diagnostic schema version 2 and includes the complete record.
Directories must use mode `0700` and JSON entries mode `0600`.

## Exit Codes

The command exit code remains the primary process result:

```text
0   success, validated, or skipped as intended
1   runtime backup failure
2   invalid command-line arguments or configuration
130 cancelled by interrupt
```

More specific failures should be represented in private history JSON through
`phase`, `message`, `errorCode`, `errorMessage`, `details`, `recoverable`, and
`suggestedAction` rather than by inventing many process exit codes.

## Snapshot Semantics

Implementations must preserve these storage rules:

1. Receive into `<INCOMING_ROOT>/<SOURCE_NAME>/<RUN_ID>/`.
2. Verify that the received result is a read-only Btrfs subvolume.
3. Verify that the local snapshot UUID matches the target `Received UUID`.
4. Commit by creating or moving the verified snapshot into the final source
   target directory on the same Btrfs filesystem.
5. Never expose incomplete receive data as a completed snapshot.
6. Select incremental parents by comparing local `UUID` with target
   `Received UUID`, not by snapshot name alone.

## Error Recovery

Before creating a local snapshot, the engine writes a private
`pending-<SOURCE_NAME>` marker in the profile state directory. The marker
records the local snapshot path and its planned final target path. After an
interruption, the next run must resolve the marker before creating a new
snapshot for the same source.

If verification fails after a snapshot appears at the final path, the engine
must delete that snapshot. If deletion also fails, it must report
`repository.recovery_required`, retain the pending marker, and retry deletion
before clearing the marker or starting a new snapshot for that source.

If target access is lost while handling an error, the engine must preserve the
local snapshot and pending marker so that a later run can complete recovery.

## Application Hooks

Hooks are executable paths plus argument arrays and must never be interpreted as
shell command text. Every hook has an explicit finite timeout, runs in its own
process group, and observes the run cancellation token. Timeout uses
phase-specific `hook.*_timeout` error codes; non-zero exit and process-start
failures use phase-specific `hook.*_failed` codes. User cancellation remains
`runner.cancelled`, not a hook failure.

The program must be a direct child of `/etc/btrfs-backup/hooks.d`, a regular
non-symlink file owned by root, executable, and not writable by group or others.
Every directory from `/` through `hooks.d` must be root-owned and not writable
by group or others. The verified file descriptor remains inherited and is the
object executed by the child, so pathname replacement after verification does
not change the selected program.

## Compatibility

Profile configuration uses `schemaVersion: 3`. Public current status uses
`schemaVersion: 3`; private diagnostic history uses `schemaVersion: 2`;
checkpoint and internal event documents retain their own version 1 contracts.
Run status intentionally does not contain target
safe-removal state. Consumers must ignore unknown fields and treat missing
optional fields as unavailable.

Any future engine implementation must pass the C++ runtime tests and the real
Btrfs integration test before becoming the default.
