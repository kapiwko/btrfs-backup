# TODO

## 4.0 Release Gate

Current decision: **NO-GO** until the changes after the last release candidate
pass the complete local suite and a new manual release-gate run for the final
commit. The previous candidate passed, but that result must not be attributed
to a newer commit.

### Verified Gates

The following are evidence, not active tasks:

- browse roots are root-owned, descriptor-pinned and protected by independent
  operation leases; cleanup cannot close an active operation;
- previous-version listing and the local-path-to-restore KDE flow are covered;
- restore shows its plan and structured outcome and uses stable error codes;
- provisioning requires an explicit destructive scope, explains exclusions
  and reports completed, pending and recovery steps;
- migration preflight is read-only, bulk v4 export is atomic and
  non-overwriting, runtime profile loading remains v4-only, and package
  upgrades fail closed before replacing an incompatible 3.2 installation;
- untrusted transaction records, occupied block stacks and unsupported target
  layouts are rejected;
- QEMU verifies provisioning recovery and exact systemd helper device
  isolation;
- GCC, Clang, strict warnings, clang-tidy, ASan/LSan/UBSan, KDE, QEMU and
  real-Btrfs passed locally;
- release packages were built and inspected, and the base package plus staged
  KDE package load without libraries from the build tree;
- remote compiler, sanitizer, clang-tidy, strict-warnings, D-Bus, KDE and
  systemd-security gates passed for `3bd8d71e`; the complete packaging, QEMU and
  real-Btrfs release workflow last passed for its parent `1cb7757a` and must be
  rerun for the final candidate.

### Accepted Non-Blocking 4.0 Residual Risk

The P2 controller, physical-device, extended power-loss and wider
kernel/systemd matrix remains in [ROADMAP.md](ROADMAP.md). It does not block
4.0 unless it reveals a regression.
