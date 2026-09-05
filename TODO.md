# TODO

## 4.0 Release Gate

Current decision: **NO-GO** because pacman does not abort an upgrade when an
Arch `pre_upgrade` install scriptlet fails. A real `v3.2.0` to 4.0 transaction
with an installed legacy profile returned success and replaced the package.
The runtime rejected the profile without modifying it, but the documented
package-level fail-closed guarantee cannot be met by an incoming Arch
install scriptlet.

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

Provide an Arch pre-transaction guard from a 3.2.x bridge package, or change
the Arch upgrade design so pacman can reject incompatible installed profiles
before file replacement. Then verify both the successful exported-v4 path and
the rejected legacy path in real pacman transactions, and rerun the remote
release gates for the final candidate.

### Accepted Non-Blocking 4.0 Residual Risk

The P2 controller, physical-device, extended power-loss and wider
kernel/systemd matrix remains in [ROADMAP.md](ROADMAP.md). It does not block
4.0 unless it reveals a regression.
