# Plasma Integration

This directory contains optional Plasma 6 integration. It is part of the main
CMake graph only when `BUILD_KDE_INTEGRATION=ON`, so the system backup runtime
does not require Qt Quick, Kirigami, Plasma or a graphical session by default.

The integration includes a plasmoid with a small C++ QML backend and a session
monitor for native Plasma progress. Both use the implemented system manager
D-Bus API:

```text
io.github.btrfsbackup.Manager1
/io/github/btrfsbackup/Manager1
```

QML only binds to C++ properties. The C++ model validates manager capabilities,
loads public profiles, status, target state and sanitized history, and invokes
authorized operations asynchronously. Subsequent refreshes are driven by D-Bus
change signals rather than polling. It does not spawn `btrfs-backupctl` or block
the UI thread on D-Bus calls.

`managerConnected` reports whether a compatible manager has answered. It must
not be interpreted as target-device connectivity; target lifecycle is a
separate `GetDeviceState` contract.

The plasmoid offers start, run-scoped cancellation and eject through the
manager's polkit-protected methods. Target validation belongs to the planned
KCM. Target removal state is read from the separate `GetDeviceState` response,
never inferred from backup success. The installed policy grants the plasmoid's
operational controls without a password to the active local session; inactive
callers and future profile, hook, or device changes remain administrator-
authorized operations.

## Build

```bash
cmake -S . -B /tmp/btrfs-backup-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_KDE_INTEGRATION=ON
cmake --build /tmp/btrfs-backup-build --target btrfs-backup-kde
ctest --test-dir /tmp/btrfs-backup-build -L kde --output-on-failure
cmake --install /tmp/btrfs-backup-build \
    --prefix "$HOME/.local" \
    --component KDEIntegration
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

## Desktop Progress

The expanded profile view owns the live transfer chart. The
`btrfs-backup-kde-monitor` process represents each active manager run as a
`KJob`, registers it with `KUiServerV2JobTracker`, and maps manager progress and
byte rate onto the job. A killable job forwards cancellation to the manager
with the matching profile and run ID. KIO is not required.

This adapter must run in the graphical user session. The root system manager
cannot publish directly to a user's Plasma job tracker. `KNotification` remains
appropriate for terminal success or failure messages; it is not the primary
transport for continuously updated job progress.

## Architecture

The desktop integration is divided into five surfaces:

1. the plasmoid provides concise status and routine controls;
2. plasmoid settings contain presentation preferences only;
3. the session monitor owns `KJob`, `KUiServerV2JobTracker` and terminal
   notifications without owning run results;
4. a QML Kirigami KCM provides profile inspection, target validation,
   diagnostics and later controlled writes through the system service;
5. future read-only KIO and Dolphin adapters browse backups through a shared,
   CLI-first restore engine.

The shared Qt D-Bus client supports these adapters without becoming a single
desktop application object. KIO must not implement repository discovery or
restore policy, and the `kio-snapshot` provider and authorization model must be
evaluated before a public backup URL is selected. Profile editing is introduced
only through authorized manager methods; no desktop component invokes `sudo` or
writes `/etc` directly.

The base backup package must keep working without this integration installed.
