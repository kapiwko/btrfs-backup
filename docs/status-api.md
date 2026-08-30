# Run Status API

The runtime writes machine-readable backup-run state for each configured profile.
This file-based interface is intended for tools and integrations that need
status without parsing journal text.

This document describes `RunStatus`, not target lifecycle state. A successful
backup and a target that has been unmounted and closed are independent facts.
Target lifecycle and filesystem-capacity data are exposed separately by the
system manager; clients must not infer that a successful run means the device
is safe to disconnect.

The broader engine boundary is described in
[engine-contract.md](engine-contract.md).

## Public Current Status

Current status is written atomically to:

```text
/run/btrfs-backup/profiles/<PROFILE_ID>/current.json
```

The runtime writer root can be overridden only by the trusted global
`STATUS_ROOT` setting in `/etc/btrfs-backup.conf`, never by a profile. This file
is readable by unprivileged local users, so schema version 3 contains only
presentation-safe state and progress:

```json
{
  "schemaVersion": 3,
  "runId": "20260829T160000Z-1-1",
  "state": "running",
  "phase": "sizing",
  "activity": "sizing",
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

`state` reports lifecycle values such as `starting`, `running`, `validating`, `validated`,
`skipped`, `succeeded`, `failed`, `cancelled`, and `exited`. `errorCode` is
empty for normal states, `backup.failed` for failures, and `backup.cancelled`
for cancellation. Specific errors are private.

Progress values use `-1` when unknown. `progressAccuracy` is `exact`,
`estimated`, or `indeterminate`. `speedBps` uses a three-second exponentially
weighted moving average, while `etaSeconds` is an estimate. Clients must not
present estimated progress as an exact guarantee.

During Btrfs stream sizing, percentage and ETA remain indeterminate. The
effectful transfer reports exact percentage progress because its total is
measured by an identical, non-effectful send pass.

`activity` is a stable presentation category: `preparing`, `sizing`,
`transferring`, `finalizing`, or `idle`. `phase` provides the detailed stage:
run/source preparation, pending recovery, incoming cleanup, hooks, snapshot
creation, sizing, transfer, verification, commit, retention, and source
cleanup. `runId` and `canCancel` let an authorized client bind
`CancelBackup(profileId, runId)` to the exact active run.

`sourceName` and `targetName` are presentation labels from the sanitized public
profile. They must never be populated from device paths, mount points, UUIDs,
or snapshot paths. The public document deliberately excludes private
messages, timestamps, paths, UUIDs, byte totals, detailed error codes,
diagnostic details, recovery guidance, and exit codes.

All fields shown in the schema version 3 example are required. Unknown numeric
progress is represented by `-1`, and an unavailable run identifier is represented
by an empty `runId`; fields are not omitted. Producers and in-tree consumers use
the shared typed `RunStatusDocumentCodec`. Consumers reject missing fields, wrong
JSON types, invalid progress ranges, and inconsistent cancellation/error state,
while ignoring additional fields and retaining unknown state, phase, and activity
values for forward compatibility.

## Private History

Finished runs are written atomically to:

```text
/var/lib/btrfs-backup/history/<PROFILE_ID>/<RUN_ID>.json
/var/lib/btrfs-backup/history/<PROFILE_ID>/last.json
```

The root and per-profile directories use mode `0700`; JSON entries use mode
`0600`. History retains diagnostic schema version 2 with the complete record,
including names, run id, phase, messages, timestamps, detailed error code,
`details`, recovery guidance, byte counters, and exit code. Reading history
requires root or a future authorized system API.

## CLI

Unprivileged clients can inspect and watch current status:

```bash
btrfs-backupctl status show --profile default
btrfs-backupctl status watch --profile default
```

Human output for a public current-status document labels the run with its
profile id because schema version 3 deliberately does not expose the profile
display name. When `status show` falls back to the latest durable history
record, it can use the stored profile display name and diagnostic fields. The
per-run document is authoritative; `last.json` is only a rebuildable cache.

History commands require root:

```bash
sudo btrfs-backupctl status history --profile default --limit 10
```

`status watch` reads and validates schema version 3 once, then waits for an
inotify invalidation before reading the JSON document again. The watcher tracks
the containing directory so atomic replacement by `rename` remains visible.
`--interval SECONDS` optionally enables a periodic resynchronization timeout;
it is not used by default. A root `status show` invocation may fall back to the
latest private history record after systemd removes the runtime directory.

The system manager follows the same invalidation model and emits
`StatusChanged(profileId)` over D-Bus. This signal carries no status payload:
GUI clients load state when connecting and call `GetStatus` again after each
signal, so JSON remains the source of truth and manager or client restarts do
not lose the current state.

## Target Storage Measurement

Before target cleanup, the runner records the last successful filesystem-space
measurement in the private state tree:

```text
/var/lib/btrfs-backup/profiles/<PROFILE_ID>/target-storage.json
```

The exact root follows the trusted global state-root configuration. The file is
written atomically with mode `0600` under directories with mode `0700`. It is a
private cache, not a public status document. It contains the profile id, target
identity, capacity, free and available byte counts, and a UTC measurement time.
Readers discard it after a profile or target identity change.

When the target is already mounted through the configured mapper and its Btrfs
identity is verified, the manager measures it live. Otherwise it may expose the
matching cached measurement. Reading status never unlocks LUKS, mounts the
target, starts validation, or changes target state.

Capacity is the filesystem capacity reported by `statvfs`, not the raw disk,
partition or LUKS container size. Used bytes are `capacity - free`; usage
percentage follows `df`-like usable-space semantics using used plus available
bytes. On Btrfs these values are an operational approximation and do not model
chunk allocation, compression ratios, metadata pressure or qgroup accounting.

## Compatibility

Public current status uses schema version 3. Private history retains schema
version 2. These are separate contracts; consumers must not expect diagnostic
history or target capacity fields in public current status. Target lifecycle
and storage usage use the separate authorized system API described in
[system-dbus-api.md](system-dbus-api.md).
