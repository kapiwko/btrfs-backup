# Engine Contract

This document defines the stable boundary between configuration tooling, the
backup engine, and status consumers. The current engine is implemented with
Bash and system tools. Future implementations must preserve this contract
before they can replace the current engine.

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
be trusted root-owned runtime data. Legacy `sources.d/*.conf` files may exist
for migration compatibility, but the runtime no longer uses them as source
definitions.

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
lock file
state directory
status directory
history directory
systemd unit instance
```

An operation must fail if the selected profile file declares a different
`PROFILE_ID`.

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

## Status Output

The engine writes current status atomically:

```text
<STATUS_ROOT>/<PROFILE_ID>/current.json
```

Schema version 1 currently requires these fields:

```json
{
  "schemaVersion": 1,
  "profileId": "default",
  "profileName": "Default backup",
  "runId": "20260823T024407Z-4298-30158",
  "state": "running",
  "phase": "transferring",
  "message": "Transferring snapshot for home.",
  "currentSourceName": "home",
  "sourceIndex": 1,
  "sourceCount": 1,
  "startedAt": "2026-08-23T02:44:07+00:00",
  "updatedAt": "2026-08-23T02:44:07+00:00",
  "finishedAt": "",
  "error": "",
  "exitCode": 0
}
```

Consumers must ignore additional fields. Future compatible status writers may
add fields such as `currentSourceId`, progress percentages, byte counters,
speed, ETA, cancellation availability, target mount state, and safe-removal
state without changing `schemaVersion`.

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

## History Output

Finished runs are written atomically:

```text
<HISTORY_ROOT>/<PROFILE_ID>/<RUN_ID>.json
<HISTORY_ROOT>/<PROFILE_ID>/last.json
```

History entries use the same schema as `current.json` and must include final
`state`, `phase`, `message`, `finishedAt`, and `exitCode`.

## Exit Codes

The command exit code remains the primary process result:

```text
0   success, validated, or skipped as intended
1   runtime backup failure
2   invalid command-line arguments or configuration
130 cancelled by interrupt
```

More specific failures should be represented in status/history JSON through
`phase`, `message`, `error`, and `result` rather than by inventing many process
exit codes.

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
`pending-<SOURCE_NAME>` marker in the profile state directory. After an
interruption, the next run must resolve the marker before creating a new
snapshot for the same source.

If target access is lost while handling an error, the engine must preserve the
local snapshot and pending marker so that a later run can complete recovery.

## Compatibility

The contract starts at `schemaVersion: 1`. Compatible changes may add fields to
JSON documents. Consumers must ignore unknown fields and treat missing optional
fields as unavailable.

Any future engine implementation must pass the same mocked runtime tests and
the real Btrfs integration test before becoming the default.
