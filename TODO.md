# TODO

## 4.0 Release Gate

Current decision: **NO-GO** until the remote test and release workflows pass
for the final candidate commit. A 3.x bridge release is not part of this gate;
the project has no deployed 3.x profile base that requires migration support.

### Verified Gates

The following are evidence, not active tasks:

- browse roots are root-owned, descriptor-pinned and protected by independent
  operation leases; cleanup cannot close an active operation;
- previous-version listing and the local-path-to-restore KDE flow are covered;
- restore shows its plan and structured outcome and uses stable error codes;
- provisioning requires an explicit destructive scope, explains exclusions
  and reports completed, pending and recovery steps;
- untrusted transaction records, occupied block stacks and unsupported target
  layouts are rejected;
- QEMU covers provisioning recovery and hotplug through emulated USB, NVMe and
  SCSI controllers with exact systemd helper device isolation;
- GCC and Clang presets cover builds with and without the system manager;
- seeded libFuzzer smoke tests cover profile JSON and security-relevant path
  inputs under AddressSanitizer and UndefinedBehaviorSanitizer;
- pull requests have pinned CodeQL analysis and Conventional Commit title
  gates;
- native installation includes man pages and Bash, Zsh and Fish completions;
- the reference platform, package support levels and LUKS header recovery
  procedure are documented;
- the full local base and KDE suites, the privileged real-Btrfs/LUKS suite,
  reproducible release builds and a real Plasma 6 Wayland smoke run have passed
  during 4.0 development.

### Required Before 4.0

Push the final candidate and require the compiler, sanitizer, fuzz, static
analysis, CodeQL, D-Bus, KDE and systemd-security checks. Then run the manual
release gates for packaging, reproducibility, QEMU and real Btrfs against that
same commit. Record the resulting commit and workflow links in the release
notes.

### Accepted Non-Blocking 4.0 Residual Risk

Physical-device coverage, a wider kernel/systemd matrix and the extended QEMU
failure-injection matrix remain in [ROADMAP.md](ROADMAP.md). They do not block
4.0 unless they reveal a regression.
