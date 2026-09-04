# Plasma Integration

Plasma support is optional. The backup runner, profile model and systemd units
must keep working without a graphical session and without any Plasma package
installed.

The integration includes a plasmoid, session monitor, System Settings KCM,
read-only KIO worker, Dolphin action, guided restore application and KRunner
plugin. They share the `io.github.btrfsbackup.Manager1` system D-Bus client and
desktop-neutral restore engine. Calls are asynchronous; after the initial state
load, clients refresh only in response to manager change signals. Mutating
calls remain behind the manager's per-operation polkit authorization.

The model validates `apiMajor` and public status schema capabilities before it
accepts data. `managerConnected` reports manager availability, while target
connectivity, mount state, and safe-removal state come only from authoritative
`GetDeviceState` data supplied by the system backend.

The plasmoid consumes profiles, reduced current status, sanitized history and
target lifecycle state. Its expanded profile view also shows filesystem
capacity, used and available space, and a usage percentage when the manager
advertises `target-storage-usage`. Cached measurements are labelled with their
measurement time, and space below `minimum_target_free_bytes` is presented as a
theme-aware warning. Compact mode remains limited to status and progress.

Opening the widget has no target-side effects. A live measurement is used only
when the target is already mounted and verified; otherwise the widget shows the
last identity-matching measurement saved by the runner or a no-data state. It
does not unlock LUKS, mount a filesystem or start validation to obtain capacity.
The values represent Btrfs filesystem space reported by the kernel, not raw disk
size or exact Btrfs chunk, compression, metadata or qgroup consumption.

The presentation backend is split into grouped run, target and history models.
The shared state-document codec validates target JSON before the Qt target model
applies it, keeping schema handling out of the D-Bus coordinator and QML.

## Interface Responsibilities

The KDE interface has five distinct presentation layers:

| Layer | Responsibility |
| --- | --- |
| Plasmoid | Current status, progress and frequent operations such as start, cancel and eject |
| Plasmoid settings | Per-user or per-widget presentation preferences stored through `plasmoid.configuration` |
| System KCM | Profile, target, retention and automation configuration, plus administrative validation and diagnostics |
| Session monitor | Native job progress and terminal notifications independent of plasmoid lifetime |
| Restore adapters | KIO browsing, Dolphin previous versions, guided restore and KRunner commands over shared manager and restore contracts |

The KIO worker derives its visible snapshot namespace from the verified
repository catalog, not from directory names at the repository root. The
virtual `.versions/<source>/<relative-path>` namespace lists only verified
snapshots that contain the requested entry and links each result to its normal
`btrfsbackup:` URL. When a local path is covered by multiple profile/source
pairs, Dolphin requires an explicit selection instead of depending on response
order.

The boundary rule is that the plasmoid answers what is happening now and what
routine action is available, while the KCM answers how backup is configured and
whether that configuration is valid. Plasmoid settings must not become an
alternate profile editor or write privileged system configuration.

The current plasmoid scope is intentional. It lists profiles and presents run
state, phase, current source, transfer rate, estimated time remaining, progress,
target state, filesystem-space usage, the last successful backup, the outcome
of the last completed attempt and the three most recent runs. Its routine
controls are limited to starting a backup, cancelling the specific active run,
safely ejecting the target and switching automatic activation for an already
configured profile. Backup age is informational until a configured schedule
defines when a backup is overdue.

The system KCM is a QML Kirigami module usable from System Settings and through
`kcmshell6 kcm_btrfsbackup`. It validates drafts and performs profile save or
delete only through authorized manager APIs with generation and fingerprint
preconditions. Hook changes require their own high-risk authorization. The
plasmoid and its settings remain useful without opening the KCM.

Device preparation first obtains a caller-bound storage topology and then asks
the manager to build a short-lived plan for the selected opaque candidate. The
topology is a sanitized presentation model: the KCM identifies devices and
partitions by display ordinal and never receives device nodes, hardware
identity, mount paths, labels or filesystem UUIDs. The
KCM renders proportional before/after layouts and a parallel textual list, so
color is not the only indication of destructive scope. It can preview an
existing-partition plan that preserves the table and every sibling region.
Selecting a device does not select a destructive mode. Using the whole device
is a separate explicit choice alongside an existing partition or unallocated
region, and excluded devices remain visible with a reason why they cannot be
selected.
Existing LUKS2/Btrfs targets are inspected read-only; compatible repositories
can be adopted, while empty, legacy, unsupported and foreign layouts are
presented as distinct non-adoptable results. Whole-disk, existing-partition and
unallocated-space preparation and adoption plans are executable. After a
failed preparation, the KCM presents the completed checkpoint sequence, failed
phase, cleanup result, stable error code and operation identifier. It also
offers a copyable diagnostic summary and a link to the recovery guide; private
device identity remains in the root-only transaction and journal.

For a development build, install the generated plugin and desktop entry, then
refresh KDE's service cache:

```bash
cmake --build build/kde-tests --target kcm_btrfsbackup btrfsbackup_kde_modelsplugin
sudo cmake --install build/kde-tests
kbuildsycoca6
kcmshell6 kcm_btrfsbackup
```

The installed files are
`/usr/lib/qt6/plugins/plasma/kcms/systemsettings/kcm_btrfsbackup.so` and
`/usr/share/applications/kcm_btrfsbackup.desktop`. The plugin identifier is
derived by `kcmutils_add_qml_kcm`; `KPlugin.Id` must not be duplicated in the
JSON metadata.

The plasmoid exposes start, run-scoped cancellation, eject and automatic
activation through the authorized manager methods. The automatic switch is
also available in the KCM and uses the dedicated passwordless operational
authorization; it does not edit any other profile field. Validation and
configuration belong to the KCM, not to the status plasmoid. Refresh and KCM
navigation use Plasma's contextual header actions instead of a second header
inside the popup. Detailed run phases drive the visible activity text; compact
mode shows determinate or indeterminate progress and a terminal-state badge.
Device changes, successful operations and active-to-terminal run transitions
trigger coalesced device-state refreshes without polling on progress updates.

## Visual And Interaction Principles

The plasmoid must look and behave like an integrated part of Plasma rather than
a standalone application embedded in a popup. The Bluetooth, Networks and
Audio Volume plasmoids are the primary interaction references. New UI work
must preserve the following rules:

1. use Plasma, Kirigami and Qt Quick Controls components, theme colors, metrics
   and icons instead of introducing a private visual language;
2. present profiles as compact `PlasmaExtras.ExpandableListItem` rows with an
   icon, name and short state summary, and place secondary information in the
   expanded view;
3. expose one clear default action for the current state, such as start or
   cancel, and keep less frequent operations in contextual actions;
4. reserve the plasmoid for status and routine operations, its settings for
   presentation preferences, and the KCM for profile editing, validation and
   other administrative workflows;
5. use semantic warning and error colors only for states that require user
   attention, and otherwise inherit the active Plasma theme;
6. avoid custom cards, decorative backgrounds, fixed color palettes and
   application-style headers inside the popup;
7. keep layouts stable and readable in compact panel mode, the normal popup and
   narrow or expanded popup widths, without overlapping or clipped labels;
8. support keyboard navigation, visible focus, accessible names and the
   disabled, busy, empty, disconnected and error states expected from native
   Plasma controls;
9. keep motion subtle and functional, limited to state transitions, progress
   changes and native expansion behavior;
10. verify user-facing changes in both light and dark Plasma themes and compare
    their information density and action placement with the reference
    plasmoids.

## Package

The Plasma integration is shipped separately as `btrfs-backup-kde`. Install it
next to the matching base package version:

```bash
sudo pacman -U btrfs-backup-1.0.0-1-x86_64.pkg.tar.zst \
               btrfs-backup-kde-1.0.0-1-x86_64.pkg.tar.zst
```

The base package does not depend on Plasma. The KDE package installs:

```text
/usr/bin/btrfs-backup-kde-monitor
/usr/bin/btrfs-backup-kde-restore
/usr/lib/systemd/user/btrfs-backup-kde-monitor.service
/usr/lib/qt6/plugins/kf6/kio/kio_btrfsbackup.so
/usr/lib/qt6/plugins/kf6/kfileitemaction/previousversionsaction.so
/usr/lib/qt6/plugins/kf6/krunner/btrfsbackup-runner.so
/usr/share/applications/io.github.btrfsbackup.ProgressMonitor.desktop
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

The architecture is:

1. system manager with a versioned D-Bus API and polkit for mutating actions;
2. shared desktop client library extracted from the current Qt D-Bus model;
3. monitor process that owns KJob, KUiServer progress and notifications;
4. plasmoid for status and controls routed through the shared D-Bus client;
5. KCM for profile inspection, validation and controlled configuration writes;
6. KIO, Dolphin, restore and KRunner adapters over caller-bound browse sessions
   and the shared restore engine.

A profile write that adds or changes a hook can schedule an arbitrary trusted
program to run as root during the next backup. The system API must expose that
as a distinct high-risk authorization path. Its polkit action must require an
explicit administrator decision and must not be implicitly granted to the
active desktop user.

The plasmoid does not own long-running backup progress. Progress belongs to the
desktop monitor so it survives plasmoid removal and shell restarts.

The privileged core publishes reduced current status and sanitized history.
Full diagnostic history and service diagnostics remain root-only.
Terminal notifications are delivered by the per-session monitor without
moving any desktop dependency into the privileged system service.
