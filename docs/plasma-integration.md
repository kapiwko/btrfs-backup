# Plasma Integration

Plasma support is optional. The backup runner, profile model and systemd units
must keep working without a graphical session and without any Plasma package
installed.

The current integration starts with a plasmoid in
`integrations/kde/plasmoid`. Its QML UI talks to a C++ `BackupStatusModel`, and
that model uses the read-only `io.github.btrfsbackup.Manager1` system D-Bus
interface. Calls are asynchronous and status is polled because the first
manager interface does not publish change signals.

The model validates `apiMajor` and public status schema capabilities before it
accepts data. `managerConnected` reports manager availability, not target
connectivity. A future `targetConnected` property must come from authoritative
`GetDeviceState` data supplied by the system backend.

This initial plasmoid exposes read-only status. The system manager already owns
privileged mutation and polkit authorization, including cancellation. A later
Plasma control surface will consume that authorized API.

The plasmoid displays reduced `RunStatus`, including configured source and
target labels, progress, speed, and ETA. It does not show an eject icon or a
safe-to-disconnect message because the current runtime has no separate,
authoritative `TargetStatus`. That indication will be added only after the
system API reports the result of the actual eject operation.

## Package

The Plasma integration is shipped separately as `btrfs-backup-kde`. Install it
next to the matching base package version:

```bash
sudo pacman -U btrfs-backup-0.3.0-1-x86_64.pkg.tar.zst \
               btrfs-backup-kde-0.3.0-1-x86_64.pkg.tar.zst
```

The base package does not depend on Plasma. The KDE package installs:

```text
/usr/share/plasma/plasmoids/org.btrfsbackup.plasmoid
/usr/lib/qt6/qml/org/btrfsbackup/plasma
```

The package install hook runs `kbuildsycoca6` when available. If a widget is
already present on the desktop or panel, the running shell may still have the
old QML module loaded. Restart the shell after upgrading the widget:

```bash
systemctl --user restart plasma-plasmashell.service
```

If that user unit is not available, remove and add the widget again after
restarting the shell through the desktop session tools.

The target architecture remains:

1. system manager with a versioned D-Bus API and polkit for mutating actions;
2. shared desktop client library extracted from the current Qt D-Bus model;
3. monitor process that owns KJob, KUiServer progress and notifications;
4. plasmoid for status and controls routed through the shared D-Bus client;
5. KCM for profile inspection, validation and controlled configuration writes.

A profile write that adds or changes a hook can schedule an arbitrary trusted
program to run as root during the next backup. The system API must expose that
as a distinct high-risk authorization path. Its polkit action must require an
explicit administrator decision and must not be implicitly granted to the
active desktop user.

The plasmoid must not own long-running backup progress. After the desktop
monitor exists, progress and notifications belong there so they survive
plasmoid removal and shell restarts.

The privileged core publishes reduced current status. Full history and service
diagnostics remain root-only.
KNotifications belongs to the future per-session KDE monitor, which owns the
user session and desktop delivery context.
