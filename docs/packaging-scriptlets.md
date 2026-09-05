# Package Lifecycle Inventory

Normal package installation is declarative: CMake installs files,
`systemd-tmpfiles` owns the standard state and status directory modes, and
distribution package hooks reload systemd, D-Bus, udev and desktop caches when
their native formats require it. Packages contain no application-specific
pre-install or migration script.

| Package path | Lifecycle code | Replacement or reason |
|---|---|---|
| Arch base package | None | Declarative files plus standard systemd, udev and desktop cache handling |
| Arch KDE package | None | Standard Plasma and desktop database hooks; session restart is a user choice |
| CPack DEB/RPM | None | Declarative install; no supported legacy profile migration |
| RPM and Gentoo definitions | None | Declarative install; no supported legacy profile migration |
| Nix definition | None | Immutable package replacement does not mutate `/etc` |

No `systemd-sysusers` entry is installed because the device, mount and backup
operations intentionally run as root. Introducing an unused service account
would imply a privilege boundary that the runtime does not implement.

No application-specific systemd preset is installed. The shipped services
have no `[Install]` section: the manager is activated on demand through D-Bus,
and profile units are started by udev or explicit commands. Package
installation therefore cannot silently enable a persistent service.

Upgrades do not rewrite administrator-owned files under `/etc`, restart an
active manager, or regenerate profile-specific systemd and udev artifacts.
Version 1.0 has no deployed legacy profile base to migrate, so its packages do
not carry a 3.x bridge or transaction gate. The runtime accepts only canonical
v4 profiles and rejects other schemas without modifying them.

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
