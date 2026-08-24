# Status API

The runtime writes machine-readable JSON state for each configured profile.
This file-based interface is intended for tools and integrations that need
status without parsing journal text.

The broader engine boundary is described in
[engine-contract.md](engine-contract.md).

## Current Status

Current status is written atomically to:

```text
/run/btrfs-backup/profiles/<PROFILE_ID>/current.json
```

The root directory can be overridden with `paths.statusRoot` in the runtime profile JSON.
The status directory is intended to be readable by unprivileged local users.
After a oneshot service exits, systemd may remove the runtime directory. In
that case, `btrfs-backupctl status show --profile <profile>` falls back to
`/var/lib/btrfs-backup/history/<PROFILE_ID>/last.json` when it exists.

Example:

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
  "errorCode": "",
  "errorMessage": "",
  "details": {},
  "recoverable": false,
  "suggestedAction": "",
  "canCancel": false,
  "safeToRemove": false,
  "exitCode": 0
}
```

`state` is one of:

```text
starting
running
validated
skipped
succeeded
failed
exited
```

`phase` is a stable, machine-readable step name such as:

```text
starting
mounting-target
validating-target
validating-source
creating-snapshot
selecting-parent
transferring
committing
pruning
succeeded
failed
validated
skipped
```

Transfer progress fields are present in every status document. During a live
transfer, byte and speed fields are updated from the native transfer pipeline.
The backup executor starts transfers through an asynchronous handle and forwards
progress events from that running transfer into the status writer. Transfer
completion and cancellation are handled through pollable file descriptors rather
than sleep-based polling in the executor.
When no transfer is active or a value cannot be estimated, numeric progress
fields use `0` or `-1` as documented below:

| Field | Meaning |
|---|---|
| `currentSourceId` | stable id of the currently processed source |
| `sourceProgress` | percentage for the current source, or `-1` when unknown |
| `overallProgress` | percentage for the whole run, or `-1` when unknown |
| `progressAccuracy` | `exact`, `estimated`, or `indeterminate` |
| `bytesProcessed` | bytes delivered to the receive process in the current stream |
| `bytesTotalEstimated` | estimated total bytes for the current stream, or `0` when unknown |
| `runBytesProcessed` | cumulative bytes delivered in the current run, including prior sources |
| `speedBps` | current transfer speed in bytes per second |
| `etaSeconds` | estimated seconds remaining, or `-1` when unknown |
| `canCancel` | whether a client should offer cancellation |
| `safeToRemove` | whether the target was logically unmounted and closed |

The status `details` object for `transferring` includes lower-level diagnostics:
`bytesProduced`, `bytesTransferred`, `deltaBytes`, `elapsedMs`, and `speedBps`.
Clients must treat progress as advisory. Unknown or estimated
progress must not be displayed as a precise guarantee.

When a source byte total is unknown, `sourceProgress` remains `-1`.
`overallProgress` is still estimated from the current one-based `sourceIndex`
and `sourceCount`, so it does not reset to zero between sources. Byte-oriented
clients should prefer `runBytesProcessed` for a monotonic run-level counter.
When the runtime can walk the local snapshot, `bytesTotalEstimated` is populated
from the apparent size of regular files in that snapshot. This is an estimate,
not the exact Btrfs send stream size, especially for incremental sends, reflinks
and compression. In that case `sourceProgress` and `etaSeconds` are useful for
orientation but must remain labelled as estimated.

When `canCancel` is `true`, a client may request cancellation with:

```bash
btrfs-backupctl runner cancel --profile <PROFILE_ID>
```

The command writes a private cancellation request in the profile state
directory. The active runner observes that request, asks the transfer pipeline
to stop, and then removes the handled request. A cancelled run finishes with
`state` set to `cancelled` and stable `errorCode` `runner.cancelled`.
SIGINT and SIGTERM delivered to an executing runner request the same controlled
cancellation path.

Transfer failures use stable error codes instead of requiring clients to parse
the diagnostic text:

| Code | Meaning |
|---|---|
| `transfer.producer_failed` | the send side failed |
| `transfer.consumer_failed` | the receive side failed |
| `transfer.producer_consumer_failed` | both transfer processes failed |
| `transfer.failed` | the pipeline failed without a side-specific cause |

## History

Finished runs are written atomically to:

```text
/var/lib/btrfs-backup/history/<PROFILE_ID>/<RUN_ID>.json
/var/lib/btrfs-backup/history/<PROFILE_ID>/last.json
```

The root directory can be overridden with `paths.historyRoot` in the runtime profile JSON.
History entries use the same schema as `current.json`, with `finishedAt` and
the final `exitCode` populated.
History is intended to be readable by unprivileged local users; private recovery
state remains under `STATE_DIR/profiles/<PROFILE_ID>`.
`btrfs-backupctl status history` returns an empty JSON array when no history exists yet.

## CLI

Use `btrfs-backupctl` to inspect the file-based status API:

```bash
btrfs-backupctl status show --profile default
btrfs-backupctl status show --profile default --human
btrfs-backupctl status show --all --human
btrfs-backupctl status history --profile default --limit 10
btrfs-backupctl status watch --profile default --json --interval 1
```

`status watch --json` emits the full status JSON object whenever
`current.json` changes. It validates that the object uses `schemaVersion: 1`
and contains the status API fields documented above before writing it to
stdout.

For tests or chrooted environments, the roots can be overridden:

```bash
btrfs-backupctl \
  --status-root /tmp/run/profiles \
  --history-root /tmp/history \
  status show --profile default
```

## Compatibility

The JSON schema starts at `schemaVersion: 1`. Future compatible changes may add
fields. Clients should ignore unknown fields and treat missing optional fields
as unavailable.
