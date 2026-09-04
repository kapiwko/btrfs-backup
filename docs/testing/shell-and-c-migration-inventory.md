# Shell and C test migration inventory

This document freezes the migration baseline at commit `05de7fb3` and records
the owner, observable contract, privileges and intended replacement for every
tracked C helper and shell entry point present at that revision. Migration
status is informational: parity remains defined by the baseline behaviour and
must be demonstrated before an old entry point is removed.

Exit-code conventions in the baseline are `0` for success, `1` for a failed
scenario or tool operation, and `2` for invalid command-line usage unless a row
states otherwise. Files marked **security-critical** exercise a trust boundary
or own privileged system resources.

## Native integration clients

| Baseline owner | Scenarios and observable artifacts | Invocation and privileges | Parity replacement | Status |
|---|---|---|---|---|
| `tests/integration/BrowseSessionClient.c` | Opens a manager browse session, prints its JSON document, holds the unique D-Bus connection while a control file exists, then releases the connection so the manager removes the read-only browse mount. Exit `2` on bad arguments and `1` on a D-Bus/protocol error. | Separate unprivileged process on the system bus; no root requirement. | CMake-built C++23 `btrfsbackup-integration-browse-session-client`; retain caller-disconnect and mount-cleanup assertions. | Migrated; extended negative lifecycle coverage remains tracked. |
| `tests/integration/DeviceProvisioningClient.c` | Reads block geometry, selects candidates for all four provisioning modes, builds a plan, passes the LUKS secret by Unix FD, starts the operation and polls terminal status. Machine JSON is written to stdout. | Separate process; system D-Bus access and readable selected block device; destructive calls are **security-critical** and run only in Docker/QEMU. | CMake-built C++23 `btrfsbackup-integration-device-provisioning-client`; retain typed JSON, FD passing, topology checks, deadlines and distinct failure classes. | Migrated; full client error-code/negative matrix remains tracked. |

Neither replacement is installed. Both must be passed by `$<TARGET_FILE:...>`
to privileged runners, compiled as C++23, and included in GCC, Clang,
sanitizer, strict-warning and clang-tidy gates.

## Test and integration shell entry points

| Baseline owner | Scenarios and expected artifacts | Privilege / safety boundary | Planned replacement | Status |
|---|---|---|---|---|
| `tests/run-tests.sh` | Configures and builds the project; runs native CTest, syntax, installation rendering, profile JSON, command-surface and release-note contracts; supports static-only and targeted variants. | Developer/CI user; temporary build and fixture trees only. | CMake configure/build/test presets and labeled CTest workflow tests. | Removed after preset parity. |
| `tests/integration/dbus_manager_test.sh` | Private system bus, fake polkit, daemon startup/restart, public manager calls, authorization, caller identity/disconnect, signals and manager recovery. | Unprivileged private sockets/processes; **security-critical** authorization boundary. | C++ RAII fixture registered with the `dbus` CTest label and bounded deadlines. | Removed after C++ fixture parity. |
| `tests/integration/real_restore_engine_test.sh` | Loop-backed Btrfs repository; restore plan, subvolume execute, content/metadata comparison and drill cleanup. | Root, loop, mount and Btrfs ioctls; **security-critical** cleanup. | Root-only C++ fixture invoking public `btrfs-backupctl`, labeled `real-btrfs;restore`. | Removed; cancellation and failure-injected cleanup still require completion. |
| `tests/integration/docker/real-btrfs-test.sh` | Former installed-package and storage bootstrap harness. | Root inside a privileged container; loop, mounts, LUKS/device-mapper, partition tables, udev and systemd; **security-critical**. | `real_btrfs_suite.py` orchestrates the C++ scenario executables and owns container-local setup/cleanup. | Removed after verified stage-13 parity. |
| `tests/integration/docker/run-real-btrfs.sh` | Builds the container and Arch package, mounts package and test clients read-only, starts systemd container, runs the real-Btrfs scenario and removes the container/artifact staging tree. | Host Docker access, therefore effective root/kernel device access; **security-critical**. | `tests/integration/docker/run_real_btrfs.py`. | Removed after Python runner parity. |
| `tests/qemu/run_hotplug.py` | Builds guest/package artifacts, boots QEMU, provisions whole disk and partitions, exercises manager/helper interruption recovery and target detach/reattach through QMP, and verifies guest survival and cleanup. | KVM/QEMU, guest root, disposable images; **security-critical**, preferred isolation for device-policy proof. | Python host/VM orchestration with deadlines and a native QMP client; the guest payload remains declarative shell. | Migrated in stage 14. |
| `tests/systemd/check-security.sh` | Renders service units and runs `systemd-analyze security`, checking required sandbox properties for backup and provisioning helpers. | No root; analyzes temporary units. **Security-critical** policy regression gate. | CMake/CTest systemd security tests with labels. | Removed after CTest parity. |

### Real-Btrfs scenario parity

The monolithic Docker scenario cannot be removed until each row below has an
independent owner and records setup, public invocation, expected exit code,
artifacts, Btrfs state, cleanup and required kernel capabilities.

| Scenario | Baseline proof | Expected result | Migration stage |
|---|---|---|---|
| Full backup | First manager-triggered run and snapshot/UUID comparison | exit `0`; full stream; one local and remote snapshot; status/history and last-success committed | 10 |
| Incremental backup | Source mutation and second run | exit `0`; incremental parent used; two matching snapshot generations | 10 |
| Interrupted/incoming receive | Incoming cleanup assertions and pending interruption fixtures | no partial snapshot under committed repository; source-specific incoming root empty after recovery | 10–11 |
| Retention | Third successful run | exit `0`; exactly the newest two local and remote snapshots remain | 10 |
| Pending recovery | Before-receive orphan and after-commit marker cases | owned orphan removed; committed pair preserved; pending marker cleared | 11 |
| Target identity | UUID mismatch, source-on-target and mapper lifecycle cases | non-zero on mismatch/unsafe topology; source and unrelated mappings unchanged | 11 |
| Cancellation/manager independence | Long-running hook and manager stop | operation terminates through public control; worker lifecycle and final state remain correct | 11 |
| Browse session | Unprivileged client plus real manager/polkit | read-only constrained mount exists for caller lifetime and is removed on disconnect | 11 |
| Restore | Repository rebuild, plan, execute and drill | content, permissions and Btrfs lineage preserved; staging removed | 11 |
| Whole-device provisioning | Empty loop disk | exit `0`; GPT, LUKS2, Btrfs and profile publication | 12 |
| Existing partition | Selected partition with preserved sibling | exit `0`; sibling bytes and table identity unchanged | 12 |
| Unallocated space | GPT free extent | exit `0`; new partition contained in selected extent; existing partition unchanged | 12 |
| Existing target adoption | Compatible encrypted repository | exit `0`; no block mutation; existing LUKS/Btrfs/repository identities retained | 12 |
| Provisioning interruption | Manager death, helper death, device removal and power-loss recovery | terminal/recoverable state is accurate; owned mapper/mount/temp profile cleaned; retry is safe | 12 and QEMU |

## Quality, release and developer shell entry points

| Baseline owner | Contract and artifacts | Privileges | Planned replacement | Status |
|---|---|---|---|---|
| `tools/check-cpp-format.sh` | Checks changed or all C++ files without modifying them. | Developer user. | CMake `check-format` target. | Removed. |
| `tools/run-clang-tidy.sh` | Configures compile commands and analyzes the complete production source set. | Developer user. | CMake clang-tidy preset. | Removed. |
| `tools/run-clang-tidy-changed.sh` | Selects changed implementation lines and reports clang-tidy diagnostics. | Developer user. | `tools/run_clang_tidy_changed.py`. | Removed. |
| `tools/render_release_notes.py` | Validates arguments and renders release notes from repository metadata. | Developer/release user. | Python release-note renderer. | Migrated in stage 16. |
| `tools/install-local-release.sh` | Builds and installs a local release with stage diagnostics. | Build as user; installation may require elevation and is **security-sensitive**. | CMake install preset/target using `cmake --install`. | Pending stage 16. |
| `tools/build-release.sh` | Deterministic source/archive creation, CMake native payload, install staging, Arch/Deb/RPM/Nix/Gentoo packaging and package verification. | Release user plus optional container/package tools; package script execution is **security-sensitive**. | CPack/CMake install plus Python release orchestration. | Pending stage 17. |
| `tools/screenshots/render.py render` | Dispatches five screenshot compositions and writes README image artifacts plus a machine-readable manifest. | Desktop session. | Python renderer and scenario modules. | Migrated in stage 15. |
| `tools/screenshots/render.py container` | Creates the isolated screenshot-rendering container and writes artifacts into the mounted output directory. | Docker access. | Python container subcommand. | Migrated in stage 15. |
| `tools/screenshots/scenarios.py:active_window` | Captures the active application window. | Isolated Wayland/X11 desktop. | Python screenshot scenario. | Migrated in stage 15. |
| `tools/screenshots/scenarios.py:notification` | Creates/captures a desktop notification and cleans temporary state. | Isolated Plasma session and D-Bus. | Python screenshot scenario. | Migrated in stage 15. |
| `tools/screenshots/scenarios.py:dolphin` | Opens a controlled Dolphin view, captures it and cleans its temporary tree. | Isolated Plasma session. | Python screenshot scenario. | Migrated in stage 15. |
| `tools/screenshots/scenarios.py:plasma_widget` | Installs and captures the development widget in a disposable desktop session. | Isolated Plasma session. | Python screenshot scenario. | Migrated in stage 15. |
| `tools/screenshots/scenarios.py:system_settings` | Opens the KCM, captures it and cleans temporary state. | Isolated Plasma session. | Python screenshot scenario. | Migrated in stage 15. |

## CI owners

| Workflow | Baseline responsibility | Authoritative replacement contract |
|---|---|---|
| `.github/workflows/tests.yml` | GCC/Clang builds, unit/integration tests, format, clang-tidy, sanitizer, strict-warning, KDE and architecture gates. | Invoke the same named CMake/CTest presets documented for developers; integration clients must remain in compile/static-analysis gates. |
| `.github/workflows/systemd-security.yml` | Dedicated systemd unit sandbox analysis. | Invoke labeled CTest systemd-security tests; no private shell runner. |

## Removal gate

A baseline entry may be deleted only when its replacement uses the same public
CLI or D-Bus boundary, retains at least the same meaningful assertions and
error cases, has bounded process waits, reports cleanup failures, and passes the
relevant GCC, Clang, sanitizer, strict-warning and static-analysis gates. Root
tests additionally require an isolated disposable environment, explicit device
ownership checks and a complete resource-cleanup report.
