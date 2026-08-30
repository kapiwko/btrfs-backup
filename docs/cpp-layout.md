# C++ Source Layout

Local file organization, lifecycle, and ownership rules are documented in
[`cpp-readability.md`](cpp-readability.md).

The native code is organized by domain and responsibility. Headers live beside
their implementations because this repository builds an application, not a
public C++ SDK.

```text
apps/                         # small executable entry points
src/
├── core/                     # identifiers, errors, cancellation and shared runtime primitives
├── backup/                   # planning, execution, snapshots, transfer, recovery
│   ├── model/                # run plans, typed actions, events, snapshots and retention
│   ├── ports/                # platform-neutral application contracts
│   ├── action_handlers/      # action dispatcher and focused effect handlers
│   └── transfer/             # transfer model, events, results and async orchestration
├── config/                   # profile domain, JSON adapter, validation and rendering
│   ├── model/                # typed profile data and validation; JSON files build as config-json
│   └── wizard/               # interactive profile construction
├── state/                    # status, checkpoints, fingerprints, history reads
├── platform/linux/           # explicitly Linux-specific system integration
├── cli/                      # argv parsing, presentation, exit-code mapping
└── daemon/                   # optional authorized system D-Bus adapter
tests/
├── unit/{backup,config,state,platform,cli,daemon}/
├── integration/
├── support/
└── systemd/
data/{examples,schemas,systemd,udev}/
integrations/kde/             # optional desktop integration
```

## Namespaces

The source directory and C++ namespace describe the same owner. Shared core
types remain directly in `btrfsbackup`; domain code uses the following map:

| Source directory | Namespace |
|---|---|
| `src/core/` | `btrfsbackup` |
| `src/config/` | `btrfsbackup::config` |
| `src/state/` | `btrfsbackup::state` |
| `src/backup/` | `btrfsbackup::backup` |
| `src/backup/transfer/` | `btrfsbackup::backup::transfer` |
| `src/platform/linux/` | `btrfsbackup::platform::linux` |
| `src/cli/` | `btrfsbackup::cli` |
| `src/daemon/` | `btrfsbackup::daemon` |

Technical subdirectories such as `model`, `ports`, `action_handlers`, and
`wizard` do not create additional namespaces. Global entry points delegate
immediately to their qualified adapter function. The `namespace-layout`
architecture test enforces this mapping and rejects namespace-wide using
directives.

Directories describe what code does. Architectural boundaries are enforced by
CMake targets and their declared dependencies:

```mermaid
flowchart TB
    subgraph contracts[Dependency-light contracts]
        core[core]
        config_domain[config-domain]
        config_json[config-json]
        config_wizard[config-wizard]
        config_ports[config-ports]
        state_model[state-model]
        state_persistence[state-persistence-ports]
        backup_model[backup-model]
        transfer[transfer]
        backup_ports[backup-ports]

        core --> config_domain
        config_domain --> config_json
        config_domain --> config_wizard
        config_json --> config_wizard
        config_domain --> config_ports
        core --> config_ports
        core --> state_model
        config_domain --> backup_model
        core --> backup_model
        core --> transfer
        backup_model --> backup_ports
        config_domain --> backup_ports
        core --> backup_ports
        transfer --> backup_ports
    end

    subgraph support[Supporting runtime components]
        platform[platform-linux]
        config[config]
        linux_config[platform-linux-config]
        state[state]
        backup[backup orchestration]
        daemon_core[daemon-core]
    end

    cli[CLI adapter]
    manager[authorized D-Bus executable]

    backup_ports --> platform
    backup_model --> platform
    config_domain --> platform
    config_ports --> platform
    core --> platform
    state_persistence --> platform
    transfer --> platform
    config_domain --> config
    config_json --> config
    core --> config
    config --> linux_config
    config_domain --> linux_config
    config_json --> linux_config
    config_wizard --> linux_config
    config_ports --> linux_config
    core --> linux_config
    platform --> linux_config
    config_domain --> state
    config_json --> state
    backup_model --> state
    backup_ports --> state
    core --> state
    state_model --> state
    state_persistence --> state
    backup_model --> backup
    backup_ports --> backup
    transfer --> backup
    config_domain --> backup
    config_ports --> backup
    core --> backup
    backup --> cli
    backup_model --> cli
    backup_ports --> cli
    config_domain --> cli
    config_ports --> cli
    core --> cli
    config_json --> cli
    platform --> cli
    linux_config --> cli
    state --> cli
    transfer --> cli
    core --> daemon_core
    config_domain --> daemon_core
    config_json --> daemon_core
    platform --> daemon_core
    backup_ports --> manager
    config_ports --> manager
    daemon_core --> manager
    platform --> manager
    linux_config --> manager
    state --> manager

    cli --> executables[btrfs-backup<br/>btrfs-backupctl]
```

Arrows point from a dependency provider to its direct consumer. Third-party
libraries are omitted. In particular, `btrfsbackup-backup` is platform-neutral:
Linux and file-backed adapters meet it only in the CLI composition root.

The `*-model` targets contain dependency-light contracts needed to avoid
cycles between configuration, backup concepts, and Linux implementations. They
are implementation details of the domain layout, not separate source trees.
Configure-time architecture checks reject unexpected target dependencies and
JSON, Linux, D-Bus, or UI includes in the model and transfer targets. The same
checks require every source header to have exactly one owning CMake target.
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
- `config-domain` owns typed profile data and validation without a JSON
  dependency. The separate `config-json` adapter owns canonical profile parsing,
  serialization and document conversion. The remaining configuration targets
  own profile repositories, fingerprinting, installation rendering and
  validation, and the profile wizard.
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
- `daemon` owns the versioned D-Bus adapter, authorization, and sanitization boundary.
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

`BackupRunExecutor` sees one `IBackupActionExecutor` port. Its production
implementation delegates non-transfer effects to a small
`BackupRunActionHandler` dispatcher composed from snapshot, recovery, retention,
hook, and repository handlers. Each specialized
handler accepts only the action types and dependencies belonging to its own
effect group. `DefaultBackupRunActionHandlerFactory` owns this reusable,
run-scoped assembly in `backup`; the runner composition root supplies the Linux
and file-backed port implementations. Transfer execution remains a separate
coordinator owned by `backup`.

## Rules

1. Put a component's `.hpp` and `.cpp` files together in the owning domain.
   C++ filenames use `PascalCase` and match their primary type or precise
   module concept; `main.cpp` remains the conventional entry-point exception.
   Include internal headers through paths such as `<backup/BackupService.hpp>`
   or `<platform/linux/Process.hpp>`.
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
