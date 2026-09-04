# Package Lifecycle Inventory

The installable packages contain no lifecycle shell scriptlets. Package
installation is declarative: CMake installs files, `systemd-tmpfiles` owns the
standard state and status directory modes, and distribution package hooks
reload systemd, D-Bus, udev and desktop caches when their native formats
require it.

| Package path | Lifecycle code | Replacement or reason |
|---|---|---|
| Arch base package | None | `/usr/lib/tmpfiles.d/btrfs-backup.conf` and standard pacman hooks |
| Arch KDE package | None | Standard Plasma and desktop database hooks; session restart is a user choice |
| CPack DEB/RPM | None | Native package triggers and the installed tmpfiles declaration |
| RPM, Nix and Gentoo definitions | None | Their native packaging mechanisms consume the CMake install tree |

No `systemd-sysusers` entry is installed because the device, mount and backup
operations intentionally run as root. Introducing an unused service account
would imply a privilege boundary that the runtime does not implement.

No application-specific systemd preset is installed. The shipped services
have no `[Install]` section: the manager is activated on demand through D-Bus,
and profile units are started by udev or explicit commands. Package
installation therefore cannot silently enable a persistent service.

Upgrades do not rewrite administrator-owned files under `/etc`, restart an
active manager, or regenerate profile-specific systemd and udev artifacts.
After an upgrade that changes generated artifacts, the administrator performs
the migration explicitly:

```bash
sudo btrfs-backupctl profile regenerate --all
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
sudo systemctl try-restart btrfs-backupd.service
```

The command preserves profile choices and reports individual profiles that
cannot be regenerated. Existing backup runner instances are not restarted.
