# ADR 0002: Let systemd Own Target Activation

- Status: accepted
- Date: 2026-08-25
- Amended: 2026-08-30

## Context

An encrypted removable target requires device discovery, LUKS activation,
mount ordering, backup execution, unmount and mapper closure. Duplicating this
lifecycle inside the runner risks dependency cycles and inconsistent state
between the service namespace and PID 1.

The backup must start from udev without a desktop session or long-running
application daemon. Requiring an administrator to merge generated fragments
into global `fstab` and `crypttab` files makes otherwise transactional profile
installation incomplete and complicates unattended provisioning.

## Decision

udev sets `SYSTEMD_WANTS` for the profile service. Profile installation
transactionally publishes a native, profile-specific `.mount` unit and its
dependency on `btrfs-backup-target@PROFILE.service`. The target service opens
the configured LUKS device through `systemd-cryptsetup` and records whether it
created the mapper. The mount unit does not start the backup service.

A generated service drop-in uses `RequiresMountsFor` so systemd activates the
native mount and target service before starting the sandboxed runner. Neither
`/etc/fstab` nor `/etc/crypttab` is read or modified by btrfs-backup. LUKS key
acquisition is stored in the root-only profile and is restricted to structured,
validated modes rather than arbitrary unit or cryptsetup arguments.

After the sandboxed runner reaches its final state, `OnSuccess` or `OnFailure`
schedules a separate eject unit that synchronizes, stops the expected mount,
and deactivates the matching target service. A target service restores a mapper
only when that activation created it; explicit eject retains its separately
authorized, identity-checked ability to close a pre-existing mapper. These
unit-level dependencies run only after the runner's processes and private mount
namespace are gone. A directly invoked administrative command may request the
same systemd units as a controlled fallback.

## Alternatives

- Let the runner call `cryptsetup` and `mount` for the complete lifecycle.
- Let udev execute mount and backup commands through `RUN+=`.
- Require a system manager daemon to mount and start every backup.
- Generate fragments for an administrator to merge into global `fstab` and
  `crypttab` files.
- Rewrite global `fstab` and `crypttab` files automatically.

## Consequences

- PID 1 remains the authority for mount and cryptsetup unit state.
- Profile installation owns native target activation units and must publish,
  replace, and remove them transactionally.
- Profile schema and public redaction must account for private key acquisition
  settings.
- Existing `noauto` fstab and crypttab entries may remain dormant during
  migration, but new installations do not need them.
- Service and eject ordering require systemd integration tests.
- The runner must validate the mounted target identity before writing.
- Automatic backup remains independent of KDE and the future manager.
