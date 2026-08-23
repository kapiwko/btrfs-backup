# Migration To Profiles

Version 1.1 introduces profile configuration files:

```text
/etc/btrfs-backup/profiles.d/<profile>.env
/etc/btrfs-backup/profiles/<profile>/profile.json
```

The backup runtime uses `profile.json` for source definitions.

The legacy `/etc/btrfs-backup/backup.env` fallback still works for the
`default` profile in 1.x, but it is deprecated and will be removed in 2.0.

For new tooling, the canonical source format is JSON. Use
`btrfs-backup-profile save --file profile.json` to generate the runtime profile
files from that JSON.

## Convert The Existing Configuration

Create the default profile from the legacy file and its `SOURCES_DIR` source
definitions:

```bash
sudo btrfs-backup-migrate-profile --profile default
```

Validate the migrated profile with the target connected:

```bash
sudo btrfs-backup --profile default --validate --no-eject
btrfs-backupctl list-profiles
btrfs-backupctl status --profile default --human
```

After confirming that the profile works, the legacy configuration, source
directory, and old udev rule can be moved aside:

```bash
sudo btrfs-backup-migrate-profile --profile default --force --remove-legacy
```

The command keeps timestamped backups next to the original legacy paths.

## Regenerate Systemd And Udev Files

Newly rendered configuration includes:

```text
/etc/btrfs-backup/profiles.d/<profile>.env
/etc/btrfs-backup/profiles/<profile>/profile.json
/etc/systemd/system/btrfs-backup@.service
/etc/udev/rules.d/99-btrfs-backup.rules
/var/lib/btrfs-backup/public/profiles/<profile>.json
```

The udev rule starts `btrfs-backup@<profile>.service`. Reload systemd and udev
after applying generated files:

```bash
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
```
