# Package Lifecycle Inventory

Normal package installation is declarative: CMake installs files,
`systemd-tmpfiles` owns the standard state and status directory modes, and
distribution package hooks reload systemd, D-Bus, udev and desktop caches when
their native formats require it. The application-specific 4.0 pre-upgrade
check is read-only and never edits administrator configuration.

| Package path | Lifecycle code | Replacement or reason |
|---|---|---|
| Arch base package | ALPM `PreTransaction` hook with `AbortOnFail` | Runs the installed migration preflight before package replacement; it must arrive in the 3.2.x bridge package |
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
Before a 4.0 upgrade, the administrator runs the read-only compatibility gate
and creates a non-overwriting configuration export:

```bash
sudo btrfs-backupctl upgrade preflight
sudo btrfs-backupctl profile export-v4 --all --output-dir /root/btrfs-backup-before-4.0
```

An exit status of `1` from preflight blocks DEB, RPM and Gentoo package
transactions. If the installed binary predates the migration command, those
transactions also stop and request installation of the latest 3.2.x bridge
release. On Arch, that bridge installs
`90-btrfs-backup-v4-migration.hook`; its `AbortOnFail` stops the transaction
before replacement when the installed preflight rejects a profile. A direct
3.2.0 to 4.0 transaction remains unsupported because an incoming package's
hook is not visible before its own transaction. The 4.0 runtime still rejects
legacy profiles and does not mutate them. Legacy profiles are exported
explicitly, saved as v4 while the bridge is active, and checked again.

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
