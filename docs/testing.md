# Testing

## Automated Suite

The full suite requires root because controlled mocks create a temporary entry under `/dev/mapper`:

```bash
sudo ./tests/run-tests.sh
```

Mode without root-only operations:

```bash
./tests/run-tests.sh --static-only
```

Tests cover:

1. syntax of all Bash scripts and install hooks;
2. multi-source rendering without unresolved placeholders;
3. systemd unit and udev rule validation;
4. full and incremental transfers;
5. parent selection by UUID;
6. daily limit and invalidation after configuration changes;
7. local and remote retention;
8. cleanup after `btrfs receive` errors;
9. per-profile status and history JSON;
10. behavior after losing the target;
11. pending recovery before and after remote commit;
12. refusal to use non-private configuration;
13. refusal to use a source on the same Btrfs filesystem as the target;
14. refusal to write through a symlink escaping the target directory;
15. safe unmounting and closure of the expected mapper.

## Mock Boundaries

Mocks test script control flow and invariants, not the kernel, Btrfs, LUKS, or systemd implementations. Production use needs a test on a real or disposable environment:

```text
source Btrfs -> snapshot -> send/receive -> disconnect -> reconnect -> restore
```

The test should also cover process interruption, low disk space, and device loss.

## Real Btrfs Docker Test

The repository also includes a heavier Docker integration test that builds and
installs the Arch package, then uses real loop-backed filesystems inside a
privileged container. It creates:

1. an installable `btrfs-backup` package from the current source tree;
2. a source Btrfs filesystem with a `home` subvolume;
3. a LUKS2 target image with a Btrfs filesystem inside `/dev/mapper`;
4. rendered configuration from the installed `btrfs-backup-configure`;
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

1. package build and installation through `pacman -U`;
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
tar --zstd -tf dist/btrfs-backup-2.0.0-1-x86_64.pkg.tar.zst
sha256sum -c dist/SHA256SUMS
```
