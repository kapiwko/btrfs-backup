# Testing

## Automated Suite

The default local suite runs without real Btrfs devices:

```bash
./tests/run-tests.sh
```

Mode without root-only operations:

```bash
./tests/run-tests.sh --static-only
```

Tests cover:

1. syntax of remaining shell launchers and install hooks;
2. multi-source rendering without unresolved placeholders;
3. systemd unit and udev rule validation;
4. canonical profile JSON validation, rendering, save, show, and export;
5. per-profile status and history JSON.

## Mock Boundaries

C++ unit tests cover the native backup entrypoint, target mount/eject commands,
planning, transfer orchestration, recovery, retention, status writing and
validation logic. Production use also needs a test on a real or disposable
environment:

```text
source Btrfs -> snapshot -> send/receive -> disconnect -> reconnect -> restore
```

The test should also cover process interruption, low disk space, and device loss.

## QEMU Hotplug System Tests

The Docker integration test is useful for real Btrfs, LUKS and package
installation coverage, but it still cannot model the full desktop/system
boundary: kernel hotplug events, udev rule delivery, systemd instance startup,
device disappearance and reconnect behavior. Those cases belong in a separate
QEMU-based system test target, outside the default suite.

The target scenario should boot a minimal Arch Linux guest, install the package
from the current source tree, create a Btrfs source filesystem, then dynamically
attach a virtual USB target disk. The test should let udev trigger the
configured systemd instance and verify:

1. first full transfer;
2. second incremental transfer;
3. target detach after a successful run;
4. reconnect of the same target;
5. recovery from pending state created by an interrupted run;
6. status and history JSON with stable `errorCode`, `errorMessage`, `details`,
   `recoverable` and `suggestedAction` fields.

Failure scenarios should be injected at the process or block-device boundary,
not by shell mocks:

1. `SIGKILL` for `btrfs send`;
2. `SIGKILL` for `btrfs receive`;
3. ENOSPC on the target;
4. target remounted read-only;
5. mapper loss while the run is active;
6. corrupted active profile JSON;
7. stale pending marker from an old run;
8. interruption during commit;
9. interruption after commit but before history write;
10. guest suspend while transfer is active.

Keep this harness opt-in. It needs QEMU, nested privileges, disposable disk
images, and root-equivalent control inside the guest, so it should not run from
`make` or the default local test script.

Local runner tests cover the file-based cancellation request and verify that an
active transfer sees the request, reports `runner.cancelled`, and clears the
handled marker. Future privileged-control tests should still verify that manual
start, force, validate, cancel and eject operations cannot conflict with an
active backup unit, cannot operate on a mismatched profile id, and cannot affect
unrelated processes or targets.

## Real Btrfs Docker Test

The repository also includes a heavier Docker integration test that builds and
installs the Arch base and KDE packages, then uses real loop-backed filesystems
inside a privileged container. It creates:

1. installable `btrfs-backup` and `btrfs-backup-kde` packages from the current
   source tree;
2. a source Btrfs filesystem with a `home` subvolume;
3. a LUKS2 target image with a Btrfs filesystem inside `/dev/mapper`;
4. rendered configuration from the installed `btrfs-backupctl`;
5. active test configuration under `/etc/btrfs-backup` inside the container.

Run it with:

```bash
tests/integration/docker/run-real-btrfs.sh
```

The Docker run uses `--privileged` because the test needs loop devices, device
mapper, mounts, and Btrfs ioctls. It does not read or write the host backup
configuration. The repository is mounted read-only into the container, the
package is built under `/tmp`, and all test filesystems live under `/tmp` inside
the container.

The test covers:

1. base and KDE package build and installation through `pacman -U`;
2. configuration rendering and validation through the installed CLI;
3. runtime validation of the mounted target;
4. rejection of a mismatched target Btrfs UUID;
5. rejection of a source located on the backup target filesystem;
6. a real full `btrfs send/receive`;
7. a real incremental `btrfs send -p` after source data changes;
8. rejection of an incremental run when remote snapshots exist but no local
   UUID-matching parent is available;
9. verification that the latest remote snapshot matches the latest local
   snapshot;
10. verification that the remote snapshot `Received UUID` matches the local
   snapshot UUID;
11. local and remote retention after a third backup;
12. cleanup of per-source `.incoming` content after successful receives;
13. per-profile `current.json` and history JSON after a real backup.

## Release Checks

`tools/build-release.sh --target all` runs tests, creates the source tarball, builds all supported release targets, and writes SHA-256 reports. Package targets that produce installable archives are also smoke-tested where practical. After building the Arch target, also check:

```bash
tar --zstd -tf dist/btrfs-backup-0.2.2-1-x86_64.pkg.tar.zst
sha256sum -c dist/SHA256SUMS
```
