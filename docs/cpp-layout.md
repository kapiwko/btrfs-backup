# C++ Source Layout

The C++ code lives under `cpp/` and is split by responsibility:

```text
cpp/
├── apps/                  # command-line entry points
├── include/btrfsbackup/   # public headers for reusable core code
├── src/                   # reusable implementation units
└── tests/                 # native unit tests and fixtures
```

Rules for new C++ code:

1. CLI parsing and process exit behavior belong in `cpp/apps/`.
2. Profile, status, history, validation, filesystem, and command-runner logic
   belong in reusable code under `cpp/include/btrfsbackup/` and `cpp/src/`.
3. External commands must be invoked without a shell; pass executable and
   arguments separately.
4. File writes that affect runtime state or configuration must use same-directory
   temporary files, `fsync`, atomic rename, and directory `fsync` where practical.
5. Keep root-only state separate from public status/history data.
6. Do not introduce UI or session dependencies into the base package.
7. Prefer small types with explicit validation over passing raw JSON through the
   codebase.
8. Keep compatibility tests against the existing Bash behavior until the Bash
   implementation is intentionally removed.
9. Keep model code independent from systemd, D-Bus, Qt, desktop libraries,
   block-device libraries, mount libraries, and LUKS libraries.
10. Use Linux system libraries in system-facing code when they replace command
    output parsing: `libbtrfsutil` for subvolumes, `libmount` for mount-table
    inspection, and `libblkid` for filesystem identity.
11. Keep long-running transfer process orchestration separate from short
    synchronous administrative commands. A future asynchronous runner may use an
    event loop, but simple tested POSIX execution should remain available for
    small operations and unit tests.
12. Do not make the base package depend on a graphical session. Any future
    desktop integration must communicate with the system backend instead of
    becoming part of the backup core.

The migrated native profile and status tooling currently lives in
`btrfs-backupctl`. Its CLI entry point stays in `cpp/apps/`, while reusable
implementation is built from `cpp/src/`. New shared logic should go through
headers under `cpp/include/btrfsbackup/` before it is used by another command.
