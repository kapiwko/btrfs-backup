# Product Roadmap

This roadmap describes direction, not sprint commitments or release promises.
The active sprint is maintained in [TODO.md](TODO.md). Proposed architecture is
developed in [`docs/design`](docs/design/), while accepted constraints are
recorded in [`docs/adr`](docs/adr/).

## Product Invariants

Every roadmap item must preserve these properties:

1. automatic backup works without KDE, a logged-in user or the system manager;
2. systemd and udev own unattended activation;
3. profile JSON is the canonical configuration source;
4. incomplete receives never appear as committed snapshots;
5. incremental parents are selected by UUID identity, not names alone;
6. restore and administrative workflows remain usable without a desktop;
7. formats and APIs are versioned before external clients depend on them.

## Near Term: Complete The System Control Boundary

The optional system manager described in
[system-manager.md](docs/design/system-manager.md) now provides versioned,
sanitized read APIs and polkit-protected start, cancel, validate and eject
operations. Runner execution remains independent and owned by systemd.

The next increments are:

- state-change signals and recovery of presentation state after manager restart;
- administrative profile save/delete with separate hook-change authorization;
- destructive device preparation only after repeated device-identity checks;
- explicit `TargetStatus` and safe-removal state;
- a shared C++ client, KDE session monitor, KJob integration and KCM;
- privileged-operation audit records without secrets.

The manager remains an outer adapter. The systemd runner continues to execute
an already-started backup if the manager or desktop disappears.

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
- better Btrfs-aware transfer estimates and exact progress when available;
- optional transfer-rate limits and an event-driven multi-transfer runner;
- checkpoint-aware application hooks that distinguish pre-snapshot and
  post-snapshot failure.

## Configuration And Media

Improve configuration without weakening the privileged boundary:

- analyze mounted Btrfs layouts and classify subvolumes as covered, explicitly
  excluded or probably missed;
- generate proposed source entries without silently modifying a profile;
- provide a destructive media-preparation workflow with system-disk rejection,
  repeated device-identity checks and typed confirmation;
- finish media preparation with profile creation, trial backup and trial
  restore;
- model LUKS unlock policy explicitly while keeping keyfile and passphrase as
  baseline modes;
- evaluate TPM2, FIDO2 and PKCS#11 enrollment only after recovery-key and LUKS
  header-backup guidance is in place;
- consider `libcryptsetup` only after manager authorization, cancellation and
  recovery behavior are stable.

## Repository Durability

Make backup media self-describing with a versioned repository identity and a
rebuildable snapshot catalog. Define explicit format upgrades with dry-run,
transactional commit and compatibility fixtures before writing the first
repository format. See [repository-format.md](docs/design/repository-format.md).

Repository operations should grow in this order:

1. discover and inspect;
2. verify Btrfs state, UUID chains, incoming data and catalog consistency;
3. rebuild derived metadata;
4. produce a repair plan;
5. repair or full reseed without deleting the last verified copy;
6. optionally sign catalog metadata.

Support rotated targets and multiple hosts only after repository namespaces,
identity and upgrade rules are stable.

## Restore And Disaster Recovery

Promote restore from documentation to a CLI-first engine. Provide repository
discovery, snapshot listing, read-only browsing, file/directory restore,
subvolume restore and periodic drills. Dangerous destinations remain denied by
default. See [restore-engine.md](docs/design/restore-engine.md).

Disaster-recovery work also includes:

- LUKS key-slot and header-backup audit;
- a secret-free host recovery manifest;
- an exportable rescue bundle;
- documented and recorded restore drills;
- repository repair and reseed workflows;
- read-only previous-version browsing for future KDE clients.

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
- privileged audit events tied to D-Bus caller identity;
- stable CLI quick commands for status, manual request, eject, history and
  restore, all routed through the request queue and common control API.

## C++ Architecture And Quality

Continue focused refactoring without another directory migration or behavioral
rewrite:

- split broad effect and persistence components by owned responsibility;
- replace weakly typed action payloads and configuration values with value
  objects, enums, `std::variant`, `std::chrono` and `[[nodiscard]]` where they
  improve correctness;
- keep exceptions at infrastructure/application boundaries and return typed
  domain outcomes where failure is expected;
- enforce minimal CMake link interfaces and keep model targets free of Linux,
  JSON and UI dependencies;
- add ASan, UBSan and compiler-matrix gates after establishing clean baselines.

Refactors must preserve public CLI, profile, status, history, recovery and
package contracts and pass the real-Btrfs regression suite when storage
semantics are touched.

## Interoperability And Desktop

After stable CLI and D-Bus operations exist, add optional integrations in this
order:

- detect and later adopt suitable Snapper snapshots;
- dry-run import from btrbk configuration;
- snapshot diff for diagnostics and restore selection;
- KDE KIO read-only browsing;
- Dolphin previous-version actions;
- KRunner commands routed through the shared client API.

No integration may become a required dependency of the base runtime.

## Verification Investment

Maintain unit and real-Btrfs Docker coverage while adding an opt-in QEMU Arch
system test for actual hotplug, udev delivery, systemd startup, device loss,
ENOSPC and interruption at commit boundaries. Add fuzzing for untrusted JSON
and path inputs, sanitizers, formatting/static-analysis gates and GCC/Clang
coverage.

## Open Source And Release Maturity

Bring contributor and release governance up to the level of the runtime:

- publish a tested platform support matrix and distinguish native packages from
  best-effort packaging templates;
- add CodeQL as an appropriate pull-request gate;
- validate pull-request titles against the documented Conventional Commits
  types and scopes, without adding a runtime dependency;
- pin GitHub Actions by commit, enable Dependabot for Actions and review
  workflow permissions;
- automate tagged releases only after verifying `VERSION`, changelog, tests,
  reproducibility, checksums and artifact inventory;
- derive changelog, release-note and SemVer automation from Conventional
  Commits only after the title policy has remained stable in normal use;
- add signed checksums or artifact attestations/provenance;
- provide man pages and Bash, Zsh and Fish completions;
- document development versus stable versions clearly;
- improve first-screen README onboarding and add a current KDE screenshot;
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
