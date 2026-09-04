# Product Roadmap

This roadmap describes direction, not sprint commitments or release promises.
[TODO.md](TODO.md) records active sprint tasks when a sprint is defined.
Proposed architecture is developed in [`docs/design`](docs/design/), while
accepted constraints are recorded in [`docs/adr`](docs/adr/).

## Product Invariants

Every roadmap item must preserve these properties:

1. automatic backup works without KDE, a logged-in user or the system manager;
2. systemd and udev own unattended activation;
3. profile JSON is the canonical configuration source;
4. incomplete receives never appear as committed snapshots;
5. incremental parents are selected by UUID identity, not names alone;
6. restore and administrative workflows remain usable without a desktop;
7. formats and APIs are versioned before external clients depend on them.

## Near Term: Extend The System Control Boundary

- add scheduling and a persistent request queue without making the manager
  responsible for runner execution;
- broaden provisioning tests across the supported systemd range;
- test physical removable devices from multiple vendors;
- add emulated NVMe and SCSI/SATA controller coverage;
- cover more unsupported block stacks and power-loss boundaries.

## Backup Semantics

Separate local capture from target replication so local recovery points can be
created independently of removable-media availability. Add a persistent
request queue that merges triggers such as device connection, schedule, manual
request, pre-upgrade capture and retry.

Introduce consistency groups and a snapshot barrier before replication. Group
hooks must quiesce applications only for the bounded capture window, and
history must retain the shared capture identity. See
[consistency-groups.md](docs/design/consistency-groups.md) and
[scheduling.md](docs/design/scheduling.md).

Later improvements include:

- pre-upgrade snapshots and longer retention for tagged captures;
- power, sleep-inhibition, CPU and I/O policies;
- controlled concurrency across independent targets;
- calendar retention, pins, notes and reclaimable-space analysis;
- multi-source capacity forecasting and preflight space estimates that include
  retention and repository cleanup effects;
- optional transfer-rate limits and an event-driven multi-transfer runner;
- checkpoint-aware application hooks that distinguish pre-snapshot and
  post-snapshot failure.

## Configuration And Media

Improve configuration without weakening the privileged boundary:

- keep extending the media-preparation safety matrix after its 4.0 release
  validation without expanding the set of supported destructive layouts;
- extend successful media preparation with an explicit trial backup and trial
  restore;
- evaluate TPM2, FIDO2 and PKCS#11 enrollment only after recovery-key and LUKS
  header-backup guidance is in place;
- strengthen cancellation and recovery coverage for credential enrollment
  before adding further enrollment methods.

## Repository Durability

Define future repository-format upgrades with dry-run, transactional commit
and compatibility fixtures. See
[repository-format.md](docs/design/repository-format.md).

Repository operations should grow in this order:

1. produce a repair plan;
2. repair or full reseed without deleting the last verified copy;
3. optionally sign catalog metadata.

Support rotated targets and multiple hosts only after repository namespaces,
identity and upgrade rules are stable.

## Restore And Disaster Recovery

Disaster-recovery work includes:

- LUKS key-slot and header-backup audit;
- a secret-free host recovery manifest;
- an exportable rescue bundle;
- documented, scheduled and recorded restore drills;
- repository repair and reseed workflows;

## Transport And Resilience

Keep encrypted removable Btrfs media as the reference transport. Add a
transport boundary only after local repository and recovery semantics are
stable. The first remote candidate is a constrained SSH helper that cannot
execute arbitrary shell commands and can support append-only source
credentials. See [remote-transport.md](docs/design/remote-transport.md).

Future policy tools may evaluate 3-2-1 coverage, off-site freshness, restore
drill age and repository health across multiple targets.

## Operations And Diagnostics

Expand stable error codes and structured details across repository, restore,
retention, scheduling, power and transport failures. Add:

- `btrfs-backupctl doctor` and sanitized support bundles;
- target scrub freshness, Btrfs device statistics and optional SMART/NVMe data;
- health output suitable for Nagios/Icinga and OpenMetrics exporters;
- history statistics and report export;
- configuration history, diff and transactional rollback;
- stable CLI quick commands for status, manual request, eject, history and
  restore, all routed through the request queue and common control API.

## C++ Architecture And Quality

Evolve the domain, port, adapter, persistence, CLI and daemon boundaries without
a broad directory migration or behavioral rewrite:

- split a broad effect or persistence component only when a concrete ownership
  or lifecycle problem remains;
- replace remaining security-relevant strings with value objects and enums
  where they prevent invalid states;
- keep exceptions at infrastructure/application boundaries and extend typed
  outcomes where failure is expected;
- keep CMake interfaces minimal and model targets free of Linux, JSON and UI
  dependencies;
- keep ASan, UBSan, GCC, Clang, formatting and static-analysis gates, and
  extend compiler coverage to manager-enabled and manager-disabled
  builds.

Refactors must preserve public CLI, profile, status, history, recovery and
package contracts and pass the real-Btrfs regression suite when storage
semantics are touched.

## Interoperability And Desktop

Keep the optional desktop integration split between the status plasmoid,
presentation-only widget settings, the session monitor, a QML Kirigami KCM and
read-only KIO/Dolphin adapters. The monitor owns terminal notifications, the
KCM owns target validation and administration, and the manager remains an outer
adapter rather than the backup execution owner. See
[ADR 0005](docs/adr/0005-kde-integration-boundaries.md).

Continue desktop integration in this order:

- detect and later adopt suitable Snapper snapshots;
- dry-run import from btrbk configuration;
- snapshot diff for diagnostics and restore selection;
- evaluate `kio-snapshot` interoperability and a provider API;
- extend repository health and restore-drill reporting into the desktop.

No integration may become a required dependency of the base runtime.

## Verification Investment

Add QEMU coverage for ENOSPC and more interruption boundaries. Add fuzzing for
untrusted JSON and path inputs, and extend compiler and runtime checks when new
supported platforms are introduced.

## Open Source And Release Maturity

Bring contributor and release governance up to the level of the runtime:

- publish a tested platform support matrix and distinguish native packages from
  best-effort packaging templates;
- add CodeQL as an appropriate pull-request gate;
- validate pull-request titles against the documented Conventional Commits
  types and scopes, without adding a runtime dependency;
- automate tagged releases only after verifying `VERSION`, changelog, tests,
  reproducibility, checksums and artifact inventory;
- derive changelog, release-note and SemVer automation from Conventional
  Commits only after the title policy has remained stable in normal use;
- add signed checksums or artifact attestations/provenance;
- provide man pages and Bash, Zsh and Fish completions;
- document development versus stable versions clearly;
- adopt a code of conduct when actively inviting community participation and
  CODEOWNERS when multiple maintainers make ownership meaningful;
- evaluate a distinctive product name before wider promotion while preserving
  command compatibility if a rename is chosen.

## Explicitly Deferred

- implementing the Btrfs send-stream format in-process;
- enabling qgroups automatically;
- a custom scheduler loop when systemd timers are sufficient;
- automatic repository upgrades;
- mandatory Snapper, KDE, Qt Quick or desktop dependencies in the base package;
- arbitrary remote shell execution for SSH transport;
- restore that overwrites an active root or home subvolume by default.
