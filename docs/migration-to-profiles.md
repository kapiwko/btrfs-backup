# Migration To Profiles

Version 0.1.1 introduces profile configuration files:

```text
/etc/btrfs-backup/profiles.d/<profile>.env
/etc/btrfs-backup/profiles/<profile>/profile.json
/etc/btrfs-backup/profiles/<profile>/sources.d/*.conf
```

The legacy `/etc/btrfs-backup/backup.env` fallback still works for the
`default` profile in 1.x, but it is deprecated and will be removed in 0.2.

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

After confirming that the profile works, the legacy file can be moved aside:

```bash
sudo btrfs-backup-migrate-profile --profile default --force --remove-legacy
```

The command keeps a timestamped backup next to the original legacy file.

## Regenerate Systemd And Udev Files

Newly rendered configuration includes:

```text
/etc/btrfs-backup/profiles.d/<profile>.env
/etc/btrfs-backup/profiles/<profile>/profile.json
/etc/btrfs-backup/profiles/<profile>/sources.d/*.conf
/etc/systemd/system/btrfs-backup@.service
/etc/udev/rules.d/99-btrfs-backup.rules
/var/lib/btrfs-backup/public/profiles/<profile>.json
```

The udev rule starts `btrfs-backup@<profile>.service`. Reload systemd and udev
after applying generated files:

```bash
sudo systemctl daemon-reload
sudo udevadm control --reload
```
