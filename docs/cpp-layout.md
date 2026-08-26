# C++ Source Layout

The native code is organized by domain and responsibility. Headers live beside
their implementations because this repository builds an application, not a
public C++ SDK.

```text
apps/                         # small executable entry points
src/
├── core/                     # identifiers, errors, cancellation and shared runtime primitives
├── backup/                   # planning, execution, snapshots, transfer, recovery
│   └── transfer/             # transfer model, events, results and async orchestration
├── config/                   # profile model, validation, storage, rendering
│   └── wizard/               # interactive profile construction
├── state/                    # status, checkpoints, fingerprints, history reads
├── platform/linux/           # explicitly Linux-specific system integration
├── cli/                      # argv parsing, presentation, exit-code mapping
└── daemon/                   # optional read-only system D-Bus adapter
tests/
├── unit/{backup,config,state,platform,cli,daemon}/
├── integration/
├── support/
└── systemd/
data/{examples,schemas,systemd,udev}/
integrations/kde/             # optional desktop integration
```

Directories describe what code does. Architectural boundaries are enforced by
CMake targets and their declared dependencies:

```mermaid
flowchart TB
    subgraph contracts[Dependency-light contracts]
        direction LR
        core[core]
        config_model[config-model]
        state_model[state-model]
        backup_model[backup-model]
        transfer[transfer]

        core --> config_model
        core --> state_model
        config_model --> backup_model
        core --> transfer
        config_model --> transfer
        state_model --> transfer
    end

    subgraph support[Supporting runtime components]
        direction LR
        platform[platform-linux]
        config[config]
        state[state]
    end

    backup[backup orchestration]
    cli[CLI adapter]
    daemon[read-only D-Bus adapter]

    config_model --> platform
    backup_model --> platform
    transfer --> platform
    config_model --> config
    platform --> config
    config_model --> state
    state_model --> state
    platform --> state
    backup_model --> backup
    transfer --> backup
    platform --> backup
    config --> backup
    state --> backup
    backup --> cli
    config --> cli
    state --> cli
    config_model --> daemon
    platform --> daemon

    cli --> executables[btrfs-backup<br/>btrfs-backupctl]
    daemon --> manager[btrfs-backupd]
```

The `*-model` targets contain dependency-light contracts needed to avoid
cycles between configuration, backup concepts, and Linux implementations. They
are implementation details of the domain layout, not separate source trees.
The `btrfsbackup-core` target contains the validated `ProfileId`, `RunId`, and
`SourceId` value types and the shared error hierarchy. Those contracts can be
used by configuration, backup, state, and platform adapters without pulling in
JSON or filesystem implementations.

## Ownership

- `core` owns identifiers, the shared error hierarchy, platform-neutral
  cancellation state, and other shared runtime primitives.
- `backup` owns the single-execution `BackupRun`, run planning and execution,
  incremental-parent selection,
  transfer model and orchestration, snapshot commit, retention and recovery,
  target operations, and backup use cases.
- `config` owns the canonical profile JSON model, validation, profile loading
  and storage, installation rendering and validation, and the profile wizard.
- `state` owns configuration fingerprints, JSON checkpoint persistence,
  file-backed current status and history, status history reads, and the public
  status contract. It implements the event and checkpoint interfaces declared
  by `backup`; the executor never writes those files directly.
  `btrfsbackup-state-model` exposes the typed `RunStatus`, `RunProgress`, and
  optional `RunError` contract without depending on JSON or filesystem code.
- `platform/linux` owns POSIX processes, Btrfs and block-device integration,
  mount inspection, trusted files, locks, durable filesystem operations,
  transfer processes and `splice` pumping, poll wakeups for cancellation, and
  other Linux-specific effects.
- `cli` owns command-line parsing, output formatting, interactive streams, and
  exit-code mapping. Reusable orchestration must remain outside this target.
- `daemon` owns the versioned read-only D-Bus adapter and sanitization boundary.
  It observes file-backed state and never owns runner execution.
- `apps` owns only process entry points. The CLI runner adapter performs
  dependency composition after resolving its configuration and diagnostic
  options.

## Backup Application Service

`BackupService` is an application service over explicit ports. Profile loading,
mount inspection and activation, planning, run construction, locking, runtime
state, cancellation monitoring, clocks, and run-id generation are constructor
dependencies. Its public
`BackupRequest` carries only operation intent (`profileId`, `force`, and
`validateOnly`); filesystem paths, mount-table overrides, timestamps, and test
fixtures are adapter configuration rather than domain input. The CLI adapter
also owns selection of the default profile id; the request model does not
silently choose one.

The runner CLI is the composition boundary for the backup use case because it
resolves the selected configuration root and the diagnostic `plan` overrides.
It constructs the Linux and file-backed adapters and then invokes the same
`BackupService` constructor used by tests. The service itself does not select
between production and test behavior and does not instantiate `Posix*`,
`LibBtrfsOperations`, or JSON persistence implementations.

Run plans store actions as a `std::variant` of operation-specific types. Each
alternative owns exactly the inputs needed by that operation, and executors use
`std::visit` instead of interpreting shared path slots. `BackupRunActionKind`
remains only the stable action label used by events, checkpoints, and CLI JSON.

`BackupRunExecutor` sees one `IBackupRunActionHandler` port. Its production
implementation is a small `BackupRunActionHandler` dispatcher composed from
snapshot, recovery, retention, hook, repository, and transfer handlers. Each
specialized handler accepts only the action types and dependencies belonging to
its own effect group; the runner CLI assembles them at the composition boundary.

## Rules

1. Put a component's `.hpp` and `.cpp` files together in the owning domain.
   Include internal headers through paths such as `<backup/backup_service.hpp>`
   or `<platform/linux/process.hpp>`.
2. Reserve a future top-level `include/` directory for an intentionally public,
   versioned SDK. Do not mirror internal headers there.
3. Each unit test belongs to the domain it exercises and links the narrowest
   target that provides the tested contract. Cross-domain file contracts belong
   under `tests/integration/`.
4. Keep Linux-specific effects under `src/platform/linux/`. Pure configuration,
   state, and planning code must not acquire UI or desktop dependencies.
5. Invoke external programs without a shell. Pass executable and arguments
   separately through the shared command/process abstractions.
6. Keep long-running transfer orchestration separate from short administrative
   commands. Cancellation state and async completion remain platform-neutral;
   Linux adapters translate cancellation into pollable runtime events.
7. Keep root-only state and history separate from sanitized public status.
8. Do not add empty directories for planned QEMU or KDE components.
   Add them only with the first implementation they own.
9. Optional KDE code remains under `integrations/kde` and outside the base
   runtime dependency graph.

Application-facing functions such as `plan_backup`, `start_backup`,
`cancel_backup`, `mount_target`, `eject_target`, `save_profile`, `get_statuses`,
and `render_installation` accept typed requests and results. They do not depend
on command-line arguments, output streams, presentation rules, or process exit
codes, so future D-Bus and KDE adapters can use the same behavior.

New backup and state contracts use `ProfileId`, `RunId`, and `SourceId` rather
than interchangeable strings. Run lifecycle, phase, and error codes are enums.
CLI and JSON adapters perform the explicit conversion to their stable textual
representations; older persistence and configuration structures can migrate
incrementally when their owning boundary changes.
