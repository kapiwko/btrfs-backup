# Building Release Artifacts

## Release Script

The upstream repository includes a release script with separate build targets:

The root `VERSION` file is the authoritative version source for the native
build, Plasma metadata, and all release artifact names.

```bash
./tools/build-release.sh --target source --static-tests
./tools/build-release.sh --target deb --static-tests
./tools/build-release.sh --target all --static-tests
sudo ./tools/build-release.sh --target arch
```

Targets:

| Target | Output |
|---|---|
| `source` | source tarball, source ZIP, checksums, build report |
| `arch` | source tarball, Arch-compatible base and KDE `pkg.tar.zst` packages, source ZIP, checksums, build report |
| `deb` | source tarball, Debian-compatible `.deb`, source ZIP, checksums, build report |
| `tar-install` | source tarball, generic install tree tarball, source ZIP, checksums, build report |
| `rpm` | source tarball, RPM spec packaging archive, source ZIP, checksums, build report |
| `nix` | source tarball, Nix packaging skeleton archive, source ZIP, checksums, build report |
| `ebuild` | source tarball, Gentoo ebuild packaging archive, source ZIP, checksums, build report |
| `pkgbuild` | source tarball, Arch/AUR `PKGBUILD` packaging archive, source ZIP, checksums, build report |
| `all` | all targets above except unsupported package ecosystems |

The script runs the selected test suite, creates deterministic source archives,
builds native package archives where practical, and writes SHA-256 reports from
its own packaging generators.

Outputs are written to `dist/`:

```text
btrfs-backup-3.0.0.tar.gz
btrfs-backup-3.0.0-1-x86_64.pkg.tar.zst
btrfs-backup-kde-3.0.0-1-x86_64.pkg.tar.zst
btrfs-backup_3.0.0-1_amd64.deb
btrfs-backup-3.0.0-install.tar.gz
btrfs-backup-3.0.0-rpm-packaging.tar.gz
btrfs-backup-3.0.0-nix-packaging.tar.gz
btrfs-backup-3.0.0-ebuild.tar.gz
btrfs-backup-3.0.0-pkgbuild.tar.gz
btrfs-backup-3.0.0-source.zip
SHA256SUMS
BUILD-REPORT.txt
```

The `source` target uses the source archive toolchain. Arch package construction
additionally uses `zstd`; other package backends may have their own tool
requirements.
Building from source requires CMake, a C++20 compiler, `pkg-config`,
`nlohmann-json`, `libmount`, `libblkid`, `libudev`, and `libbtrfsutil`
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

Native commands are installed directly under `/usr/bin`, the profile and eject
systemd units under `/usr/lib/systemd/system`, examples under
`/usr/share/btrfs-backup/examples`, and documentation under
`/usr/share/doc/btrfs-backup`. The base package creates the trusted hook
directory `/etc/btrfs-backup/hooks.d` as `root:root 0755`.

The public command surface is `btrfs-backup` and `btrfs-backupctl`. Target
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

This installs the two commands, rendered profile/eject service templates,
configuration examples, schema, documentation, and the trusted hook directory.
It does not install an active profile or a profile-specific udev rule.

The package provides fstab and crypttab fragments for administrator-managed
configuration. `btrfs-backupctl profile wizard --apply` and `profile save`
write active profiles, udev rules, and profile-specific systemd mount-dependency
drop-ins after an explicit user command.

The optional `btrfs-backup-kde` package installs the Plasma applet under
`/usr/share/plasma/plasmoids` and the compiled QML module under
`/usr/lib/qt6/qml`. Its install hook refreshes the desktop service cache with
`kbuildsycoca6` when that command is available.

The base package installs native ELF commands directly in `/usr/bin` and uses
Btrfs userspace tools, cryptsetup, systemd/udev, `coreutils`, and `util-linux` at
runtime. The base service exposes reduced current status and writes private
operational diagnostics to the system journal. Desktop notifications are owned
by `btrfs-backup-kde`.

## Reproducibility

Directories, file modes, owners, entry order, and timestamps are normalized with a fixed `SOURCE_DATE_EPOCH`. Rebuilding from the delivered source ZIP should produce identical SHA-256 sums for the tarball, package, and source ZIP.
