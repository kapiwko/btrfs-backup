# Package Lifecycle Inventory

Normal package installation is declarative: CMake installs files,
`systemd-tmpfiles` owns the standard state and status directory modes, and
distribution package hooks reload systemd, D-Bus, udev and desktop caches when
their native formats require it. The sole application-specific lifecycle
action is a fail-closed 1.0 pre-upgrade gate; it is read-only and never edits
administrator configuration.

| Package path | Lifecycle code | Replacement or reason |
|---|---|---|
| Arch base package | `pre_upgrade` | Runs the installed migration preflight when profiles exist |
| Arch KDE package | None | Standard Plasma and desktop database hooks; session restart is a user choice |
| CPack DEB/RPM | `preinst`/`%pre` | Runs the installed migration preflight when profiles exist |
| RPM and Gentoo definitions | `%pre`/`pkg_preinst` | Runs the installed migration preflight when profiles exist |
| Nix definition | None | Immutable package replacement does not mutate `/etc`; preflight remains an explicit activation prerequisite |

No `systemd-sysusers` entry is installed because the device, mount and backup
operations intentionally run as root. Introducing an unused service account
would imply a privilege boundary that the runtime does not implement.

No application-specific systemd preset is installed. The shipped services
have no `[Install]` section: the manager is activated on demand through D-Bus,
and profile units are started by udev or explicit commands. Package
installation therefore cannot silently enable a persistent service.

Upgrades do not rewrite administrator-owned files under `/etc`, restart an
active manager, or regenerate profile-specific systemd and udev artifacts.
Before a 1.0 upgrade, the administrator runs the read-only compatibility gate
and creates a non-overwriting configuration export:

```bash
sudo btrfs-backupctl upgrade preflight
sudo btrfs-backupctl profile export-v4 --all --output-dir /root/btrfs-backup-before-1.0
```

An exit status of `1` from preflight blocks the package transaction. If the
installed binary predates the migration command, the transaction also stops
and requests installation of the latest 0.3.x bridge release. Legacy profiles
are exported explicitly, saved as v4 while the old installation is still
active, and checked again. The gate does not mutate or silently migrate
administrator configuration.

After an upgrade that changes generated artifacts, the administrator performs
the regeneration explicitly:

```bash
sudo btrfs-backupctl profile regenerate --all
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
sudo systemctl try-restart btrfs-backupd.service
```

The command preserves profile choices and reports individual profiles that
cannot be regenerated. Existing backup runner instances are not restarted.
