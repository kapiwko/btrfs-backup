# Building Release Artifacts

## Release Script

The upstream repository includes a release script with separate build targets:

The root `VERSION` file is the authoritative version source for the native
build, Plasma metadata, and all release artifact names.

```bash
./tools/build-release.sh --target source --static-tests
./tools/build-release.sh --target deb --static-tests
./tools/build-release.sh --target arch-base --static-tests
./tools/build-release.sh --target all --static-tests
sudo ./tools/build-release.sh --target arch --full-tests
```

For repeated local integration runs, reuse a persistent native build tree:

```bash
./tools/build-release.sh \
  --target arch-base \
  --skip-tests \
  --build-dir build/integration-package \
  --dist-dir /tmp/btrfs-backup-packages
```

Targets:

| Target | Output |
|---|---|
| `source` | source tarball, source ZIP, checksums, build report |
| `arch` | source tarball, Arch-compatible base and KDE `pkg.tar.zst` packages, source ZIP, checksums, build report |
| `arch-base` | source tarball, base `pkg.tar.zst` package, source ZIP, checksums, build report |
| `deb` | source tarball, Debian-compatible `.deb`, source ZIP, checksums, build report |
| `tar-install` | source tarball, generic install tree tarball, source ZIP, checksums, build report |
| `rpm` | source tarball, RPM spec packaging archive, source ZIP, checksums, build report |
| `nix` | source tarball, Nix packaging skeleton archive, source ZIP, checksums, build report |
| `ebuild` | source tarball, Gentoo ebuild packaging archive, source ZIP, checksums, build report |
| `pkgbuild` | source tarball, Arch/AUR `PKGBUILD` packaging archive, source ZIP, checksums, build report |
| `all` | all targets above except unsupported package ecosystems |

The script creates deterministic source archives, builds native package
archives where practical, and writes SHA-256 reports from its own packaging
generators. Packaging does not run the repository test suite unless
`--static-tests` or `--full-tests` is selected explicitly. Release CI should
depend on successful test jobs and then package without repeating them. The
generated Arch `PKGBUILD` still defines `check()` for distribution builders.

Outputs are written to `dist/`:

```text
btrfs-backup-0.3.0.tar.gz
btrfs-backup-0.3.0-1-x86_64.pkg.tar.zst
btrfs-backup-kde-0.3.0-1-x86_64.pkg.tar.zst
btrfs-backup_0.3.0-1_amd64.deb
btrfs-backup-0.3.0-install.tar.gz
btrfs-backup-0.3.0-rpm-packaging.tar.gz
btrfs-backup-0.3.0-nix-packaging.tar.gz
btrfs-backup-0.3.0-ebuild.tar.gz
btrfs-backup-0.3.0-pkgbuild.tar.gz
btrfs-backup-0.3.0-source.zip
SHA256SUMS
BUILD-REPORT.txt
```

The `source` target uses the source archive toolchain. Arch package construction
additionally uses `zstd`; other package backends may have their own tool
requirements.
Building from source requires CMake, a C++20 compiler, `pkg-config`,
`nlohmann-json`, `libmount`, `libblkid`, `libudev`, `libsystemd`, and `libbtrfsutil`
development files for the native code under `src/`.
Building the optional Plasma package also requires Extra CMake Modules, Qt 6
QML/Quick, Kirigami, KPackage, KI18n and libplasma development files.

## Arch Packaging

The upstream repository intentionally does not track `PKGBUILD`. Arch or AUR packaging should live in the corresponding packaging repository and use the upstream source tarball:

```text
source=("btrfs-backup-${pkgver}.tar.gz")
```

The base package name and package base should both be `btrfs-backup`. Optional
desktop integration should be packaged as `btrfs-backup-kde` and must not add
Qt, Kirigami or Plasma dependencies to the base package.

The maintained package install hooks live under `packaging/arch/`. The release
tool generates `PKGBUILD` and `.SRCINFO` from the current source tree so version
and source checksums remain tied to the release artifacts.

## Package Contents

Native commands are installed directly under `/usr/bin`, runner and manager
systemd units under `/usr/lib/systemd/system`, D-Bus activation and policy
files under `/usr/share/dbus-1`, polkit actions under
`/usr/share/polkit-1/actions`, examples under
`/usr/share/btrfs-backup/examples`, and documentation under
`/usr/share/doc/btrfs-backup`. The base package creates the trusted hook
directory `/etc/btrfs-backup/hooks.d` as `root:root 0755`.

The public command surface is `btrfs-backup` and `btrfs-backupctl`. The optional
system-bus service executable is `btrfs-backupd`. Target
mount and eject operations are `btrfs-backupctl target mount` and
`btrfs-backupctl target eject`; standalone mount/eject wrapper commands are no
longer packaged.

The base project also provides a native CMake installation contract. Packaging
backends can stage the same base filesystem layout with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
DESTDIR="$pkgdir" cmake --install build
```

This installs the two commands, the manager executable, rendered profile/eject
service templates, manager activation and policy files, configuration examples,
schema, documentation, and the trusted hook directory.
It does not install an active profile or a profile-specific udev rule.

The packaged manager unit grants write access to the default
`/var/lib/btrfs-backup/state` root for run-scoped cancellation. Installations
that override `STATE_ROOT` must add the corresponding `ReadWritePaths` entry in
a `btrfs-backupd.service` drop-in.

The package provides fstab and crypttab fragments for administrator-managed
configuration. `btrfs-backupctl profile wizard --apply` and `profile save`
write active profiles, udev rules, and profile-specific systemd mount-dependency
drop-ins after an explicit user command.

The optional `btrfs-backup-kde` package installs the Plasma applet under
`/usr/share/plasma/plasmoids` and the compiled QML module under
`/usr/lib/qt6/qml`. Its install hook prints a Plasma reload hint and does not
run user-session cache tools as root.

The base package installs native ELF commands directly in `/usr/bin` and uses
Btrfs userspace tools, cryptsetup, systemd/udev, `coreutils`, and `util-linux` at
runtime. `btrfs-backupd` exposes presentation-safe read methods through
`io.github.btrfsbackup.Manager1` alongside separately polkit-authorized
operational controls; backup execution remains a separate systemd runner and
works without the daemon. Desktop notifications are owned by
`btrfs-backup-kde`.

## Reproducibility

Directories, file modes, owners, entry order, and timestamps are normalized with a fixed `SOURCE_DATE_EPOCH`. Rebuilding from the delivered source ZIP should produce identical SHA-256 sums for the tarball, package, and source ZIP.
