# System Manager

Status: query, event-driven change signals, authorized operational control,
profile administration, caller-bound browse sessions and stable audit records
implemented; scheduling and device preparation remain planned.

Descriptions of the current query and operational-control surface below are
implemented unless explicitly marked as planned. The delivery sequence records
both completed and future stages. [`TODO.md`](../../TODO.md) records active
sprint tasks when a sprint is defined, while longer-term features remain in
[`ROADMAP.md`](../../ROADMAP.md).

## Role

`btrfs-backupd` is an optional privileged system-bus adapter. Its implemented
surface exposes sanitized profiles, status, history and device state plus
polkit-authorized start, cancel, validation, eject, profile administration and
read-only browse-session operations. It does not contain backup planning or
transfer implementations copied from the CLI.

The runner remains a separate systemd process. udev starts the runner unit
without contacting the manager, and an active runner survives manager restart
or removal.

## API Shape

The versioned `io.github.btrfsbackup.Manager1` interface begins with:

```text
GetCapabilities
ListProfiles
GetStatus
GetHistorySanitized
GetDeviceState
StartBackup
CancelBackup
ValidateTarget
EjectTarget
GetProfileDetails
UpdateProfileSettings
AddProfileSource
UpdateProfileSource
RemoveProfileSource
DeleteProfile
GetBackupCoverage
OpenBrowseSession
RenewBrowseSession
SetBrowseSessionActive
CloseBrowseSession
ListBrowseDirectory
InspectBrowseEntry
OpenBrowseFile
ProfilesChanged
StatusChanged
HistoryChanged
DeviceStateChanged
```

Destructive device preparation remains a later operation. Stable codes and
structured details cross the bus; presentation text remains a client concern.

Capabilities independently advertise D-Bus API, profile schema, public status
schema, history schema and optional features. Clients reject incompatible
major versions and tolerate unknown optional fields.

## State Ownership

The runner owns execution and writes runtime current status plus durable
history. The manager loads the current files on each request, falls back to
durable history after the oneshot runner exits, and emits invalidation signals
when those files change. It observes files, udev and kernel mount notifications;
it never owns or signals the running backup process, and stopping the manager
cannot stop an active run.

`TargetStatus` is separate from `RunStatus` and represents mounted, ejecting,
safe-to-remove and error states based on the actual target lifecycle.

## Authorization

The system bus policy is default-deny. Presentation-safe reads are available
without polkit. Every mutating method authorizes the D-Bus caller through the
specific action defined in [the authorization contract](../system-dbus-api.md).
Inputs are validated before prompting and revalidated immediately before the
effect. The revalidation compares configuration generation and fingerprint,
and the caller's unique bus name must still have an owner after polkit returns.
Hook changes require both profile-save and change-hooks permission.

Stable, secret-free audit records contain caller UID, action, profile, result
and stable error code. The manager appends and synchronizes them to the root-only
`/var/log/btrfs-backup/manager-audit.jsonl`; ordinary journald diagnostics are
not the audit contract.

## Failure Model

- Manager crash does not stop a runner or suppress its history write.
- Runner crash is reflected from systemd and durable status, not hidden by a
  cached manager state.
- Caller disconnect cancels only the pending request/authorization, never an
  unrelated run.
- Cancellation targets the selected unit and run identity.
- Conflicting profile or target operations are rejected by server-side checks
  and existing locks.

## Delivery Sequence

1. read-only capabilities, profiles, status, history and device state
   (implemented);
2. state-change signals and file-backed reconstruction after a manager restart
   (implemented);
3. operational start/cancel/validate/eject actions with polkit (implemented);
4. administrative profile writes and hook-change authorization (implemented);
5. shared C++ client, KDE monitor and KCM (implemented);
6. caller-bound browse sessions and KDE restore adapters (implemented);
7. scheduling and request queue integration (planned).

## Open Questions

- ObjectManager adoption if multiple live run objects are introduced;
- exact boundary between systemd unit control and application orchestration.
