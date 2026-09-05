# TODO

## 4.0 Release Gate

Current decision: **NO-GO** until the last 3.2.x bridge release ships the ALPM
pre-transaction migration hook and the three real upgrade paths pass. The 4.0
package definition now preserves that `AbortOnFail` hook, but an incoming
package cannot install a hook early enough to guard its own transaction.

### Verified Gates

The following are evidence, not active tasks:

- browse roots are root-owned, descriptor-pinned and protected by independent
  operation leases; cleanup cannot close an active operation;
- previous-version listing and the local-path-to-restore KDE flow are covered;
- restore shows its plan and structured outcome and uses stable error codes;
- provisioning requires an explicit destructive scope, explains exclusions
  and reports completed, pending and recovery steps;
- migration preflight is read-only, bulk v4 export is atomic and
  non-overwriting, and runtime profile loading remains v4-only;
- untrusted transaction records, occupied block stacks and unsupported target
  layouts are rejected;
- QEMU verifies provisioning recovery, USB hotplug and exact systemd helper
  device isolation for `c5524906`;
- the full local base and KDE suites pass for `947e9553` with 139/139 and
  166/166 tests respectively;
- the real-Btrfs/LUKS suite passes for the same runtime tree as `c5524906`,
  including package installation, provisioning, full and incremental backup,
  interruption, retention, browse, restore, system D-Bus and systemd isolation;
- two independent complete release builds for `947e9553` produced matching
  hashes for all 14 artifacts after fixing Arch archive member ordering;
- a real Plasma 6 Wayland smoke run loaded the current plasmoid and its QML
  module, 12/12 UI scenarios passed, and the rendered disconnected state was
  inspected;
- a real Arch upgrade from `v3.2.0` without profiles succeeds; with a legacy
  profile pacman reports the preflight scriptlet failure but still installs
  4.0, which is the remaining blocker;
- remote compiler, sanitizer, clang-tidy, strict-warnings, D-Bus, KDE and
  systemd-security gates passed for `3bd8d71e`; the complete packaging, QEMU and
  real-Btrfs release workflow last passed remotely for its parent `1cb7757a`.

### Required Before 4.0

Backport `upgrade preflight` and `profile export-v4` plus
`90-btrfs-backup-v4-migration.hook` to the final 3.2.x bridge package. Verify
legacy rejection, exported-and-saved v4 success, and no-profile success in real
pacman transactions. Then rerun the remote release gates for the final
candidate.

### Accepted Non-Blocking 4.0 Residual Risk

The P2 controller, physical-device, extended power-loss and wider
kernel/systemd matrix remains in [ROADMAP.md](ROADMAP.md). It does not block
4.0 unless it reveals a regression.
