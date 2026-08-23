# Plasma Integration

Plasma support is optional. The backup runner, profile model and systemd units
must keep working without a graphical session and without any Plasma package
installed.

The current integration starts with a plasmoid in `integrations/plasma`. Its
QML UI talks to a C++ `BackupStatusModel`, and that model reads the public
`btrfs-backupctl status watch --json` stream. This keeps QML away from shell
commands and private state files while the versioned system D-Bus manager is not
ready yet.

The target architecture remains:

1. system manager with a versioned D-Bus API and polkit for mutating actions;
2. shared desktop client library translating D-Bus data to Qt/QML models;
3. monitor process that owns KJob, KUiServer progress and notifications;
4. plasmoid for status and lightweight controls;
5. KCM for profile inspection, validation and controlled configuration writes.

The plasmoid must not own long-running backup progress. After the desktop
monitor exists, progress and notifications belong there so they survive
plasmoid removal and shell restarts.
