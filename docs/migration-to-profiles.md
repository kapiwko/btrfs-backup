# Migration To Profiles

Version 0.2 uses profile JSON as the runtime configuration file:

```text
/etc/btrfs-backup/profiles/<profile>/profile.json
```

The legacy `/etc/btrfs-backup/backup.env` file is accepted only by the migrator.
The runtime requires profile JSON.

For new tooling, the canonical source format is JSON. Use
`btrfs-backupctl profile save --file profile.json` to generate runtime files from
that JSON.

## Convert The Existing Configuration

Create the default profile from the legacy file and its `SOURCES_DIR` source
definitions:

```bash
sudo btrfs-backupctl migrate-profile --profile default
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
sudo btrfs-backupctl migrate-profile --profile default --force --remove-legacy
```

The command keeps timestamped backups next to the original legacy paths.

## Regenerate Systemd And Udev Files

Newly rendered configuration includes:

```text
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
