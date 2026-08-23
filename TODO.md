# TODO

## Configuration Layout

- Treat `/etc/btrfs-backup/profiles/<profile>/profile.json` as the canonical
  source of truth for profile configuration.
- Keep `/etc/btrfs-backup/profiles.d/<profile>.env` and
  `/etc/btrfs-backup/profiles/<profile>/sources.d/*.conf` as generated runtime
  files while the backup runner is implemented in Bash.
- In a future major version, consider moving generated runtime files out of
  `/etc` into a clearly generated location, for example
  `/var/lib/btrfs-backup/generated/profiles/<profile>/`.
- In 0.2, remove the legacy `/etc/btrfs-backup/backup.env` fallback after users
  have had a full 1.x migration path through `btrfs-backup-migrate-profile`.
