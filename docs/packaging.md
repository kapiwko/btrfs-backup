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
btrfs-backup-0.2.2.tar.gz
btrfs-backup-0.2.2-1-x86_64.pkg.tar.zst
btrfs-backup-kde-0.2.2-1-x86_64.pkg.tar.zst
btrfs-backup_0.2.2-1_amd64.deb
btrfs-backup-0.2.2-install.tar.gz
btrfs-backup-0.2.2-rpm-packaging.tar.gz
btrfs-backup-0.2.2-nix-packaging.tar.gz
btrfs-backup-0.2.2-ebuild.tar.gz
btrfs-backup-0.2.2-pkgbuild.tar.gz
btrfs-backup-0.2.2-source.zip
SHA256SUMS
BUILD-REPORT.txt
```

The `source` target uses the source archive toolchain. Arch package construction
additionally uses `zstd`; other package backends may have their own tool
requirements.
Building from source requires CMake, a C++20 compiler, `pkg-config`,
`nlohmann-json`, `libmount`, `libblkid`, `libudev`, and `libbtrfsutil`
development files for the native code under `cpp/`.
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

## Package Contents

Native commands are installed directly under `/usr/bin`, the profile systemd unit under
`/usr/lib/systemd/system`, examples under `/usr/share/btrfs-backup/examples`,
and documentation under `/usr/share/doc/btrfs-backup`. The base package creates
the trusted hook directory `/etc/btrfs-backup/hooks.d` as `root:root 0755` but
does not install or enable any hook programs.

The public command surface is `btrfs-backup` and `btrfs-backupctl`. Target
mount and eject operations are `btrfs-backupctl target mount` and
`btrfs-backupctl target eject`; standalone mount/eject wrapper commands are no
longer packaged.

The package does not install active fstab, crypttab, or udev entries.
`btrfs-backupctl profile wizard --apply` and `profile save` write active
configuration and udev files only after an explicit user command.

The optional `btrfs-backup-kde` package installs the Plasma applet under
`/usr/share/plasma/plasmoids` and the compiled QML module under
`/usr/lib/qt6/qml`. Its install hook refreshes the desktop service cache with
`kbuildsycoca6` when that command is available.

The base package installs native ELF commands directly in `/usr/bin` and uses
Btrfs userspace tools, cryptsetup, systemd/udev, `coreutils`, and `util-linux` at
runtime. The base service exposes status/history and writes operational
diagnostics to the system journal. Desktop notifications are owned by
`btrfs-backup-kde`.

## Reproducibility

Directories, file modes, owners, entry order, and timestamps are normalized with a fixed `SOURCE_DATE_EPOCH`. Rebuilding from the delivered source ZIP should produce identical SHA-256 sums for the tarball, package, and source ZIP.
