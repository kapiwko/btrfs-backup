# TODO: Next Sprint

This file is intentionally limited to the next implementation sprint. Product
directions that are not sprint commitments belong in [ROADMAP.md](ROADMAP.md).
Unresolved architecture belongs in `docs/design/`; accepted decisions belong
in `docs/adr/`.

## Sprint Goal

Deliver the read-only foundation of the system manager without making backup
execution depend on the daemon, and revalidate the current runtime before the
new system boundary is introduced.

## Runtime Baseline

- [ ] Run `tests/integration/docker/run-real-btrfs.sh` from a clean checkout and
  record the full, incremental, recovery, restore-drill, systemd and eject
  results in the release notes.
- [ ] Build and install both Arch packages in a clean environment; verify that
  the base package has no KDE runtime dependency and works without the KDE
  package installed.
- [ ] Confirm on a real system or QEMU guest that udev starts
  `btrfs-backup@<profile>.service` without a graphical session.
- [ ] Fix any baseline regression before adding the manager target.

## Read-Only System Manager

- [ ] Add a `btrfs-backupd` executable and daemon target under `src/daemon/`
  without linking daemon code into `backup`, `config`, `state` or
  `platform-linux`.
- [ ] Add system-bus activation, a default-deny bus policy and the versioned
  `io.github.btrfsbackup.Manager1` interface.
- [ ] Implement `GetCapabilities`, `ListProfiles`, `GetStatus`,
  `GetHistorySanitized` and `GetDeviceState` using existing application use
  cases and presentation-safe state.
- [ ] Restore visible state after daemon restart from current status and
  history; do not claim ownership of the running backup process.
- [ ] Keep the udev/systemd runner path fully operational when the daemon is
  stopped, crashes or is not installed.

## Tests And Documentation

- [ ] Test the read-only API on a private bus, including malformed input,
  bounded history pagination, caller disconnect and daemon restart.
- [ ] Prove that no mutating method is exported in this sprint and that the bus
  policy denies undeclared calls.
- [ ] Add an integration test that stops the manager during an active run and
  verifies that the runner completes independently.
- [ ] Update `docs/system-dbus-api.md`, packaging manifests and installed-file
  documentation to match the implemented interface.

## Exit Criteria

- [ ] All default tests, the real Btrfs test and package smoke tests pass.
- [ ] GCC and Clang builds pass with and without `BUILD_KDE_INTEGRATION`.
- [ ] The manager can be removed from a running system without disabling
  automatic backups.
- [ ] The next sprint is selected from [ROADMAP.md](ROADMAP.md), and this file
  is replaced rather than appended with long-term ideas.
