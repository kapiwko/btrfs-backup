# Plasma Integration

This directory contains optional Plasma 6 integration. It is part of the main
CMake graph only when `BUILD_KDE_INTEGRATION=ON`, so the system backup runtime
does not require Qt Quick, Kirigami, Plasma or a graphical session by default.

The integration includes a plasmoid, session monitor, System Settings KCM,
read-only KIO worker, Dolphin previous-version action, guided restore
application and KRunner plugin. They use the implemented system manager D-Bus
API:

```text
io.github.btrfsbackup.Manager1
/io/github/btrfsbackup/Manager1
```

QML only binds to C++ properties. Shared models under `models/` validate manager
capabilities, load public profiles, status, target state and paged sanitized
history, and invoke authorized operations asynchronously. The neutral
`org.btrfsbackup.kde` module also owns profile status icons, priorities and
action-availability rules used by both the KCM and plasmoid. Subsequent refreshes
are driven by D-Bus change signals rather than polling. It does not spawn
`btrfs-backupctl` or block the UI thread on D-Bus calls.

`managerConnected` reports whether a compatible manager has answered. It must
not be interpreted as target-device connectivity; target lifecycle is a
separate `GetDeviceState` contract.

The plasmoid offers start, run-scoped cancellation, browse and eject through
the manager's polkit-protected methods. The KCM owns target validation and
profile administration, with a separate high-risk authorization for hook
changes. Target removal state is read from the separate `GetDeviceState`
response, never inferred from backup success. Routine operational controls are
available to the active local session; administrative operations require
administrator authorization.

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
plasmoid also needs the compiled `org.btrfsbackup.kde` QML module.

After installing both the plasmoid and the QML module:

```bash
plasmawindowed org.btrfsbackup.plasmoid
```

## Desktop Progress

The expanded profile view owns the live transfer chart. The
`btrfs-backup-kde-monitor` process represents each active manager run as a
`KJob`, registers it with `KUiServerV2JobTracker`, and maps manager progress and
byte rate onto the job. A killable job forwards cancellation to the manager
with the matching profile and run ID.

This adapter must run in the graphical user session. The root system manager
cannot publish directly to a user's Plasma job tracker. `KNotification` remains
appropriate for terminal success or failure messages; it is not the primary
transport for continuously updated job progress.

## Architecture

The desktop integration is divided into six surfaces:

1. the plasmoid provides concise status and routine controls;
2. plasmoid settings contain presentation preferences only;
3. the session monitor owns `KJob`, `KUiServerV2JobTracker` and terminal
   notifications without owning run results;
4. a QML Kirigami KCM provides profile inspection, target validation,
   diagnostics and controlled writes through the system service;
5. KIO and Dolphin adapters browse backups through a shared, CLI-first restore
   engine and caller-bound read-only manager sessions;
6. the guided restore application and KRunner plugin expose restore and common
   backup operations without duplicating domain logic.

The shared Qt D-Bus client supports these adapters without becoming a single
desktop application object. KIO does not implement repository discovery or
restore policy; it maps `btrfsbackup:` URLs onto the shared engine inside an
authorized session. Profile editing uses only authorized manager methods; no
desktop component invokes `sudo` or writes `/etc` directly.

The base backup package must keep working without this integration installed.
