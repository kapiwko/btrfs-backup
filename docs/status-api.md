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

The root directory can be overridden with `STATUS_ROOT` in the runtime profile env.
The status directory is intended to be readable by unprivileged local users.
After a oneshot service exits, systemd may remove the runtime directory. In
that case, `btrfs-backupctl status --profile <profile>` falls back to
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
  "sourceCount": 0,
  "startedAt": "2026-08-23T02:44:07+00:00",
  "updatedAt": "2026-08-23T02:44:07+00:00",
  "finishedAt": "",
  "error": "",
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

## History

Finished runs are written atomically to:

```text
/var/lib/btrfs-backup/history/<PROFILE_ID>/<RUN_ID>.json
/var/lib/btrfs-backup/history/<PROFILE_ID>/last.json
```

The root directory can be overridden with `HISTORY_ROOT` in the runtime profile env.
History entries use the same schema as `current.json`, with `finishedAt` and
the final `exitCode` populated.
History is intended to be readable by unprivileged local users; private recovery
state remains under `STATE_DIR/profiles/<PROFILE_ID>`.
`btrfs-backupctl history` returns an empty JSON array when no history exists yet.

## CLI

Use `btrfs-backupctl` to inspect the file-based status API:

```bash
btrfs-backupctl status --profile default
btrfs-backupctl status --profile default --human
btrfs-backupctl status --all --human
btrfs-backupctl history --profile default --limit 10
btrfs-backupctl watch --profile default --interval 1
```

For tests or chrooted environments, the roots can be overridden:

```bash
btrfs-backupctl \
  --status-root /tmp/run/profiles \
  --history-root /tmp/history \
  status --profile default
```

## Compatibility

The JSON schema starts at `schemaVersion: 1`. Future compatible changes may add
fields. Clients should ignore unknown fields and treat missing optional fields
as unavailable.
