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

- Add trusted-file checks for profile JSON, matching
  `bb_assert_trusted_config_file`; include owner, readability, and private mode
  validation with a test-only rootless policy.
- Port generic path and value validation helpers: unsigned integers, positive
  integers, absolute paths, relative paths, canonicalization, and
  `path_is_within`.
- Extend the new mount-info module with `mount_for_path`, `mount_at`,
  filesystem UUID or device fallback, and mapper matching equivalents for
  `bb_paths_are_same_filesystem` and `bb_mount_uses_mapper`.
- Add small device helpers for mapper paths, canonical device resolution, and
  stripping Btrfs subvolume suffixes from mount sources.
- Port free-space checks with `std::filesystem::space` as the C++ equivalent of
  `bb_available_bytes` and `bb_check_minimum_free_space`.
- Add an RAII file lock for the future runner and manager, matching
  `bb_acquire_lock` and `bb_release_lock`.
