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

The first migrated native tool is `btrfs-backup-profile`. Its CLI entry point
stays in `cpp/apps/btrfs-backup-profile.cpp`, while the implementation is built
as the reusable `btrfsbackup_profile_tool` library from `cpp/src/`. New shared
logic should go through headers under `cpp/include/btrfsbackup/` before it is
used by another command.
