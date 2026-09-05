# Interactive KDE Laboratory

The laboratory runs the current project code in a disposable Arch Linux
virtual machine with Plasma, Dolphin, real systemd, and separate Btrfs and
LUKS2 disk images. Libvirt manages the guest and opens it in virt-manager. The
guest does not use any backup disks or configuration from the host.

Docker, libvirt with QEMU/KVM, `virsh`, and `virt-manager` are required. The
user must have access to the Docker daemon and the selected libvirt connection.
Start the laboratory with:

```bash
make manual-lab
```

The equivalent canonical CMake commands are:

```bash
cmake --preset kde-debug
cmake --build --preset kde-debug --target manual-kde-lab --parallel
```

The runner tries `qemu:///system` and then `qemu:///session`. Select a connection
explicitly when necessary:

```bash
LIBVIRT_DEFAULT_URI=qemu:///session make manual-lab
```

Both Arch packages are built locally from the current tree and then copied to
the guest's installation disk. Docker only prepares the base Arch filesystem
and disposable disk images. The first run also builds the container image and
a cached guest root, so it takes longer. Later runs reuse the guest root. Every
session receives fresh qcow2 overlay images. Changes made in the guest disappear
when it closes, while base images remain under `build/manual-qemu-cache`.
Remove that directory to force a clean rebuild. The lower-level runner receives
the locally built `btrfs-backup` and `btrfs-backup-kde` packages through
`PACKAGE_DIR`; the CMake target sets it automatically.

The script creates `btrfs-backup-manual-lab` and opens its graphical console.
The desktop and sudo credentials are `tester` / `tester`; the recovery key for
the encrypted target is `manual-backup-key`. The fixture provides:

1. a “Manual test backup” profile with home and web-server sources;
2. two versions of its files, including a long CSV name, hidden files, and
   files with different owners and access modes;
3. a 384 MiB versioned file plus a shortcut that writes a 1.5 GiB change for
   progress and cancellation testing, and a 24 MiB destination for an
   insufficient-space restore;
4. a destination containing an existing `report.txt` for conflict handling;
5. four extra provisioning devices: a blank whole disk, a GPT disk with an
   existing 512 MiB partition and unallocated space, an incompatible ext4
   disk, and an independent LUKS2/Btrfs adoption candidate whose passphrase is
   `manual-adopt-key`;
6. desktop shortcuts for Dolphin versions, the KCM, the widget, source files,
   service logs, and preparation of another large transfer.

The terminal running the host script accepts these commands:

- `disconnect` physically removes the virtual backup disk;
- `connect` attaches the same disk and generates a real hotplug event;
- `status` prints the domain and disk state;
- `quit` shuts the guest down and removes the transient domain.

This covers full and incremental backup, progress and cancellation, eject,
device removal during work, reconnect, version browsing, file restore, name
conflicts, insufficient space, and permission checks. Use the “Service logs”
desktop shortcut or `build/manual-qemu-cache/current/console.log` on the host
for diagnostics.

Docker and a system libvirt connection grant control equivalent to root on the
host. The script limits its block operations to image files under its cache,
but the laboratory should be run only from a trusted project tree.
