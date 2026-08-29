# Plasma Integration

This directory contains optional Plasma 6 integration. It is intentionally kept
outside the main CMake project so the system backup runtime does not depend on
Qt Quick, Kirigami, Plasma or a graphical session.

The first component is a plasmoid with a small C++ QML backend. The backend
uses the implemented system manager D-Bus API:

```text
io.github.btrfsbackup.Manager1
/io/github/btrfsbackup/Manager1
```

QML only binds to C++ properties. The C++ model validates manager capabilities,
loads public profiles, status, target state and sanitized history, and invokes
authorized operations asynchronously. It does not spawn `btrfs-backupctl` or
block the UI thread on D-Bus calls.

`managerConnected` reports whether a compatible manager has answered. It must
not be interpreted as target-device connectivity; target lifecycle is a
separate `GetDeviceState` contract.

The plasmoid offers start, run-scoped cancellation, target validation and eject
through the manager's polkit-protected methods. Target removal state is read
from the separate `GetDeviceState` response, never inferred from backup success.

## Build

```bash
cmake -S integrations/kde -B /tmp/btrfs-backup-plasma-build -DCMAKE_BUILD_TYPE=Release
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
`integrations/kde/plasmoid/package` with `kpackagetool6` is not enough because the
plasmoid also needs the compiled `org.btrfsbackup.plasma` QML module.

After installing both the plasmoid and the QML module:

```bash
plasmawindowed org.btrfsbackup.plasmoid
```

## Roadmap

This package is a stepping stone toward the planned native desktop layer:

1. extraction of the plasmoid's D-Bus client into a shared C++ client library;
2. session monitor using KJob, KUiServer and KNotifications for long-running
   backup progress;
3. KCM for profile browsing, validation and controlled writes through the
   system service;
4. profile editing introduced only through future authorized manager methods.

The base backup package must keep working without this integration installed.
