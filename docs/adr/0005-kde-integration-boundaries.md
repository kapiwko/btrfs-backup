# ADR 0005: Separate KDE Integration Surfaces

- Status: accepted
- Date: 2026-08-30

## Context

The optional KDE integration serves status, session progress, configuration,
repository browsing and file-oriented recovery. Combining those concerns in a
single plasmoid or privileged desktop process would blur authorization,
lifecycle and dependency boundaries. The runner must continue without KDE, a
desktop session or the system manager.

## Decision

The KDE integration has five product surfaces with distinct ownership:

1. the plasmoid presents concise status and routine start, cancel and eject
   operations;
2. plasmoid settings contain only per-user and per-instance presentation
   preferences;
3. the session monitor owns Plasma `KJob` integration and terminal
   `KNotification` events, but never owns or determines the result of a run;
4. a QML Kirigami KCM owns profile configuration, target validation and
   administrative diagnostics through authorized manager use cases;
5. read-only KIO and Dolphin integration browse verified backups and select
   data for recovery.

KIO remains read-only. Restore behavior belongs to a desktop-neutral,
CLI-first restore engine; QML, KIO and Dolphin remain adapters over that engine.
The manager remains an optional control and authorization adapter rather than
the execution owner. Losing the manager or desktop session cannot turn an
active backup into a failed run.

Implementation proceeds in this order: monitor semantics, notifications,
plasmoid presentation, read-only KCM, authorized profile administration,
desktop-neutral restore engine, browse sessions, KIO, Dolphin actions and
guided restore UI.

Before selecting or implementing a snapshot URL and Dolphin integration, the
project must evaluate KDE's `kio-snapshot`, including its provider model,
authorization boundary and suitability for removable encrypted repositories.

## Alternatives

- Expand the plasmoid into a complete administration and restore application.
- Let each KDE component discover repositories and parse backup layout itself.
- Implement KIO before the restore engine and stable repository catalog.
- Run privileged CLI commands through `sudo` from desktop components.

## Consequences

- Each surface can be packaged and tested against its actual dependencies.
- Notification ownership is unique and independent of plasmoid installation.
- Administrative and backup-content access can use stronger authorization than
  routine operations.
- KIO and Dolphin work is deferred until repository and restore contracts are
  stable.
- The base runtime retains no KDE dependency and restore remains usable from
  the CLI.
