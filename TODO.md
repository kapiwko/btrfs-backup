# TODO: Post-release Work After v0.3.0

v0.3.0 has been released. Unchecked items in this file are post-release work,
not retroactive release criteria. Product directions that are not near-term
commitments belong in [ROADMAP.md](ROADMAP.md). Unresolved architecture belongs
in `docs/design/`; accepted decisions belong in `docs/adr/`.

## Sprint Goal

Finish system-level verification and auditability for the implemented,
narrowly authorized operational controls while keeping runner execution owned
by systemd and preserving automatic backup when the manager is absent.

## Operational API

- [x] Specify stable D-Bus request/result schemas for `StartBackup`,
  `CancelBackup`, `ValidateTarget` and `EjectTarget`.
- [x] Add a distinct polkit action for every operation, require administrator
  authentication for inactive callers, and grant only the implemented
  operational controls to the active local session without a password.
- [x] Resolve caller identity only from the D-Bus connection and bind each
  authorization decision to that connection and request.
- [x] Start an operation-specific hardened transient runner without moving
  execution into `btrfs-backupd` or bypassing profile and target locks.
- [x] Route cancellation through the existing run-scoped cancellation request;
  reject stale or mismatched run identities.
- [x] Reuse target validation and eject entry points, including target identity
  checks, lock conflicts and safe-removal state.

## Races And Failure Handling

- [x] Revalidate profile and run identity after authorization and perform target
  validation inside the authorized operation immediately before its effect.
- [x] Define stable outcomes for already-running, not-running, busy-target,
  caller-disconnected and manager-restarted cases.
- [x] Ensure caller disconnect or manager failure never terminates an already
  started runner and never leaves an authorization result reusable.
- [ ] Emit secret-free audit records containing caller UID, action, profile,
  result and stable error code.

## Tests And Documentation

- [x] Test unauthenticated denial, exact-action grants and caller-bound
  authorization on an isolated system bus and polkit authority.
- [ ] Add packaging-level cross-action delegation and inactive-session denial tests.
- [x] Test disconnect, cancellation, restart and conflicting-operation races.
- [x] Extend the real-Btrfs test with an authorized start by an unprivileged
  caller while retaining the direct udev/systemd path.
- [ ] Add real-system manager cancellation, validation, eject, cross-action
  delegation, and inactive-session denial coverage.
- [x] Update the D-Bus contract, security model, package contents and client
  guidance without advertising administrative profile writes.

## Post-release Verification

- [x] All default, private-bus, QEMU hotplug, real-Btrfs and package tests pass.
- [ ] GCC and Clang builds pass with manager enabled and disabled; the base
  package remains free of Qt/KDE dependencies.
- [ ] Automatic backup and an active runner work when `btrfs-backupd` is
  stopped, killed or removed.
- [ ] No profile, hook or destructive media-preparation mutation is exported in
  this sprint.
