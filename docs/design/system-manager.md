# System Manager

Status: read-only foundation implemented; mutating operations remain proposed.

## Role

`btrfs-backupd` is an optional privileged system-bus adapter. Its implemented
surface exposes sanitized profiles, status, history and device state. It does
not contain backup planning, transfer or persistence implementations copied
from the CLI. Authorized control requests remain future work.

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
```

Later authorized operations include validation, start, cancel, eject, profile
save/delete and device preparation. Stable codes and structured details cross
the bus; presentation text remains a client concern.

Capabilities independently advertise D-Bus API, profile schema, public status
schema, history schema and optional features. Clients reject incompatible
major versions and tolerate unknown optional fields.

## State Ownership

The runner owns execution and writes durable current status and history. The
read-only manager loads the current files on each request and falls back to
durable history after the oneshot runner exits. It never owns or signals the
running backup process, and stopping the manager cannot stop an active run.

`TargetStatus` is separate from `RunStatus` and represents mounted, ejecting,
safe-to-remove and error states based on the actual target lifecycle.

## Authorization

The system bus policy is default-deny. Presentation-safe reads are available
without polkit. Every mutating method authorizes the D-Bus caller through the
specific action defined in [the authorization contract](../system-dbus-api.md).
Inputs are validated before prompting and revalidated immediately before the
commit. Hook changes require both profile-save and change-hooks permission.

Administrative events record caller UID, action, profile, result and stable
error code without configuration secrets.

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

1. read-only capabilities, profiles, status, history and device state;
2. state change signals and restart recovery;
3. operational start/cancel/eject actions with polkit;
4. administrative profile writes and hook-change authorization;
5. shared C++ client, KDE monitor and KCM;
6. scheduling and request queue integration.

## Open Questions

- state-change signals and their coalescing rules;
- observation mechanism for status changes;
- ObjectManager adoption if multiple live run objects are introduced;
- exact boundary between systemd unit control and application orchestration.
