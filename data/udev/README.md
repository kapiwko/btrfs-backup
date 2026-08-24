# Generated udev Rules

`btrfs-backup` does not ship one global udev rule. Each saved profile generates
its own rule at:

```text
/etc/udev/rules.d/99-btrfs-backup-<PROFILE_ID>.rules
```

The rule matches the configured LUKS UUID and optional partition UUID and
serial number. It requests `btrfs-backup@<PROFILE_ID>.service` through
`SYSTEMD_WANTS`; it never executes the backup command directly and has no
device-removal action.

`src/config/profile_render.cpp` owns the rule format. `profile save` and the
profile wizard own materialization and udev reload. Files below this directory
document that generated-data boundary; they must not be installed directly
into `/etc/udev/rules.d`.
