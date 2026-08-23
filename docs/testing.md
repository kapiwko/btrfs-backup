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
9. behavior after losing the target;
10. pending recovery before and after remote commit;
11. refusal to use non-private configuration;
12. refusal to use a source on the same Btrfs filesystem as the target;
13. refusal to write through a symlink escaping the target directory;
14. safe unmounting and closure of the expected mapper.

## Mock Boundaries

Mocks test script control flow and invariants, not the kernel, Btrfs, LUKS, or systemd implementations. Production use needs a test on a real or disposable environment:

```text
source Btrfs -> snapshot -> send/receive -> disconnect -> reconnect -> restore
```

The test should also cover process interruption, low disk space, and device loss.

## Release Checks

`tools/build-release.sh --target all` runs tests, creates the source tarball, builds all supported release targets, and writes SHA-256 reports. Package targets that produce installable archives are also smoke-tested where practical. After building the Arch target, also check:

```bash
tar --zstd -tf dist/btrfs-backup-0.1.0-1-any.pkg.tar.zst
sha256sum -c dist/SHA256SUMS
```
