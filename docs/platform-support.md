# Platform Support

The reference platform is current Arch Linux on x86-64 with systemd, udev,
LUKS2 and Btrfs. Release candidates are exercised there with native packages,
real loop-backed filesystems and the QEMU hotplug scenario. The command-line
runtime and unattended backup path do not require KDE.

## Support Matrix

| Environment | Support level | Verification |
|---|---|---|
| Current Arch Linux, x86-64 | Reference | GCC and Clang builds, unit and contract tests, native package installation, real Btrfs/LUKS tests and QEMU/systemd hotplug tests |
| Plasma 6 on current Arch Linux | Reference for the optional desktop package | KDE build and D-Bus contract tests; release smoke testing covers the plasmoid, KCM, notifications, Dolphin/KIO browsing and restore |
| Other current systemd-based Linux distributions | Compatible when the listed build and runtime dependencies are available | Compiler tests exercise portable C++ boundaries, but each distribution still needs package installation and restore testing |
| Linux systems without systemd/udev | Manual use only | The CLI can inspect and restore an already mounted repository; automatic device activation and unattended runs require systemd and udev |
| Non-Linux systems | Unsupported | The runtime depends on Linux mount, block-device, Btrfs, device-mapper and process interfaces |

Only the environments marked **Reference** are release validation targets.
Kernel, systemd, cryptsetup and Btrfs tool versions supplied by the current
Arch repositories form the tested version set for a release candidate. A
wider version promise requires a successful package, backup, reconnect and
restore run on that exact platform.

## Package Status

| Artifact | Status |
|---|---|
| Arch `pkg.tar.zst` base and KDE packages | Native reference packages; installed and exercised by release gates |
| Source tarball and generic install tree | Maintained distribution-neutral artifacts; consumers provide compatible dependencies and integration |
| DEB and RPM | Maintained compatibility packages; artifact layout and lifecycle scripts are tested, while distribution-specific runtime validation is required before claiming native support |
| Nix expression, Gentoo ebuild and standalone `PKGBUILD` | Best-effort packaging templates for downstream maintainers; generation is tested, not installation on those ecosystems |

The exact dependencies and artifact contents are documented in
[Building Release Artifacts](packaging.md). Reports should name the operating
system, kernel, systemd, cryptsetup, Btrfs tools, package type and desktop
version when applicable.
