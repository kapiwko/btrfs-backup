# C++ Source Layout

The C++ code lives under `cpp/` and is split by responsibility:

```text
cpp/
├── apps/                  # command-line entry points
├── include/btrfsbackup/   # public headers for reusable core code
├── src/                   # reusable implementation units
└── tests/                 # native unit tests and fixtures
```

The current CMake targets are layered this way:

```text
btrfsbackup-model   # JSON model, validation, identifiers, status/catalog shapes
btrfsbackup-system  # POSIX, trusted files, process runner, mount/device/Btrfs inspection
btrfsbackup-core    # commands, profile rendering, runner planning/execution glue
btrfs-backup        # native backup runtime entry point
btrfs-backupctl     # command-line executable
```

`btrfsbackup-core` links both lower layers. Tests currently link `core` unless a
test intentionally targets a narrower layer.

Rules for new C++ code:

1. Tiny CLI `main` functions belong in `cpp/apps/`; reusable command parsing
   and behavior belong under `cpp/include/btrfsbackup/` and `cpp/src/`.
2. Profile, status, history, validation, filesystem, and command-runner logic
   belong in reusable code under `cpp/include/btrfsbackup/` and `cpp/src/`.
3. External commands must be invoked without a shell; pass executable and
   arguments separately. Process creation must use the shared `posix_spawn`
   adapter rather than `fork()` followed by C++ work before `exec`.
4. File writes that affect runtime state or configuration must use same-directory
   temporary files, `fsync`, atomic rename, and directory `fsync` where practical.
5. Keep root-only state separate from public status/history data.
6. Do not introduce UI or session dependencies into the base package.
7. Prefer small types with explicit validation over passing raw JSON through the
   codebase.
8. Keep compatibility coverage for real Btrfs runner behavior after the legacy
   Bash backup runtime and standalone target wrappers have been removed.
9. Keep model code independent from systemd, D-Bus, Qt, desktop libraries,
   block-device libraries, mount libraries, and LUKS libraries.
10. Use Linux system libraries in system-facing code when they replace command
    output parsing: `libbtrfsutil` for subvolumes, `libmount` for mount-table
    inspection, and `libblkid` for filesystem identity.
11. Keep long-running transfer process orchestration separate from short
    synchronous administrative commands. The backup executor uses an
    asynchronous transfer handle around the POSIX pump. Cancellation and
    transfer completion are exposed as pollable file descriptors, and the POSIX
    pump polls process pipes and cancellation together. A future event-loop
    runner can replace the threaded adapter, while simple tested POSIX execution
    remains available for small operations and unit tests.
12. Do not make the base package depend on a graphical session. Any future
    desktop integration must communicate with the system backend instead of
    becoming part of the backup core.

The migrated native runtime currently lives in `btrfs-backup` and
`btrfs-backupctl`. Their CLI entry points stay in `cpp/apps/`, while reusable
implementation is built from `cpp/src/`. New shared logic should go through
headers under `cpp/include/btrfsbackup/` before it is used by another command.
