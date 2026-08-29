# ADR 0002: Let systemd Own Target Mount Activation

- Status: accepted
- Date: 2026-08-25

## Context

An encrypted removable target requires device discovery, LUKS activation,
mount ordering, backup execution, unmount and mapper closure. Duplicating this
lifecycle inside the runner risks dependency cycles and inconsistent state
between the service namespace and PID 1.

The backup must start from udev without a desktop session or long-running
application daemon.

## Decision

udev sets `SYSTEMD_WANTS` for the profile service. A generated service drop-in
uses `RequiresMountsFor` so systemd activates the fstab mount and its cryptsetup
dependency before starting the runner. The mount unit does not start the backup
service.

After the sandboxed runner reaches its final state, `OnSuccess` or `OnFailure`
schedules a separate eject unit that synchronizes, unmounts the expected target
and closes the matching cryptsetup unit. These unit-level dependencies run only
after the runner's processes and private mount namespace are gone. A directly
invoked administrative command may request the same systemd units as a
controlled fallback.

## Alternatives

- Let the runner call `cryptsetup` and `mount` for the complete lifecycle.
- Let udev execute mount and backup commands through `RUN+=`.
- Require a system manager daemon to mount and start every backup.

## Consequences

- PID 1 remains the authority for mount and cryptsetup unit state.
- Generated fstab/crypttab integration is part of installation configuration.
- Service and eject ordering require systemd integration tests.
- The runner must validate the mounted target identity before writing.
- Automatic backup remains independent of KDE and the future manager.
