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

The first migrated native tool is `btrfs-backup-profile`. It is still mostly a
single translation unit, but future changes should move reusable pieces out of
`cpp/apps/btrfs-backup-profile.cpp` into the library layout above before they are
shared by another command.
