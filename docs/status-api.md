# Run Status API

The runtime writes machine-readable backup-run state for each configured profile.
This file-based interface is intended for tools and integrations that need
status without parsing journal text.

This document describes `RunStatus`, not target lifecycle state. A successful
backup and a target that has been unmounted and closed are independent facts.
The current runtime does not persist a `TargetStatus` after eject, so clients
must not infer that a successful run means the device is safe to disconnect.

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
  "state": "running",
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

`state` reports lifecycle values such as `starting`, `running`, `validated`,
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

`sourceName` and `targetName` are presentation labels from the sanitized public
profile. They must never be populated from device paths, mount points, UUIDs,
or snapshot paths. The public document deliberately excludes run ids, phases,
messages, timestamps, paths, UUIDs, byte totals, detailed error codes,
diagnostic details, recovery guidance, and exit codes.

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
btrfs-backupctl status watch --profile default --interval 1
```

Human output for a public current-status document labels the run with its
profile id because schema version 3 deliberately does not expose the profile
display name. When `status show` falls back to private `last.json`, it can use
the stored profile display name and diagnostic fields.

History commands require root:

```bash
sudo btrfs-backupctl status history --profile default --limit 10
```

`status watch` validates schema version 3 before emitting a changed public
document. A root `status show` invocation may fall back to private `last.json`
after systemd removes the runtime directory.

## Compatibility

Public current status uses schema version 3. Private history retains schema
version 2. These are separate contracts; consumers must not expect diagnostic
history fields in public current status. A future `TargetStatus` will use a
separate document or authorized system API.
