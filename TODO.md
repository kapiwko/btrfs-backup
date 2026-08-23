# TODO

## Configuration Layout

- Treat `/etc/btrfs-backup/profiles/<profile>/profile.json` as the canonical
  source of truth for profile configuration.
- Keep derived runtime values generated from JSON in memory instead of writing
  separate profile env files.
- Remove remaining legacy migration helpers once 2.0 no longer needs to import
  1.x installations.

## C++ Runtime Helper Migration

Port helpers from `scripts/lib/btrfs-backup-common.sh` in small, testable steps
before replacing larger runner flows.

- Add an RAII file lock for the future runner and manager, matching
  `bb_acquire_lock` and `bb_release_lock`.
