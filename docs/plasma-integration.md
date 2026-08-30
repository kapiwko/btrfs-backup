# Plasma Integration

Plasma support is optional. The backup runner, profile model and systemd units
must keep working without a graphical session and without any Plasma package
installed.

The current integration starts with a plasmoid in
`integrations/kde/plasmoid`. Its QML UI talks to a C++ `BackupStatusModel`, and
that model uses the full implemented `io.github.btrfsbackup.Manager1` system
D-Bus interface. Calls are asynchronous; after the initial state load, the
clients refresh only in response to manager change signals. Mutating calls
remain behind the manager's per-operation polkit authorization.

The model validates `apiMajor` and public status schema capabilities before it
accepts data. `managerConnected` reports manager availability, while target
connectivity, mount state, and safe-removal state come only from authoritative
`GetDeviceState` data supplied by the system backend.

The plasmoid consumes profiles, reduced current status, sanitized history and
target lifecycle state. It exposes start, run-scoped cancellation, validation,
and eject through the authorized manager methods. Detailed run phases drive
the visible activity text; compact mode shows determinate or indeterminate
progress and a terminal-state badge.

## Package

The Plasma integration is shipped separately as `btrfs-backup-kde`. Install it
next to the matching base package version:

```bash
sudo pacman -U btrfs-backup-0.3.2-1-x86_64.pkg.tar.zst \
               btrfs-backup-kde-0.3.2-1-x86_64.pkg.tar.zst
```

The base package does not depend on Plasma. The KDE package installs:

```text
/usr/bin/btrfs-backup-kde-monitor
/usr/lib/systemd/user/btrfs-backup-kde-monitor.service
/usr/share/plasma/plasmoids/org.btrfsbackup.plasmoid
/usr/lib/qt6/qml/org/btrfsbackup/plasma
```

The monitor starts with the graphical user session. It publishes active runs as
native Plasma jobs through `KUiServerV2JobTracker`, including progress, transfer
rate and cancellation. Filesystem, block-device and mount changes are delivered
through event-driven manager signals, so the monitor does not poll while active
or idle.

The package hook does not run KDE cache tools as root. It prints a reload hint,
and package timestamps change when the widget sources change so stale QML cache
entries are not reused. If a widget is already present on the desktop or panel,
the running shell may still have the old QML module loaded. Restart the shell
after upgrading the widget:

```bash
systemctl --user restart plasma-plasmashell.service
systemctl --user restart btrfs-backup-kde-monitor.service
```

If that user unit is not available, remove and add the widget again after
restarting the shell through the desktop session tools.

The current architecture is:

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

The plasmoid does not own long-running backup progress. Progress belongs to the
desktop monitor so it survives plasmoid removal and shell restarts.

The privileged core publishes reduced current status and sanitized history.
Full diagnostic history and service diagnostics remain root-only.
Terminal notifications can be added to the same per-session monitor without
moving any desktop dependency into the privileged system service.
