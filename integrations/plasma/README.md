# Plasma Integration

This directory contains optional Plasma 6 integration. It is intentionally kept
outside the main CMake project so the system backup runtime does not depend on
Qt Quick, Kirigami, Plasma or a graphical session.

The first component is a plasmoid with a small C++ QML backend. The backend
reads the public JSON status stream:

```bash
btrfs-backupctl status watch --profile default --json
```

QML only binds to C++ properties. It does not execute shell command strings and
does not parse textual CLI output.

## Build

```bash
cmake -S integrations/plasma -B /tmp/btrfs-backup-plasma-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/btrfs-backup-plasma-build
ctest --test-dir /tmp/btrfs-backup-plasma-build --output-on-failure
cmake --install /tmp/btrfs-backup-plasma-build --prefix "$HOME/.local"
```

The compiled QML module is installed under `lib/qt6/qml` relative to the chosen
prefix. On a local prefix, run test tools with:

```bash
QML2_IMPORT_PATH="$HOME/.local/lib/qt6/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}" \
    plasmawindowed org.btrfsbackup.plasmoid
```

For local package iteration, use the CMake install step above. Installing only
`integrations/plasma/package` with `kpackagetool6` is not enough because the
plasmoid also needs the compiled `org.btrfsbackup.plasma` QML module.

After installing both the plasmoid and the QML module:

```bash
plasmawindowed org.btrfsbackup.plasmoid
```

## Roadmap

This package is a stepping stone toward the planned native desktop layer:

1. shared C++ client library for the versioned system D-Bus API;
2. session monitor using KJob, KUiServer and KNotifications for long-running
   backup progress;
3. KCM for profile browsing, validation and controlled writes through the
   system service;
4. removal of direct CLI use from the Plasma backend once the D-Bus manager is
   available.

The base backup package must keep working without this integration installed.
