# TODO: Next Sprint

This file is intentionally limited to the next implementation sprint. Product
directions that are not sprint commitments belong in [ROADMAP.md](ROADMAP.md).
Unresolved architecture belongs in `docs/design/`; accepted decisions belong
in `docs/adr/`.

## Sprint Goal

Add narrowly authorized operational controls to the system manager while
keeping runner execution owned by systemd and preserving automatic backup when
the manager is absent.

## Operational API

- [ ] Specify stable D-Bus request/result schemas for `StartBackup`,
  `CancelBackup`, `ValidateTarget` and `EjectTarget`.
- [ ] Add a distinct polkit action for every operation with deny-by-default
  inactive and active-session policy.
- [ ] Resolve caller identity only from the D-Bus connection and bind each
  authorization decision to that connection and request.
- [ ] Start the existing profile systemd unit without moving execution into
  `btrfs-backupd` or bypassing profile and target locks.
- [ ] Route cancellation through the existing run-scoped cancellation request;
  reject stale or mismatched run identities.
- [ ] Reuse target validation and eject use cases, including target identity
  checks, lock conflicts and safe-removal state.

## Races And Failure Handling

- [ ] Revalidate profile, run and target identity after authorization and
  immediately before each operation.
- [ ] Define stable outcomes for already-running, not-running, busy-target,
  caller-disconnected and manager-restarted cases.
- [ ] Ensure caller disconnect or manager failure never terminates an already
  started runner and never leaves an authorization result reusable.
- [ ] Emit secret-free audit records containing caller UID, action, profile,
  result and stable error code.

## Tests And Documentation

- [ ] Test unauthenticated denial, exact-action grants, cross-action denial and
  inactive-session behavior on an isolated system bus and polkit authority.
- [ ] Test disconnect, cancellation, restart and conflicting-operation races.
- [ ] Extend the real-Btrfs test with authorized start, cancellation, validation
  and eject while retaining the direct udev/systemd path.
- [ ] Update the D-Bus contract, security model, package contents and client
  guidance without advertising administrative profile writes.

## Exit Criteria

- [ ] All default, private-bus, QEMU hotplug, real-Btrfs and package tests pass.
- [ ] GCC and Clang builds pass with manager enabled and disabled; the base
  package remains free of Qt/KDE dependencies.
- [ ] Automatic backup and an active runner work when `btrfs-backupd` is
  stopped, killed or removed.
- [ ] No profile, hook or destructive media-preparation mutation is exported in
  this sprint.
