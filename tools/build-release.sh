#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob
export LC_ALL=C

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PKGBASE=btrfs-backup
PKGNAME=btrfs-backup
VERSION=0.2.0
PKGREL=1
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1787356800}"
DIST_DIR="$ROOT/dist"
TEST_MODE=auto
TARGET=all

HOST_MACHINE="$(uname -m)"
case "$HOST_MACHINE" in
    x86_64)
        ARCH=x86_64
        DEB_ARCH=amd64
        ;;
    aarch64|arm64)
        ARCH=aarch64
        DEB_ARCH=arm64
        ;;
    armv7l|armv7hl)
        ARCH=armv7h
        DEB_ARCH=armhf
        ;;
    *)
        ARCH="$HOST_MACHINE"
        DEB_ARCH="$HOST_MACHINE"
        ;;
esac

usage() {
    cat <<'USAGE'
Usage: tools/build-release.sh [options]

Options:
  --target NAME      Build target: all, source, arch, deb, rpm, tar-install, nix, ebuild, or pkgbuild (default: all).
  --full-tests       Run the complete mocked test suite; requires root.
  --static-tests     Run only syntax and render validation.
  --skip-tests       Do not run tests before packaging.
  --dist-dir PATH    Write artifacts to a different directory.
  -h, --help         Show this help.
USAGE
}

while (( $# > 0 )); do
    case "$1" in
        --target)
            [[ $# -ge 2 ]] || { printf '%s\n' '--target requires a value.' >&2; exit 2; }
            TARGET="$2"
            shift 2
            ;;
        --full-tests)
            TEST_MODE=full
            shift
            ;;
        --static-tests)
            TEST_MODE=static
            shift
            ;;
        --skip-tests)
            TEST_MODE=skip
            shift
            ;;
        --dist-dir)
            [[ $# -ge 2 ]] || { printf '%s\n' '--dist-dir requires a path.' >&2; exit 2; }
            DIST_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            exit 2
            ;;
    esac
done

case "$TARGET" in
    all|source|arch|deb|rpm|tar-install|nix|ebuild|pkgbuild) ;;
    *) printf 'Invalid --target value: %s\n' "$TARGET" >&2; exit 2 ;;
esac

require_commands() {
    local command_name
    local missing=()
    for command_name in "$@"; do
        command -v "$command_name" >/dev/null 2>&1 || missing+=("$command_name")
    done
    if (( ${#missing[@]} > 0 )); then
        printf 'Missing required commands: %s\n' "${missing[*]}" >&2
        exit 1
    fi
}

make_invoker_owned() {
    local path="$1"
    if [[ -n "${SUDO_UID:-}" && "$SUDO_UID" =~ ^[0-9]+$ ]]; then
        chown "$SUDO_UID:${SUDO_GID:-$SUDO_UID}" "$path"
    fi
}

require_commands awk basename bsdtar chmod cmake cp date find g++ gzip install make mkdir mktemp mv readlink rm sha256sum stat tar touch
if [[ "$TARGET" == all || "$TARGET" == arch ]]; then
    require_commands zstd
fi
if [[ "$TARGET" == all || "$TARGET" == deb ]]; then
    require_commands ar
fi

case "$TEST_MODE" in
    auto)
        if (( EUID == 0 )); then TEST_MODE=full; else TEST_MODE=static; fi
        ;;
    full)
        (( EUID == 0 )) || { printf '%s\n' 'Full tests require root.' >&2; exit 1; }
        ;;
    static|skip) ;;
    *) printf 'Internal error: invalid test mode %s\n' "$TEST_MODE" >&2; exit 1 ;;
esac

case "$TEST_MODE" in
    full) "$ROOT/tests/run-tests.sh" --full ;;
    static) "$ROOT/tests/run-tests.sh" --static-only ;;
    skip) printf '%s\n' 'WARNING: tests were skipped.' >&2 ;;
esac

TMP_ROOT="$(mktemp -d /tmp/btrfs-backup-release.XXXXXX)"
cleanup() {
    rm -rf -- "$TMP_ROOT"
}
trap cleanup EXIT

DIST_DIR="$(realpath -m -- "$DIST_DIR")"
case "$DIST_DIR" in
    /|/etc|/usr|/var|/home|/root)
        printf 'Refusing unsafe dist directory: %s\n' "$DIST_DIR" >&2
        exit 1
        ;;
esac
rm -rf -- "$DIST_DIR"
install -d -m0755 "$DIST_DIR"

SOURCE_NAME="$PKGBASE-$VERSION"
SOURCE_STAGE="$TMP_ROOT/$SOURCE_NAME"
PACKAGE_STAGE="$TMP_ROOT/package"
ZIP_STAGE="$TMP_ROOT/zip/$SOURCE_NAME"
SOURCE_ARCHIVE="$DIST_DIR/$SOURCE_NAME.tar.gz"
PACKAGE_ARCHIVE="$DIST_DIR/$PKGNAME-$VERSION-$PKGREL-$ARCH.pkg.tar.zst"
SOURCE_ZIP="$DIST_DIR/$SOURCE_NAME-source.zip"
DEB_ARCHIVE="$DIST_DIR/${PKGNAME}_${VERSION}-${PKGREL}_${DEB_ARCH}.deb"
RPM_PACKAGING_ARCHIVE="$DIST_DIR/$SOURCE_NAME-rpm-packaging.tar.gz"
INSTALL_TARBALL="$DIST_DIR/$SOURCE_NAME-install.tar.gz"
NIX_PACKAGING_ARCHIVE="$DIST_DIR/$SOURCE_NAME-nix-packaging.tar.gz"
EBUILD_PACKAGING_ARCHIVE="$DIST_DIR/$SOURCE_NAME-ebuild.tar.gz"
PKGBUILD_ARCHIVE="$DIST_DIR/$SOURCE_NAME-pkgbuild.tar.gz"
BUILD_OUTPUTS=()

create_deterministic_tar_gz() {
    local source_dir="$1"
    local destination="$2"
    local entry_name="$3"

    (
        cd "$(dirname -- "$source_dir")"
        tar \
            --sort=name \
            --format=posix \
            --pax-option=delete=atime,delete=ctime \
            --owner=0 --group=0 --numeric-owner \
            --mtime="@$SOURCE_DATE_EPOCH" \
            -cf - "$entry_name" \
            | gzip -n -9 > "$destination"
    )
}

create_arch_mtree() {
    local root="$1"
    local mtree_plain="$TMP_ROOT/package.MTREE"
    local path rel mode size digest target

    {
        printf '%s\n' '#mtree'
        printf '%s\n' '/set type=file uid=0 gid=0 mode=644'
        while IFS= read -r -d '' path; do
            [[ "$(basename -- "$path")" == .MTREE ]] && continue
            rel="./${path#"$root"/}"
            mode="$(stat -c '%a' "$path")"
            if [[ -d "$path" ]]; then
                printf '%s time=%s.0 mode=%s type=dir\n' "$rel" "$SOURCE_DATE_EPOCH" "$mode"
            elif [[ -L "$path" ]]; then
                target="$(readlink -- "$path")"
                printf '%s time=%s.0 mode=%s type=link link=%s\n' "$rel" "$SOURCE_DATE_EPOCH" "$mode" "$target"
            elif [[ -f "$path" ]]; then
                size="$(stat -c '%s' "$path")"
                digest="$(sha256sum "$path" | awk '{print $1}')"
                if [[ "$mode" == 644 ]]; then
                    printf '%s time=%s.0 size=%s sha256digest=%s\n' "$rel" "$SOURCE_DATE_EPOCH" "$size" "$digest"
                else
                    printf '%s time=%s.0 mode=%s size=%s sha256digest=%s\n' "$rel" "$SOURCE_DATE_EPOCH" "$mode" "$size" "$digest"
                fi
            else
                printf 'Unsupported package entry: %s\n' "$path" >&2
                return 1
            fi
        done < <(find "$root" -mindepth 1 -print0 | LC_ALL=C sort -z)
    } > "$mtree_plain"

    gzip -n -9 < "$mtree_plain" > "$root/.MTREE"
    chmod 0644 "$root/.MTREE"
    touch -h -d "@$SOURCE_DATE_EPOCH" "$root/.MTREE"
}

create_deterministic_zip() {
    local source_dir="$1"
    local destination="$2"

    (
        cd "$(dirname -- "$source_dir")"
        mapfile -t zip_entries < <(find "$(basename -- "$source_dir")" -type f -printf '%p\n' | LC_ALL=C sort)
        bsdtar --format=zip -cf "$destination" "${zip_entries[@]}"
    )
}

copy_source_tree() {
    local destination="$1"
    install -d -m0755 "$destination"
    local entry
    for entry in bin config cpp docs install scripts systemd tests tools udev; do
        cp -a -- "$ROOT/$entry" "$destination/"
    done
    for entry in README.md CHANGELOG.md TODO.md LICENSE .gitignore CMakeLists.txt Makefile btrfs-backup.install; do
        cp -a -- "$ROOT/$entry" "$destination/"
    done

    # Do not inherit setgid/default ACL-derived directory modes from the build
    # workspace. ZIP extraction recreates directories as 0755, so normalizing
    # here makes a rebuild from the source ZIP byte-for-byte reproducible.
    find "$destination" -type d -exec chmod 0755 {} + -exec chmod ug-s {} +
    while IFS= read -r -d '' staged_file; do
        if [[ -x "$staged_file" ]]; then
            chmod 0755 "$staged_file"
        else
            chmod 0644 "$staged_file"
        fi
    done < <(find "$destination" -type f -print0)

}

copy_source_tree "$SOURCE_STAGE"
find "$SOURCE_STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
(
    cd "$TMP_ROOT"
    tar \
        --sort=name \
        --format=posix \
        --pax-option=delete=atime,delete=ctime \
        --owner=0 --group=0 --numeric-owner \
        --mtime="@$SOURCE_DATE_EPOCH" \
        -cf - "$SOURCE_NAME" \
        | gzip -n -9 > "$SOURCE_ARCHIVE"
)

SOURCE_SHA256="$(sha256sum "$SOURCE_ARCHIVE" | awk '{print $1}')"
BUILD_OUTPUTS+=("$SOURCE_ARCHIVE")

stage_package_payload() {
    local root="$1"
    local pkgdir="$2"
    local document

    make -C "$root" >/dev/null
    install -Dm755 "$root/scripts/btrfs-backup.sh" \
        "$pkgdir/usr/lib/btrfs-backup/btrfs-backup.sh"
    install -Dm755 "$root/scripts/btrfs-backup-eject.sh" \
        "$pkgdir/usr/lib/btrfs-backup/btrfs-backup-eject.sh"
    install -Dm755 "$root/scripts/btrfs-backup-mount.sh" \
        "$pkgdir/usr/lib/btrfs-backup/btrfs-backup-mount.sh"
    install -Dm755 "$root/scripts/btrfs-backup-migrate-profile.sh" \
        "$pkgdir/usr/lib/btrfs-backup/btrfs-backup-migrate-profile.sh"
    install -Dm755 "$root/scripts/btrfs-backup-unplug.sh" \
        "$pkgdir/usr/lib/btrfs-backup/btrfs-backup-unplug.sh"
    install -Dm755 "$root/scripts/lib/btrfs-backup-common.sh" \
        "$pkgdir/usr/lib/btrfs-backup/lib/btrfs-backup-common.sh"
    install -Dm755 "$root/build/btrfs-backup-profile" \
        "$pkgdir/usr/lib/btrfs-backup/btrfs-backup-profile"
    install -Dm755 "$root/build/btrfs-backupctl" \
        "$pkgdir/usr/lib/btrfs-backup/btrfs-backupctl"

    install -Dm755 "$root/bin/btrfs-backup" "$pkgdir/usr/bin/btrfs-backup"
    install -Dm755 "$root/bin/btrfs-backup-profile" "$pkgdir/usr/bin/btrfs-backup-profile"
    install -Dm755 "$root/bin/btrfs-backupctl" "$pkgdir/usr/bin/btrfs-backupctl"
    install -Dm755 "$root/bin/btrfs-backup-eject" "$pkgdir/usr/bin/btrfs-backup-eject"
    install -Dm755 "$root/bin/btrfs-backup-mount" "$pkgdir/usr/bin/btrfs-backup-mount"
    install -Dm755 "$root/bin/btrfs-backup-migrate-profile" "$pkgdir/usr/bin/btrfs-backup-migrate-profile"
    install -Dm755 "$root/bin/btrfs-backup-unplug" "$pkgdir/usr/bin/btrfs-backup-unplug"
    install -Dm755 "$root/install/btrfs-backup-configure.sh" \
        "$pkgdir/usr/bin/btrfs-backup-configure"

    install -Dm644 "$root/config/configurator-answers.example" \
        "$pkgdir/usr/share/btrfs-backup/examples/config/configurator-answers.example"
    install -Dm644 "$root/config/profile.example.json" \
        "$pkgdir/usr/share/btrfs-backup/examples/config/profile.example.json"
    install -Dm644 "$root/config/profile.schema.json" \
        "$pkgdir/usr/share/btrfs-backup/examples/config/profile.schema.json"
    install -Dm644 "$root/config/crypttab.fragment.example" \
        "$pkgdir/usr/share/btrfs-backup/examples/config/crypttab.fragment.example"
    install -Dm644 "$root/config/fstab.fragment.example" \
        "$pkgdir/usr/share/btrfs-backup/examples/config/fstab.fragment.example"
    install -Dm644 "$root/systemd/btrfs-backup.service.example" \
        "$pkgdir/usr/share/btrfs-backup/examples/systemd/btrfs-backup.service.example"
    install -Dm644 "$root/systemd/btrfs-backup@.service.example" \
        "$pkgdir/usr/share/btrfs-backup/examples/systemd/btrfs-backup@.service.example"
    install -Dm644 "$root/udev/99-btrfs-backup.rules.example" \
        "$pkgdir/usr/share/btrfs-backup/examples/udev/99-btrfs-backup.rules.example"
    install -d -m0755 "$pkgdir/usr/lib/systemd/system"
    sed \
        -e 's#{{BACKUP_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup.sh#g' \
        -e 's#{{EJECT_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup-eject.sh#g' \
        "$root/systemd/btrfs-backup@.service.example" \
        > "$pkgdir/usr/lib/systemd/system/btrfs-backup@.service"
    chmod 0644 "$pkgdir/usr/lib/systemd/system/btrfs-backup@.service"

    install -Dm644 "$root/README.md" "$pkgdir/usr/share/doc/btrfs-backup/README.md"
    install -Dm644 "$root/CHANGELOG.md" "$pkgdir/usr/share/doc/btrfs-backup/CHANGELOG.md"
    install -Dm644 "$root/TODO.md" "$pkgdir/usr/share/doc/btrfs-backup/TODO.md"
    for document in "$root"/docs/*.md; do
        install -Dm644 "$document" "$pkgdir/usr/share/doc/btrfs-backup/$(basename -- "$document")"
    done
    install -Dm644 "$root/LICENSE" "$pkgdir/usr/share/licenses/$PKGNAME/LICENSE"
}

build_deb_package() {
    local root="$1"
    local workdir="$2"
    local data_root="$workdir/data"
    local control_root="$workdir/control"
    local data_archive="$workdir/data.tar.gz"
    local control_archive="$workdir/control.tar.gz"
    local debian_binary="$workdir/debian-binary"

    install -d -m0755 "$data_root" "$control_root"
    stage_package_payload "$root" "$data_root"
    find "$data_root" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

    cat > "$control_root/control" <<EOF_CONTROL
Package: $PKGNAME
Version: $VERSION-$PKGREL
Section: admin
Priority: optional
Architecture: $DEB_ARCH
Maintainer: local reproducible build <root@localhost>
Depends: bash, btrfs-progs, coreutils, cryptsetup, findutils, gawk, grep, libstdc++6, sed, systemd, util-linux
Recommends: pv, libnotify-bin
Description: Verified Btrfs send/receive backups to an encrypted removable target
 systemd and udev driven Btrfs send/receive backups with LUKS target validation,
 interrupted-run recovery, retention, and controlled eject.
EOF_CONTROL

    cat > "$control_root/postinst" <<'EOF_POSTINST'
#!/bin/sh
set -e
echo "btrfs-backup installed. Run: btrfs-backup-configure --render-only --output-dir ./generated"
exit 0
EOF_POSTINST
    chmod 0755 "$control_root/postinst"
    find "$control_root" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

    (
        cd "$data_root"
        tar --sort=name --format=posix --pax-option=delete=atime,delete=ctime \
            --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
            -cf - . | gzip -n -9 > "$data_archive"
    )
    (
        cd "$control_root"
        tar --sort=name --format=posix --pax-option=delete=atime,delete=ctime \
            --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
            -cf - . | gzip -n -9 > "$control_archive"
    )

    printf '2.0\n' > "$debian_binary"
    touch -d "@$SOURCE_DATE_EPOCH" "$debian_binary" "$control_archive" "$data_archive"
    rm -f -- "$DEB_ARCHIVE"
    (
        cd "$workdir"
        ar qc "$DEB_ARCHIVE" debian-binary control.tar.gz data.tar.gz
    )
}

build_install_tarball() {
    local root="$1"
    local workdir="$2"
    local payload="$workdir/$SOURCE_NAME-install"

    install -d -m0755 "$payload"
    stage_package_payload "$root" "$payload"
    install -Dm0644 "$root/btrfs-backup.install" "$payload/usr/share/doc/btrfs-backup/btrfs-backup.install"
    find "$payload" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    create_deterministic_tar_gz "$payload" "$INSTALL_TARBALL" "$(basename -- "$payload")"
}

build_rpm_packaging() {
    local workdir="$1"
    local package_dir="$workdir/$SOURCE_NAME-rpm-packaging"

    install -d -m0755 "$package_dir"
    cat > "$package_dir/$PKGNAME.spec" <<EOF_SPEC
Name:           $PKGNAME
Version:        $VERSION
Release:        $PKGREL%{?dist}
Summary:        Verified Btrfs send/receive backups to an encrypted removable target
License:        GPL-3.0-or-later
BuildArch:      noarch
Source0:        %{name}-%{version}.tar.gz

Requires:       bash
Requires:       btrfs-progs
Requires:       coreutils
Requires:       cryptsetup
Requires:       findutils
Requires:       gawk
Requires:       grep
Requires:       libstdc++
Requires:       sed
Requires:       systemd
Requires:       util-linux

%description
systemd and udev driven Btrfs send/receive backups with LUKS target validation,
interrupted-run recovery, retention, and controlled eject.

%prep
%autosetup

%build
%{__make}

%install
install -Dm755 scripts/btrfs-backup.sh %{buildroot}%{_libdir}/btrfs-backup/btrfs-backup.sh
install -Dm755 scripts/btrfs-backup-eject.sh %{buildroot}%{_libdir}/btrfs-backup/btrfs-backup-eject.sh
install -Dm755 scripts/btrfs-backup-mount.sh %{buildroot}%{_libdir}/btrfs-backup/btrfs-backup-mount.sh
install -Dm755 scripts/btrfs-backup-migrate-profile.sh %{buildroot}%{_libdir}/btrfs-backup/btrfs-backup-migrate-profile.sh
install -Dm755 scripts/btrfs-backup-unplug.sh %{buildroot}%{_libdir}/btrfs-backup/btrfs-backup-unplug.sh
install -Dm755 scripts/lib/btrfs-backup-common.sh %{buildroot}%{_libdir}/btrfs-backup/lib/btrfs-backup-common.sh
install -Dm755 build/btrfs-backup-profile %{buildroot}%{_libdir}/btrfs-backup/btrfs-backup-profile
install -Dm755 build/btrfs-backupctl %{buildroot}%{_libdir}/btrfs-backup/btrfs-backupctl
install -Dm755 bin/btrfs-backup %{buildroot}%{_bindir}/btrfs-backup
install -Dm755 bin/btrfs-backup-profile %{buildroot}%{_bindir}/btrfs-backup-profile
install -Dm755 bin/btrfs-backupctl %{buildroot}%{_bindir}/btrfs-backupctl
install -Dm755 bin/btrfs-backup-eject %{buildroot}%{_bindir}/btrfs-backup-eject
install -Dm755 bin/btrfs-backup-mount %{buildroot}%{_bindir}/btrfs-backup-mount
install -Dm755 bin/btrfs-backup-migrate-profile %{buildroot}%{_bindir}/btrfs-backup-migrate-profile
install -Dm755 bin/btrfs-backup-unplug %{buildroot}%{_bindir}/btrfs-backup-unplug
install -Dm755 install/btrfs-backup-configure.sh %{buildroot}%{_bindir}/btrfs-backup-configure
install -d %{buildroot}/usr/lib/systemd/system
sed -e 's#{{BACKUP_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup.sh#g' \
    -e 's#{{EJECT_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup-eject.sh#g' \
    systemd/btrfs-backup@.service.example \
    > %{buildroot}/usr/lib/systemd/system/btrfs-backup@.service
install -d %{buildroot}%{_datadir}/btrfs-backup/examples
cp -a config systemd udev %{buildroot}%{_datadir}/btrfs-backup/examples/
install -Dm644 README.md %{buildroot}%{_docdir}/btrfs-backup/README.md
install -Dm644 CHANGELOG.md %{buildroot}%{_docdir}/btrfs-backup/CHANGELOG.md
install -Dm644 TODO.md %{buildroot}%{_docdir}/btrfs-backup/TODO.md
cp -a docs/*.md %{buildroot}%{_docdir}/btrfs-backup/
install -Dm644 LICENSE %{buildroot}%{_licensedir}/btrfs-backup/LICENSE

%files
%{_bindir}/btrfs-backup
%{_bindir}/btrfs-backup-profile
%{_bindir}/btrfs-backupctl
%{_bindir}/btrfs-backup-eject
%{_bindir}/btrfs-backup-mount
%{_bindir}/btrfs-backup-migrate-profile
%{_bindir}/btrfs-backup-unplug
%{_bindir}/btrfs-backup-configure
%{_libdir}/btrfs-backup/btrfs-backupctl
%{_libdir}/btrfs-backup/
/usr/lib/systemd/system/btrfs-backup@.service
%{_datadir}/btrfs-backup/
%{_docdir}/btrfs-backup/
%{_licensedir}/btrfs-backup/

%changelog
* Sun Aug 23 2026 local reproducible build <root@localhost> - $VERSION-$PKGREL
- Upstream release.
EOF_SPEC
    find "$package_dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    create_deterministic_tar_gz "$package_dir" "$RPM_PACKAGING_ARCHIVE" "$(basename -- "$package_dir")"
}

build_nix_packaging() {
    local workdir="$1"
    local package_dir="$workdir/$SOURCE_NAME-nix-packaging"

    install -d -m0755 "$package_dir"
    cat > "$package_dir/package.nix" <<'EOF_NIX'
{ lib
, stdenvNoCC
, bash
, btrfs-progs
, coreutils
, cryptsetup
, findutils
, gawk
, gnugrep
, cmake
, gcc
, nlohmann_json
, gnused
, systemd
, util-linux
}:

stdenvNoCC.mkDerivation {
  pname = "btrfs-backup";
  version = "$VERSION";

  src = ./.;

  dontBuild = true;
  nativeBuildInputs = [ cmake gcc nlohmann_json ];

  installPhase = ''
    runHook preInstall
    make
    install -Dm755 scripts/btrfs-backup.sh $out/lib/btrfs-backup/btrfs-backup.sh
    install -Dm755 scripts/btrfs-backup-eject.sh $out/lib/btrfs-backup/btrfs-backup-eject.sh
    install -Dm755 scripts/btrfs-backup-mount.sh $out/lib/btrfs-backup/btrfs-backup-mount.sh
    install -Dm755 scripts/btrfs-backup-migrate-profile.sh $out/lib/btrfs-backup/btrfs-backup-migrate-profile.sh
    install -Dm755 scripts/btrfs-backup-unplug.sh $out/lib/btrfs-backup/btrfs-backup-unplug.sh
    install -Dm755 scripts/lib/btrfs-backup-common.sh $out/lib/btrfs-backup/lib/btrfs-backup-common.sh
    install -Dm755 build/btrfs-backup-profile $out/lib/btrfs-backup/btrfs-backup-profile
    install -Dm755 build/btrfs-backupctl $out/lib/btrfs-backup/btrfs-backupctl
    install -Dm755 bin/btrfs-backup $out/bin/btrfs-backup
    install -Dm755 bin/btrfs-backup-profile $out/bin/btrfs-backup-profile
    install -Dm755 bin/btrfs-backupctl $out/bin/btrfs-backupctl
    install -Dm755 bin/btrfs-backup-eject $out/bin/btrfs-backup-eject
    install -Dm755 bin/btrfs-backup-mount $out/bin/btrfs-backup-mount
    install -Dm755 bin/btrfs-backup-migrate-profile $out/bin/btrfs-backup-migrate-profile
    install -Dm755 bin/btrfs-backup-unplug $out/bin/btrfs-backup-unplug
    install -Dm755 install/btrfs-backup-configure.sh $out/bin/btrfs-backup-configure
    mkdir -p $out/lib/systemd/system
    sed -e 's#{{BACKUP_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup.sh#g' \
        -e 's#{{EJECT_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup-eject.sh#g' \
        systemd/btrfs-backup@.service.example \
        > $out/lib/systemd/system/btrfs-backup@.service
    mkdir -p $out/share/btrfs-backup/examples
    cp -a config systemd udev $out/share/btrfs-backup/examples/
    install -Dm644 README.md $out/share/doc/btrfs-backup/README.md
    install -Dm644 CHANGELOG.md $out/share/doc/btrfs-backup/CHANGELOG.md
    install -Dm644 TODO.md $out/share/doc/btrfs-backup/TODO.md
    cp -a docs/*.md $out/share/doc/btrfs-backup/
    install -Dm644 LICENSE $out/share/licenses/btrfs-backup/LICENSE
    runHook postInstall
  '';

  meta = {
    description = "Verified Btrfs send/receive backups to an encrypted removable target";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
  };
}
EOF_NIX
    cat > "$package_dir/README.md" <<EOF_NIX_README
# Nix packaging

This is a packaging skeleton for the upstream btrfs-backup source tree. It packages the commands and examples, but host activation of systemd units, udev rules, and trusted configuration should be handled by a NixOS module.
EOF_NIX_README
    find "$package_dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    create_deterministic_tar_gz "$package_dir" "$NIX_PACKAGING_ARCHIVE" "$(basename -- "$package_dir")"
}

build_ebuild_packaging() {
    local workdir="$1"
    local package_dir="$workdir/$SOURCE_NAME-ebuild"
    local ebuild="$package_dir/$PKGNAME-$VERSION.ebuild"

    install -d -m0755 "$package_dir"
    cat > "$ebuild" <<'EOF_EBUILD'
EAPI=8

DESCRIPTION="Verified Btrfs send/receive backups to an encrypted removable target"
HOMEPAGE="https://github.com/kamil/btrfs-backup"
SRC_URI="https://github.com/kamil/btrfs-backup/releases/download/v${PV}/btrfs-backup-${PV}.tar.gz"
LICENSE="GPL-3+"
SLOT="0"
KEYWORDS="~amd64"

RDEPEND="
	app-shells/bash
	sys-fs/btrfs-progs
	sys-fs/cryptsetup
	sys-apps/coreutils
	sys-apps/findutils
	sys-apps/gawk
	sys-apps/grep
	dev-cpp/nlohmann_json
	dev-build/cmake
	sys-devel/gcc
	sys-apps/sed
	sys-apps/systemd
	sys-apps/util-linux
"

src_install() {
	emake
	dobin bin/btrfs-backup bin/btrfs-backup-profile bin/btrfs-backupctl bin/btrfs-backup-eject bin/btrfs-backup-mount bin/btrfs-backup-migrate-profile bin/btrfs-backup-unplug
	dobin install/btrfs-backup-configure.sh
	exeinto /usr/lib/btrfs-backup
	doexe scripts/btrfs-backup.sh scripts/btrfs-backup-eject.sh scripts/btrfs-backup-mount.sh scripts/btrfs-backup-migrate-profile.sh scripts/btrfs-backup-unplug.sh
	exeinto /usr/lib/btrfs-backup/lib
	doexe scripts/lib/btrfs-backup-common.sh
	exeinto /usr/lib/btrfs-backup
	doexe build/btrfs-backup-profile
	doexe build/btrfs-backupctl
	sed -e 's#{{BACKUP_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup.sh#g' \
		-e 's#{{EJECT_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup-eject.sh#g' \
		systemd/btrfs-backup@.service.example > "${T}/btrfs-backup@.service"
	insinto /usr/lib/systemd/system
	doins "${T}/btrfs-backup@.service"
	insinto /usr/share/btrfs-backup/examples
	doins -r config systemd udev
	dodoc README.md CHANGELOG.md TODO.md docs/*.md
}
EOF_EBUILD
    sha256sum "$SOURCE_ARCHIVE" > "$package_dir/source.SHA256SUM"
    find "$package_dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    create_deterministic_tar_gz "$package_dir" "$EBUILD_PACKAGING_ARCHIVE" "$(basename -- "$package_dir")"
}

build_pkgbuild_packaging() {
    local workdir="$1"
    local package_dir="$workdir/$SOURCE_NAME-pkgbuild"

    install -d -m0755 "$package_dir"
    cat > "$package_dir/PKGBUILD" <<EOF_PKGBUILD
# Maintainer: local package
pkgbase=btrfs-backup
pkgname=btrfs-backup
pkgver=$VERSION
pkgrel=$PKGREL
pkgdesc='Verified Btrfs send/receive backups to an encrypted removable target'
arch=('$ARCH')
license=('GPL-3.0-or-later')
depends=('bash' 'btrfs-progs' 'coreutils' 'cryptsetup' 'findutils' 'gawk' 'gcc-libs' 'grep' 'sed' 'systemd' 'util-linux')
makedepends=('cmake' 'gcc' 'nlohmann-json')
optdepends=('libnotify: desktop notifications via notify-send' 'pv: live progress during btrfs send')
install="\$pkgname.install"
source=("\$pkgbase-\$pkgver.tar.gz")
sha256sums=('$SOURCE_SHA256')

check() {
  cd "\$srcdir/\$pkgbase-\$pkgver"
  ./tests/run-tests.sh --static-only
}

package() {
  local root="\$srcdir/\$pkgbase-\$pkgver"
  make -C "\$root"
  install -Dm755 "\$root/scripts/btrfs-backup.sh" "\$pkgdir/usr/lib/btrfs-backup/btrfs-backup.sh"
  install -Dm755 "\$root/scripts/btrfs-backup-eject.sh" "\$pkgdir/usr/lib/btrfs-backup/btrfs-backup-eject.sh"
  install -Dm755 "\$root/scripts/btrfs-backup-mount.sh" "\$pkgdir/usr/lib/btrfs-backup/btrfs-backup-mount.sh"
  install -Dm755 "\$root/scripts/btrfs-backup-migrate-profile.sh" "\$pkgdir/usr/lib/btrfs-backup/btrfs-backup-migrate-profile.sh"
  install -Dm755 "\$root/scripts/btrfs-backup-unplug.sh" "\$pkgdir/usr/lib/btrfs-backup/btrfs-backup-unplug.sh"
  install -Dm755 "\$root/scripts/lib/btrfs-backup-common.sh" "\$pkgdir/usr/lib/btrfs-backup/lib/btrfs-backup-common.sh"
  install -Dm755 "\$root/build/btrfs-backup-profile" "\$pkgdir/usr/lib/btrfs-backup/btrfs-backup-profile"
  install -Dm755 "\$root/build/btrfs-backupctl" "\$pkgdir/usr/lib/btrfs-backup/btrfs-backupctl"
  install -Dm755 "\$root/bin/btrfs-backup" "\$pkgdir/usr/bin/btrfs-backup"
  install -Dm755 "\$root/bin/btrfs-backup-profile" "\$pkgdir/usr/bin/btrfs-backup-profile"
  install -Dm755 "\$root/bin/btrfs-backupctl" "\$pkgdir/usr/bin/btrfs-backupctl"
  install -Dm755 "\$root/bin/btrfs-backup-eject" "\$pkgdir/usr/bin/btrfs-backup-eject"
  install -Dm755 "\$root/bin/btrfs-backup-mount" "\$pkgdir/usr/bin/btrfs-backup-mount"
  install -Dm755 "\$root/bin/btrfs-backup-migrate-profile" "\$pkgdir/usr/bin/btrfs-backup-migrate-profile"
  install -Dm755 "\$root/bin/btrfs-backup-unplug" "\$pkgdir/usr/bin/btrfs-backup-unplug"
  install -Dm755 "\$root/install/btrfs-backup-configure.sh" "\$pkgdir/usr/bin/btrfs-backup-configure"
  install -d "\$pkgdir/usr/lib/systemd/system"
  sed -e 's#{{BACKUP_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup.sh#g' \\
      -e 's#{{EJECT_SCRIPT_PATH}}#/usr/lib/btrfs-backup/btrfs-backup-eject.sh#g' \\
      "\$root/systemd/btrfs-backup@.service.example" \\
      > "\$pkgdir/usr/lib/systemd/system/btrfs-backup@.service"
  install -d "\$pkgdir/usr/share/btrfs-backup/examples"
  cp -a "\$root/config" "\$root/systemd" "\$root/udev" "\$pkgdir/usr/share/btrfs-backup/examples/"
  install -Dm644 "\$root/README.md" "\$pkgdir/usr/share/doc/btrfs-backup/README.md"
  install -Dm644 "\$root/CHANGELOG.md" "\$pkgdir/usr/share/doc/btrfs-backup/CHANGELOG.md"
  install -Dm644 "\$root/TODO.md" "\$pkgdir/usr/share/doc/btrfs-backup/TODO.md"
  install -Dm644 "\$root"/docs/*.md -t "\$pkgdir/usr/share/doc/btrfs-backup/"
  install -Dm644 "\$root/LICENSE" "\$pkgdir/usr/share/licenses/\$pkgname/LICENSE"
}
EOF_PKGBUILD
    cp -a -- "$ROOT/btrfs-backup.install" "$package_dir/btrfs-backup.install"
    cat > "$package_dir/.SRCINFO" <<EOF_SRCINFO
pkgbase = btrfs-backup
	pkgdesc = Verified Btrfs send/receive backups to an encrypted removable target
	pkgver = $VERSION
	pkgrel = $PKGREL
	url = https://github.com/kamil/btrfs-backup
	arch = $ARCH
	license = GPL-3.0-or-later
	depends = bash
	depends = btrfs-progs
	depends = coreutils
	depends = cryptsetup
	depends = findutils
	depends = gawk
	depends = gcc-libs
	depends = grep
	depends = sed
	depends = systemd
	depends = util-linux
	makedepends = cmake
	makedepends = gcc
	makedepends = nlohmann-json
	optdepends = libnotify: desktop notifications via notify-send
	optdepends = pv: live progress during btrfs send
	source = btrfs-backup-$VERSION.tar.gz
	sha256sums = $SOURCE_SHA256

pkgname = btrfs-backup
EOF_SRCINFO
    find "$package_dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    create_deterministic_tar_gz "$package_dir" "$PKGBUILD_ARCHIVE" "$(basename -- "$package_dir")"
}

if [[ "$TARGET" == all || "$TARGET" == arch ]]; then
    install -d -m0755 "$PACKAGE_STAGE"
    stage_package_payload "$SOURCE_STAGE" "$PACKAGE_STAGE"
    install -m0644 "$SOURCE_STAGE/btrfs-backup.install" "$PACKAGE_STAGE/.INSTALL"

    INSTALLED_SIZE="$(find "$PACKAGE_STAGE/usr" -type f -printf '%s\n' | awk '{sum += $1} END {print sum + 0}')"
    BUILD_DATE="$SOURCE_DATE_EPOCH"
    cat > "$PACKAGE_STAGE/.PKGINFO" <<EOF_PKGINFO
# Generated by tools/build-release.sh
pkgname = $PKGNAME
pkgbase = $PKGBASE
xdata = pkgtype=pkg
pkgver = $VERSION-$PKGREL
pkgdesc = Verified Btrfs send/receive backups to an encrypted removable target
builddate = $BUILD_DATE
packager = local reproducible build
size = $INSTALLED_SIZE
arch = $ARCH
license = GPL-3.0-or-later
depend = bash
depend = btrfs-progs
depend = coreutils
depend = cryptsetup
depend = findutils
depend = gawk
depend = gcc-libs
depend = grep
depend = sed
depend = systemd
depend = util-linux
optdepend = libnotify: desktop notifications via notify-send
optdepend = pv: live progress during btrfs send
EOF_PKGINFO
    chmod 0644 "$PACKAGE_STAGE/.PKGINFO" "$PACKAGE_STAGE/.INSTALL"
    find "$PACKAGE_STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    create_arch_mtree "$PACKAGE_STAGE"

    (
        cd "$PACKAGE_STAGE"
        mapfile -t package_entries < <(find . -mindepth 1 -maxdepth 1 -printf '%P\n' | LC_ALL=C sort)
        bsdtar \
            --uid 0 --gid 0 --uname root --gname root \
            --zstd -cf "$PACKAGE_ARCHIVE" \
            "${package_entries[@]}"
    )
    BUILD_OUTPUTS+=("$PACKAGE_ARCHIVE")
fi

if [[ "$TARGET" == all || "$TARGET" == deb ]]; then
    build_deb_package "$SOURCE_STAGE" "$TMP_ROOT/deb"
    BUILD_OUTPUTS+=("$DEB_ARCHIVE")
fi

if [[ "$TARGET" == all || "$TARGET" == tar-install ]]; then
    build_install_tarball "$SOURCE_STAGE" "$TMP_ROOT/tar-install"
    BUILD_OUTPUTS+=("$INSTALL_TARBALL")
fi

if [[ "$TARGET" == all || "$TARGET" == rpm ]]; then
    build_rpm_packaging "$TMP_ROOT/rpm"
    BUILD_OUTPUTS+=("$RPM_PACKAGING_ARCHIVE")
fi

if [[ "$TARGET" == all || "$TARGET" == nix ]]; then
    build_nix_packaging "$TMP_ROOT/nix"
    BUILD_OUTPUTS+=("$NIX_PACKAGING_ARCHIVE")
fi

if [[ "$TARGET" == all || "$TARGET" == ebuild ]]; then
    build_ebuild_packaging "$TMP_ROOT/ebuild"
    BUILD_OUTPUTS+=("$EBUILD_PACKAGING_ARCHIVE")
fi

if [[ "$TARGET" == all || "$TARGET" == pkgbuild ]]; then
    build_pkgbuild_packaging "$TMP_ROOT/pkgbuild"
    BUILD_OUTPUTS+=("$PKGBUILD_ARCHIVE")
fi

copy_source_tree "$ZIP_STAGE"
find "$ZIP_STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
create_deterministic_zip "$ZIP_STAGE" "$SOURCE_ZIP"
BUILD_OUTPUTS+=("$SOURCE_ZIP")

cat > "$DIST_DIR/BUILD-REPORT.txt" <<EOF_REPORT
Package: $PKGNAME
Package base: $PKGBASE
Version: $VERSION-$PKGREL
Architecture: $ARCH
Source date epoch: $SOURCE_DATE_EPOCH
Tests: $TEST_MODE
Target: $TARGET
Source SHA-256: $SOURCE_SHA256

The automated suite uses controlled command mocks. A real Btrfs/LUKS restore test is still required on the deployment host before relying on the backup operationally.
EOF_REPORT
BUILD_OUTPUTS+=("$DIST_DIR/BUILD-REPORT.txt")

(
    cd "$DIST_DIR"
    : > SHA256SUMS
    for artifact in "${BUILD_OUTPUTS[@]}"; do
        sha256sum "$(basename -- "$artifact")" >> SHA256SUMS
    done
    sha256sum -c SHA256SUMS
)

if [[ "$TARGET" == all || "$TARGET" == arch ]]; then
    # Structural verification of the Arch package.
    tar --zstd -tf "$PACKAGE_ARCHIVE" > "$TMP_ROOT/package-files.txt"
    grep -qx '.PKGINFO' "$TMP_ROOT/package-files.txt"
    grep -qx '.INSTALL' "$TMP_ROOT/package-files.txt"
    grep -qx '.MTREE' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/bin/btrfs-backup' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/bin/btrfs-backup-profile' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/bin/btrfs-backupctl' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/bin/btrfs-backup-configure' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/bin/btrfs-backup-mount' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/bin/btrfs-backup-migrate-profile' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/lib/btrfs-backup/btrfs-backup.sh' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/lib/btrfs-backup/btrfs-backup-mount.sh' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/lib/btrfs-backup/btrfs-backup-migrate-profile.sh' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/lib/btrfs-backup/btrfs-backup-profile' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/lib/btrfs-backup/btrfs-backupctl' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/lib/systemd/system/btrfs-backup@.service' "$TMP_ROOT/package-files.txt"
    grep -qx 'usr/share/btrfs-backup/examples/config/profile.schema.json' "$TMP_ROOT/package-files.txt"
    if command -v pacman >/dev/null 2>&1; then
        pacman -Qip "$PACKAGE_ARCHIVE" >/dev/null
    fi

    # Verify that the package can also be inspected and smoke-tested before installation.
    PACKAGE_AUDIT_ROOT="$TMP_ROOT/package-audit"
    mkdir -p "$PACKAGE_AUDIT_ROOT"
    tar --zstd -xf "$PACKAGE_ARCHIVE" -C "$PACKAGE_AUDIT_ROOT"
    while IFS= read -r -d '' packaged_script; do
        if [[ "$(head -c 4 "$packaged_script")" == $'\177ELF' ]]; then
            continue
        fi
        bash -n "$packaged_script"
    done < <(find "$PACKAGE_AUDIT_ROOT/usr/bin" "$PACKAGE_AUDIT_ROOT/usr/lib/btrfs-backup" -type f -print0)
    bash -n "$PACKAGE_AUDIT_ROOT/.INSTALL"
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup" --help >/dev/null
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup-profile" --help >/dev/null
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backupctl" --help >/dev/null
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup-eject" --help >/dev/null
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup-mount" --help >/dev/null
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup-migrate-profile" --help >/dev/null
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup-unplug" --help >/dev/null 2>&1
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup-configure" --help >/dev/null

    PACKAGE_ANSWERS="$TMP_ROOT/package-answers.conf"
    cat > "$PACKAGE_ANSWERS" <<'EOF_PACKAGE_ANSWERS'
BACKUP_DEVICE=/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555
BACKUP_LUKS_UUID=11111111-2222-3333-4444-555555555555
BACKUP_BTRFS_UUID=66666666-7777-8888-9999-aaaaaaaaaaaa
BACKUP_UDEV_MATCH='ENV{DEVTYPE}=="partition", ENV{ID_FS_TYPE}=="crypto_LUKS", ENV{ID_FS_UUID}=="11111111-2222-3333-4444-555555555555"'
BACKUP_MAPPER_NAME=backupdisk
BACKUP_MOUNTPOINT=/mnt/backup
KEYFILE_PATH_OR_NONE=none
SOURCE_SUBVOLUMES=(/ /home)
SOURCE_NAMES=(root home)
LOCAL_SNAPSHOT_DIRS=(/.snapshots/btrfs-backup/root /.snapshots/btrfs-backup/home)
REMOTE_SUBDIRS=(root home)
SOURCE_RETENTION_COUNTS=(30 30)
SOURCE_LOCAL_RETENTION_COUNTS=(30 30)
NOTIFY_ENABLE=false
NOTIFY_USER=root
NOTIFY_METHOD=none
EOF_PACKAGE_ANSWERS
    chmod 0600 "$PACKAGE_ANSWERS"
    make_invoker_owned "$PACKAGE_ANSWERS"
    "$PACKAGE_AUDIT_ROOT/usr/bin/btrfs-backup-configure" \
        --render-only \
        --answers "$PACKAGE_ANSWERS" \
        --output-dir "$TMP_ROOT/package-rendered" >/dev/null

    tar --zstd -xOf "$PACKAGE_ARCHIVE" .PKGINFO | grep -qx "arch = $ARCH"
fi

printf '\nBuilt release artifacts:\n'
printf '  %s\n' "${BUILD_OUTPUTS[@]}" "$DIST_DIR/SHA256SUMS"
