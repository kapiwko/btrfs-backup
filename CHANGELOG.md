# Changelog

## 1.0.0 - Unreleased

### Highlights

1. the base package now provides a CLI-first restore engine that discovers and
   validates repository format v1, browses snapshots, finds previous versions,
   plans restores and performs transactional file, directory, subvolume and
   restore-drill workflows;
2. the KDE package adds authorized read-only repository browsing through KIO,
   previous-version actions in Dolphin, a guided restore application and
   backup commands in KRunner;
3. the System Settings KCM can inspect, validate, save and delete profiles
   through the manager's authorized administration boundary, with hook changes
   protected by a separate high-risk authorization;
4. guided provisioning prepares or adopts encrypted removable targets through
   a durable, separately isolated system helper and an explicit destructive
   confirmation flow.

### Upgrade Notes

1. profile loading now accepts only schema version 4; automatic normalization
   of schema versions 1 through 3 and their retired mount and state-path fields
   has been removed;
2. installed profiles without `configurationGeneration` are rejected by the
   runtime;
3. `btrfs-backupctl profile migrate-activation` and the crypttab import support
   have been removed;
4. pending recovery markers without `final_snapshot_path` are invalid and are
   cleared without applying the former UUID-only recovery behavior;
5. the retired multi-file configuration fingerprint API has been removed.

Version 1.0 does not support configuration from earlier releases. It accepts
only schema-v4 profiles created for this release and rejects other schemas
without modifying them. Back up required data independently, create a new v4
profile, and verify both backup and restore before relying on it. The public
migration, export and upgrade-preflight commands have been removed.

### Backup Device Provisioning

1. returning from device creation and opening it again no longer produces an
   empty KCM page; the workflow now uses a non-scrollable KCM container around
   its own step-specific scroll view;
2. the System Settings workflow supports four explicit modes: formatting an
   existing partition, creating a partition in unallocated GPT space, erasing
   and repartitioning an entire selected disk, and adopting an existing
   compatible target;
3. existing-partition mode destroys signatures and data only on the selected
   partition, unallocated-space mode creates one partition within the selected
   free extent, and whole-device mode destroys the selected disk's partition
   table and all of its contents; adoption performs read-only inspection and
   does not format the target;
4. newly prepared targets use LUKS2 with Btrfs, while adoption accepts only a
   validated LUKS2+Btrfs repository layout supported by this release;
5. the profile identifier is reserved before the first destructive operation
   and final profile publication is create-only, preventing a preparation from
   overwriting another configuration;
6. the manager and helper revalidate device identity, geometry, signatures,
   mounts, swap, holders and the source filesystem at their respective trust
   boundaries before allowing the first write;
7. LVM physical-volume and Linux MD RAID member signatures are treated as
   unsupported block stacks and cannot be selected for destructive
   preparation;
8. destructive storage commands run only in a separately hardened transient
   systemd helper with a per-operation device allow list; the manager
   coordinates authorization and durable transaction state;
9. the helper starts with access only to the selected disk and its concrete
   existing child partitions so their safety state can be revalidated. After
   inspection it retains only the selected or newly created partition; mapper devices
   are added by exact `major:minor` only after they exist; mapper control is
   granted only for the bounded LUKS open/close interval. Each replacement
   clears the preceding allow list, so sibling disks, unrelated mappers and
   permissions from an earlier operation are not inherited;
10. revisioned root-only transactions support restart recovery after
   interruption and preserve the first cleanup failure. The store rejects
   unsafe directory ownership or modes, symlinks, non-regular records,
   insecure locks and reservations, and duplicate corrupted records without
   blocking on special files or replacing diagnostic evidence;
11. ambiguous identity, unsafe persisted state or incomplete cleanup stops with
   a stable error and explicit manual-recovery guidance rather than guessing;
12. version 1.0 rejects profiles from every earlier release without modifying
   them; operators must create a new schema-v4 configuration;
13. selecting a disk no longer implicitly chooses whole-device erasure. The KCM
   requires that scope to be selected explicitly and keeps rejected devices
   visible with their blocker;
14. source choices use user-facing names, automatic-key storage and recovery
   implications are explained, and failed operations show completed steps,
   cleanup outcome, a copyable diagnostic report and recovery guidance;
15. devices and regions rejected by storage-safety checks remain visible with a
   localized reason, and provisioning sizes use locale-aware C++ formatting.

### Target Credentials And Hotplug Recovery

1. the manager and KCM list LUKS keyslots and support authorized passphrase,
   managed-key generation, key enrollment and unambiguous credential removal;
2. managed key files must remain below the trusted root-only credential store,
   while incomplete credential mutations preserve rollback diagnostics;
3. authorization distinguishes read-only credential discovery from privileged
   storage mutation;
4. target activation recovers after disconnect and reconnect by rejecting stale
   mapper state, reopening the verified LUKS device and remounting only the
   identity configured for the profile;
5. eject handles an unmounted stale mapper and a mapper whose backing device
   disappeared without treating unrelated mappings as owned resources;
6. manual eject closes idle repository-browse sessions for the selected profile
   before unmounting, while an active browse or restore operation remains busy;
7. unlocking methods are rendered from a stable scalar model and show their
   complete description directly in the list, avoiding a crash-prone details
   popup during credential refresh.

### Restore And Repository Access

1. active local sessions browse without an authentication prompt while the
   manager enforces the snapshot's stored owner, group, mode and POSIX ACL;
2. repository discovery verifies format, catalog structure, snapshot identity,
   read-only state and Btrfs UUID relationships before exposing restore data;
3. restore planning rejects traversal, symlink escapes, special files, nested
   mount boundaries and unsafe destinations, while execution stages changes
   and either commits the complete result or rolls it back;
4. the manager opens caller-bound, time-limited, read-only browse sessions in a
   root-owned hierarchy and exposes a pinned root directory descriptor instead
   of a reusable host path;
5. concurrent browse operations hold independent session leases, so completing
   one KIO request cannot allow cleanup while another request is active;
6. browse-session lifecycle and authorization are covered by unit tests and a
   real system D-Bus integration test, including cleanup after client exit;
7. `btrfs-backupctl repository rebuild` inspects mounted snapshots, previews
   the resulting metadata by default and atomically rebuilds repository and
   catalog documents only with explicit `--apply`;
8. the manager exposes bounded, stable name-sorted directory pages, and KIO
   streams every page instead of rejecting directories above 10,000 entries;
   page selection uses a bounded heap, reducing work on very large directories
   while retaining only the requested page plus one lookahead entry;
9. previous-version discovery is batched into bounded manager pages and cached
   per browse session, removing the synchronous D-Bus N+1 path for current
   daemons while retaining a legacy fallback for older services;
10. KDE clients keep browse sessions alive with bounded, identified operation
   leases; duplicate, foreign and mismatched releases are rejected, and the
   earlier counted operation-pin API has been removed;
11. restore catalog decoding has a dedicated validated boundary, while restore
   failures expose a friendly message, stable code and optional technical
   details separately;
12. the restore application finishes with a dedicated outcome view showing the
   restored file count, byte size and destination, with an action to open the
   restored directory;
13. browse filesystem traversal and marker persistence now have dedicated,
   unit-tested components while target lease and mount orchestration remain in
   the system browse backend; marker schema v1 and cleanup ordering are
   unchanged;
14. restore planning receives a descriptor pinned directly to the selected
   authorized entry, avoiding inaccessible private repository layout parents;
15. the public browse API no longer returns a descriptor for the repository
   root; clients can open only entries authorized by the broker;
16. stored permissions are evaluated against the UID and group set of the
   actual D-Bus sender process captured when its browse session opens;
17. operation leases expire after five minutes and browse sessions have a
   one-hour absolute lifetime, so an abandoned client cannot block eject
   indefinitely;
18. backup coverage queries pass an `O_PATH` descriptor for a local file or
   directory, preventing callers from probing coverage with arbitrary path
   strings at the privileged boundary.

### KDE Desktop Integration

1. `btrfsbackup:` URLs expose session-scoped repository entries without
   publishing device identifiers or persistent host paths;
2. Dolphin resolves all backup coverage for a local path, asks the user when
   more than one profile/source pair applies and opens a populated previous
   versions list backed by the verified repository catalog;
3. the restore application selects a version and destination, previews the
   operation and reports completion through native desktop notifications;
4. KRunner provides status, start, browse and previous-version commands through
   the shared manager client;
5. the plasmoid and KCM expose repository browsing alongside their existing
   status, target and profile workflows;
6. automatic backup activation can be switched per profile from both the
   plasmoid and KCM without an administrator password, while configuration
   edits remain separately authorized;
7. plasmoid-wide refresh and profile management moved to Plasma's contextual
   header actions, removing the duplicate in-popup application header;
8. hidden profiles can continue to affect the panel status icon, and the
   tooltip summarizes several profiles requiring attention in priority order;
9. the plasmoid keeps one shared profile directory and event source, while
   persistent per-profile models fetch only their own status, device state and
   history instead of creating duplicate profile probes for every delegate;
10. desktop actions resolve KDE services and URLs through KIO launcher jobs,
   preserving desktop activation and reporting asynchronous launch failures;
11. previous-version breadcrumbs show the selected source path, and restore
   launches preserve `btrfsbackup:` row URLs instead of copying them locally;
12. the restore dialog keeps long destinations inside the window and presents
   the entry type, size, modification time and backup date before restoring;
13. device-inspection progress is centered horizontally and vertically in the
   available System Settings page, including with wrapped translations.

### Manager And Desktop Reliability

1. public run status identifies backup and target-validation operations, and
   KDE progress notifications distinguish completed, validated, skipped,
   cancelled and failed outcomes while keeping stable error codes visible;
2. profile administration uses generation and fingerprint preconditions so a
   stale editor cannot overwrite a newer installed profile;
3. browse-session mount paths remain root-owned, public replies contain no host
   root path, and restore resolves content from the manager-provided pinned
   descriptor;
4. architecture and integration gates cover the expanded KCM, KIO, Dolphin,
   restore and KRunner surfaces without adding KDE dependencies to the base
   runtime;
5. the real target lifecycle test starts and verifies `systemd-udevd` before
   managed activation, removing an environment-dependent device-publication
   race from release verification.

### Profile Configuration Lifecycle

1. `btrfs-backupctl profile regenerate --all` rebuilds transactional systemd
   and udev artifacts for every installed profile;
2. package upgrades do not rewrite administrator-owned profile artifacts or
   restart the manager. Regeneration and service reload remain explicit
   administrator actions;
3. migration and upgrade-preflight commands for older profile schemas have
   been removed from the public CLI.

### Build, Packaging And Test Infrastructure

1. CMake presets and CTest are the canonical build and test entry points;
   release, screenshot, QEMU and privileged integration orchestration use
   focused Python drivers instead of a monolithic shell harness;
2. release packages are assembled from a common CPack staging tree, with Arch,
   RPM, Nix and Gentoo definitions checked against the installed runtime;
3. real-Btrfs, system D-Bus, systemd, installed-runtime and provisioning tests
   use isolated C++/Python fixtures and disposable devices, including clean
   package installation and loader checks;
4. package lifecycle scripts are intentionally minimal: tmpfiles and native
   package triggers replace privileged post-install configuration mutation;
5. adoption regression tests cover changed LUKS identity, invalid credentials,
   non-LUKS2 containers, non-Btrfs and unmountable filesystems, empty, legacy,
   unsupported and incomplete repositories, and simultaneous cleanup failures.
6. component-based DEB and RPM generation preserves the public package name
   `btrfs-backup` instead of leaking CPack's internal `Unspecified` component
   name into package metadata;
7. `TODO.md` now records the explicit 1.0 go/no-go decision, locally verified
   gates, remaining remote-CI work and the accepted non-blocking P2
   hardware-matrix risk;
8. C++ changes across the release series follow the enforced formatting
   contract when checked together against their remote base commit;
9. a manually dispatched release-gate workflow builds every package format
    and runs the privileged real-Btrfs and QEMU suites for one candidate SHA;
10. container-based integration package builders keep their CMake trees in the
    writable artifact volume while the checked-out source remains read-only;
11. the compiler, sanitizer, static-analysis, KDE/D-Bus, systemd-security,
    packaging, real-Btrfs and QEMU release gates pass remotely for the 1.0
    candidate tree;
12. an interactive libvirt laboratory opens the current Arch packages in a
    disposable Plasma guest, with prepared LUKS2/Btrfs disks, versioned files,
    restore edge cases and controllable target hotplug for manual testing in
    virt-manager.

## 0.3.3 - 2026-08-30

### Target Storage Visibility

1. the runner records a private, atomically written and identity-bound target
   filesystem-space measurement before cleanup;
2. the system manager exposes verified live or matching cached capacity, used,
   available and minimum-space state through an optional versioned
   `GetDeviceState` block without mounting the target;
3. the Plasma widget shows capacity and usage in expanded profile details,
   distinguishes cached data, warns below the configured free-space minimum and
   remains compatible with older daemons;
4. shared typed codecs and grouped run, target and history presentation models
   keep schema validation out of the D-Bus coordinator and QML.

### State API Reliability

1. public status and private history documents use shared typed codecs with
   strict schema, field type and range validation;
2. CLI and daemon readers share bounded, regular-file-only reads protected with
   `O_NOFOLLOW`;
3. history lookup treats `last.json` as a rebuildable cache and recovers the
   newest authoritative run document when that cache is stale or missing;
4. status watching uses inotify while keeping JSON documents as the recoverable
   source of truth;
5. progress-only status writes are throttled without delaying phase, source or
   terminal-state updates.

### Plasma Operations

1. target validation remains available through the manager API but is no longer
   presented as a routine action in the status plasmoid.

## 0.3.2 - 2026-08-30

### Table-Free Target Management

1. profiles use schema version 4 with an explicit `askPassword` or `keyFile`
   target activation mode; key paths remain private and are validated as
   trusted root-only files before use;
2. profile saves install a native profile-specific `.mount` unit requiring the
   managed `btrfs-backup-target@.service`, so new installations do not read or
   modify `/etc/fstab` or `/etc/crypttab`;
3. target activation uses `systemd-cryptsetup` directly, validates the LUKS and
   mapper identities, records run-time ownership, and only deactivates a mapper
   created by that managed session;
4. target mount artifacts are published and obsolete mount paths are removed in
   the existing configuration transaction with rollback and systemd reload;
5. eject and mounted-session cleanup stop the managed activation unit and
   preserve compatible mappers that were already active before the session;
6. target activation waits for udev to publish the mapper device before
   validating and mounting it, avoiding races immediately after cryptsetup
   reports success.

### Upgrade Notes

1. `btrfs-backupctl profile migrate-activation --profile ID` previews an import
   from the matching legacy crypttab entry; adding `--apply` publishes the
   migrated profile and units without modifying crypttab;
2. the migrator accepts only the legacy options generated by btrfs-backup 0.3,
   rejecting external key scripts and other semantics that cannot be preserved;
3. existing fstab and crypttab entries may remain unused during the transition;
   the compatibility migrator and command are scheduled for removal in 1.0;
4. release packages now include the managed target activation template across
   all packaging backends and no longer ship table-fragment examples.

### Plasma Integration

1. the widget presents profiles as the primary objects, with profile-scoped
   actions, history, target state, validation feedback and expandable details;
2. actions are hidden when their target is disconnected, transient operation
   confirmations dismiss automatically, and successful validation is reported
   explicitly;
3. active transfers expose live transfer rates and are also published as native
   cancellable Plasma jobs by a graphical-session monitor without requiring
   KIO;
4. the widget and progress monitor share the Qt D-Bus manager client, and
   manager, filesystem, block-device and mount changes are delivered by
   signals instead of periodic polling;
5. the progress monitor installs a desktop identity understood by Plasma and
   continues reporting jobs independently of plasmoid or shell restarts.

### Release And Tooling

1. packaged installations resolve the managed target template from the system
   unit directory, and Arch upgrades restart an already running
   `btrfs-backupd` after replacing the binary;
2. release builds print stage and compiler progress, reuse one persistent CMake
   graph for native and KDE targets, and enable test targets only for explicit
   test modes;
3. changed-line clang-tidy checks provide a fast local path while the complete
   quality target remains available for full verification.

## 0.3.1 - 2026-08-29

### Backup Lifecycle And Reliability

1. cancellation is registered immediately after acquiring the run lease, so
   preflight, discovery, and plan construction can be interrupted consistently;
2. mounted target sessions now close explicitly and propagate mount and
   cryptsetup cleanup failures into the run result;
3. target-only validation has a distinct operation kind and terminal event, so
   it no longer appears as a successful backup in status history;
4. a completed data backup remains successful when later ledger or status
   persistence fails, while the runner reports degraded observability;
5. send and receive diagnostics retain bounded head and tail excerpts plus the
   discarded-byte count while continuing to drain child process stderr fully.

### Security And Maintenance

1. privileged manager operations write durable, root-only audit records with
   caller UID, action, profile, result, and stable error code;
2. the supported-version policy and post-release backlog now reflect the 0.3
   release line, and the architecture documentation matches the current action
   executor interface;
3. GitHub Actions dependencies are updated and pinned to full commit hashes,
   with Dependabot configured to track future action updates.

## 0.3.0 - 2026-08-29

### Upgrade Notes

1. profile schema version 3 removes `target.mountPoint` and `target.mountUnit`;
   the mount point is now always derived as `TARGET_MOUNT_ROOT/profileId`, with
   `/mnt/btrfs-backup` as the default root;
2. legacy profiles are accepted only when their mount point already matches the
   derived location; migrate other profiles explicitly, then re-render and
   install their systemd, udev, fstab, and crypttab artifacts;
3. privileged filesystem roots moved to the optional root-owned
   `/etc/btrfs-backup.conf`; profile JSON now contains only profile-specific
   target identity, repository paths, policy, hooks, and sources;
4. notification settings were removed from canonical profiles; core publishes
   status and journal diagnostics, while desktop notifications are owned by the
   KDE session integration;
5. public run status is schema version 3 and private diagnostic history is
   schema version 2; `safeToRemove` was removed and detailed `activity`, `phase`,
   progress accuracy, stage, and terminal-state fields replace it;
6. native executables are installed directly in the configured bindir. Release
   packages no longer ship the legacy shell wrappers or require Bash, `pv`,
   `libnotify`, or text-processing tools at runtime;
7. Linux 6.0 and `btrfs-progs` 6.0 are now the minimum supported versions;
   transfers use send protocol v2 and compressed-data support.

### Backup Engine And Safety

1. backup runs use independent non-blocking profile and target leases, preventing
   concurrent profiles, planning, backup, mount, validation, and eject operations
   from manipulating the same repository;
2. `runner plan` is offline by default and no longer mounts or opens a target as
   a hidden diagnostic side effect; `--mount-target` is explicit and restores
   the previous target state when planning mounted it;
3. preflight, discovery, planning, and execution now share one run-level failure
   boundary, so failures before the first action are persisted as terminal run
   events and appear in status and history;
4. interrupted receive recovery retains the exact intended final path, removes
   an unverified committed snapshot on the next run, and reports incomplete
   cleanup as `repository.recovery_required`;
5. cancellation requests are bound to both profile and run identity, rejecting
   stale or mismatched requests without affecting another run;
6. run cleanup has an explicit observable close sequence for cancellation
   monitoring, event/checkpoint persistence, active-run registration, cancellation
   state, and leases; destructor failures are logged instead of silently ignored;
7. synchronous commands and transfer children use a shared `posix_spawn()`
   adapter, avoiding unsafe allocation and setup between `fork()` and `exec()` in
   the multithreaded runner;
8. repository operations use trusted directory roots and descriptor-relative
   access to reject symlink escapes and path replacement races;
9. generated target mounts require `nodev`, `nosuid`, `noexec`, and `nosymfollow`,
   and runtime validation rejects a target mounted without those restrictions;
10. application hooks are restricted to root-owned, non-writable regular files
    below `/etc/btrfs-backup/hooks.d`, reject symlinks, execute through pinned
    descriptors, have mandatory finite timeouts, and terminate their process
    groups on cancellation;
11. systemd services isolate temporary files, protect system and kernel state,
    hide unrelated processes, prevent privilege gains and writable executable
    memory, and restrict address families;
12. configuration saves are transactional across private profile, public profile,
    udev rule, and systemd drop-in files, with generation markers preventing a
    partially published or incompletely rolled-back configuration from running;
13. plans have one executable source of truth, while a unified action executor
    applies consistent events and error handling to short actions and long-running
    transfers;
14. the native architecture now separates domain models, application ports,
    Linux adapters, state persistence, CLI presentation, and daemon services with
    narrow CMake interfaces and compile-checked public headers;
15. the Arch upgrade hook migrates diagnostic history directories and files to
    root-only `0700/0600` permissions while keeping reduced current status
    readable by local clients;
16. automatic eject is scheduled with systemd `OnSuccess`/`OnFailure` only
    after the sandboxed runner reaches its final state, preventing its private
    mount namespace from keeping the LUKS mapper busy.

### Progress And Status

1. transfers are pre-measured with `btrfs send --no-data`, including incremental
   parent selection, so byte totals and progress reflect the send stream rather
   than the apparent size of snapshot files;
2. sizing is reported as its own phase before data transfer; clients receive
   detailed stages for preparation, recovery, cleanup, snapshot creation,
   sizing, transfer, verification, commit, retention, and finalization;
3. aggregate progress includes the fractional progress of the active source and
   remains monotonic through post-transfer actions;
4. live speed uses a three-second EWMA, ETA derives from measured stream size,
   and status writes are interval-limited instead of being persisted for every
   splice operation;
5. failures retain typed public error codes while private paths and detailed
   diagnostics remain confined to root-only history and journald.

### System Manager And Plasma

1. the optional `btrfs-backupd` system service exposes the versioned
   `io.github.btrfsbackup.Manager1` API for capabilities, sanitized profiles,
   current status, bounded history, device state, start, cancel, validation, and
   eject;
2. D-Bus activation uses a default-deny broker policy, and every operational
   request is authorized through a separately named polkit action;
3. authorization is bound to an immutable operation id, profile generation, and
   profile fingerprint; the launched unit revalidates this context before any
   backup work and reports `ConfigurationChanged` on mismatch;
4. target validation runs in a dedicated hardened unit, acquires the target
   lease, tracks whether it mounted the target, and restores the prior state;
5. operational controls invoked by the active local Plasma session do not prompt
   for a password; inactive or remote callers still require administrator
   authentication, and profile/KCM administration remains privileged;
6. manual `StartBackup` bypasses the daily limit with `--force`; eject remains
   non-forced and refuses to proceed while the target lease is held;
7. the Plasma widget now uses the complete manager API for start, cancel,
   validate, eject, status, history, and device state instead of spawning a CLI
   watcher;
8. the widget displays detailed run stages, measured progress, speed, ETA,
   relative history times, icon progress, and native success/failure badges;
9. the popup uses Plasma's native header/action layout, compact scrollable
   history, corrected padding, and package metadata identifying the project
   author;
10. KDE package upgrades invalidate QML caches safely and print a session reload
    hint without attempting to refresh a root user's Plasma cache.

### Packaging, Build, And Tests

1. `VERSION` is the single version source for CMake, native and Plasma metadata,
   generated packages, source archives, and release reports;
2. CMake installations and generated systemd/D-Bus files honor custom install
   prefixes instead of embedding `/usr/bin`; distribution package generators
   continue to install their policy-controlled `/usr` paths explicitly;
3. base packages include the manager D-Bus policy, polkit action policy, service
   units, and required polkit runtime dependency while remaining independent of
   Qt, Kirigami, and Plasma;
4. the release pipeline builds locally by default and gives Docker/QEMU tests a
   finished package, reducing container dependencies and repeated compilation;
5. Docker contexts and runtime images were reduced, independent checks run in
   parallel, QEMU guest root filesystems are cached, and expensive release tests
   are opt-in rather than preceding every artifact build;
6. architecture tests now inspect the CMake File API target graph, compile every
   public header against its declared interface, and use `clang-scan-deps` to
   detect unnecessary public dependencies;
7. real-system coverage installs the generated package and exercises systemd,
   system D-Bus, real polkit with an unprivileged caller, full and incremental
   Btrfs transfers, recovery, retention, restore, sandboxing, and USB hotplug in
   a disposable QEMU guest;
8. the real-Btrfs harness verifies a plain mapper close/reopen lifecycle and
   stops its auxiliary Polkit service before automatic eject so the test
   service's private mount namespace cannot pin the target;
9. the QEMU suite enters the live preparation-helper cgroup to prove that the
   selected disk can be opened while an unrelated disk is denied by systemd's
   kernel-enforced device policy, and the manager capability test tracks API
   minor 8.

## 0.2.1 - 2026-08-23

1. transfer status now includes byte counters, speed, ETA, current-source
   progress, aggregate run progress and progress accuracy fields;
2. transfer execution now runs through asynchronous handles so the executor can
   observe progress and cancellation without sleep-based polling;
3. runner cancellation requests are now exposed through
   `btrfs-backupctl runner cancel --profile <id>` and handled by the active
   transfer pipeline;
4. transfer failures now use side-specific stable error codes for producer,
   consumer and combined send/receive failures;
5. aggregate progress no longer resets between sources, and the runtime can
   estimate transfer totals from the local snapshot when exact send-stream
   totals are not available;
6. cancellation wakes the transfer event loop immediately instead of waiting for
   the next process-status poll;
7. `btrfs-backupctl status watch --profile <id> --json` now validates emitted
   JSON against the documented status API before writing it to stdout;
8. added the initial Plasma 6 status plasmoid backed by a C++
   `BackupStatusModel` reading the public JSON status stream;
9. added QML runtime smoke coverage for loading and instantiating the compiled
   `org.btrfsbackup.plasma` module;
10. added an optional `btrfs-backup-kde` Arch package for the Plasma status
    widget and its compiled QML backend;
11. kept the base `btrfs-backup` package independent from Qt, Kirigami and
    Plasma runtime dependencies;
12. installed the compiled Plasma QML module under the Qt 6 import path
    `/usr/lib/qt6/qml`;
13. added KDE package install hooks that refresh the service cache with
    `kbuildsycoca6` when available.

## 0.2.0 - 2026-08-23

1. removed runtime fallback to legacy source and main configuration files;
2. stopped generating legacy source configuration files for profile render/save;
3. `btrfs-backupctl profile list` now lists canonical profile JSON storage;
4. started replacing Python runtime tooling with C++ by porting profile commands
   to native `btrfs-backupctl profile` code under the `cpp/` source tree;
5. package builds now compile and install the native control helper;
6. profile wizard now creates canonical profile JSON through the native helper
   instead of embedded Python snippets;
7. generated runtime packages no longer depend on Python and the old Python
   profile helper has been removed;
8. legacy profile migration entrypoints and helpers have been removed;
9. backup, mount, and eject runtime entrypoints now load profile JSON as their
   only profile configuration source;
10. profile render, save, and wizard flows no longer generate `profiles.d/*.env`
    files;
11. `btrfs-backupctl profile create` now builds canonical profile JSON directly,
    replacing the old environment-and-TSV based profile compose path;
12. `btrfs-backupctl installation validate` now owns rendered and active
    installation validation that used to live in the shell configurator;
13. `btrfs-backupctl installation render` now renders systemd, fstab, and
    crypttab installation files that used to be templated by the shell
    configurator;
14. `btrfs-backupctl` commands are grouped by area, including `profile list`,
    `status show`, `status history`, `target mount`, `target eject`, and
    internal `state ...` runtime commands;
15. the standalone `btrfs-backup-configure` entrypoint has been removed; the
    interactive setup flow is now `btrfs-backupctl profile wizard`.
16. the main backup runner is now native C++ and performs full and incremental
    Btrfs send/receive, pending-state recovery, target validation, status and
    history updates, and local/remote retention without the legacy Bash runtime;
17. `btrfs-backup` is now a thin launcher for the native C++ entrypoint, and
    its option handling no longer shells out to `btrfs-backupctl` or parses
    profile JSON with text tools;
18. standalone `btrfs-backup-mount`, `btrfs-backup-eject`, and deprecated
    `btrfs-backup-unplug` commands have been removed; use
    `btrfs-backupctl target mount` and `btrfs-backupctl target eject`;
19. generated systemd units now run `btrfs-backupctl target eject` directly in
    `ExecStopPost`;
20. target mount inspection uses `libmount` and Btrfs filesystem UUID identity
    from `libblkid`;
21. Btrfs snapshot metadata, readonly checks, snapshot creation/deletion, and
    subvolume receive verification use `libbtrfsutil` where applicable;
22. real Btrfs integration coverage now verifies package installation, target
    validation, full transfer, incremental transfer, mismatch rejection,
    source-on-target rejection, retention, and `.incoming` cleanup;
23. release packaging installs only the public `btrfs-backup` and
    `btrfs-backupctl` commands plus native private binaries under
    `/usr/lib/btrfs-backup`.

## 0.1.1 - 2026-08-23

1. profile-aware configuration loading from `/etc/btrfs-backup/profiles.d/<profile>.env`;
2. `--profile` support for backup, mount, and eject commands;
3. compatibility fallback to `/etc/btrfs-backup/backup.env` for the `default` profile;
4. `btrfs-backup-migrate-profile` for converting an existing legacy configuration and source definitions into canonical profile JSON and runtime profile files;
5. `btrfs-backupctl` for status, history, and watch access to the file-based status API;
6. `btrfs-backup-mount` for mounting and validating the configured backup target without starting a backup;
7. profile JSON examples and schema for future tooling;
8. package contents updated for all generated packaging backends;
9. configurator renders and installs profile files and a templated systemd unit;
10. udev starts the profile-specific systemd unit;
11. `btrfs-backup-profile list` lists profile files;
12. `btrfs-backup-migrate-profile --remove-legacy` moves the legacy configuration, source directory, and old udev rule aside after migration;
13. profile tooling validates canonical JSON profiles and materializes runtime `.env`, source, udev, and public manifest files;
14. `btrfs-backup-configure` now renders canonical `profile.json` first, then materializes the runtime profile files from that JSON;
15. profile `show` and `export` can read the active canonical profile or reconstruct it from runtime profile files;
16. `docs/engine-contract.md` defines the stable profile, status, history, phase, and recovery contract for future engine implementations;
17. installable packages include the profile systemd template unit used by udev;
18. package install and upgrade hooks reload systemd and udev rules without triggering devices;
19. status and history JSON are readable by unprivileged local users while private recovery state remains root-only;
20. `btrfs-backupctl status` falls back to the last history entry after the oneshot service exits;
21. `btrfs-backupctl history` returns `[]` when no history exists yet and renders cleaner JSON arrays.

## 0.1.0 - 2026-08-22

1. multi-source backup through `sources.d`;
2. read-only snapshots, `.incoming` receives, and verified same-filesystem commits;
3. incremental parents verified by UUID;
4. `Received UUID` checks after receive;
5. daily limit based on target UUID and configuration fingerprint;
6. pending-state recovery after interruptions;
7. separate local and remote retention;
8. explicit `btrfs-backup-eject` command;
9. udev startup through systemd without a device-removal handler;
10. CLI configurator with render, apply, and validation modes;
11. deterministic Arch package and test suite.
