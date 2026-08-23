# Architecture

## Goal

The project backs up one or more Btrfs subvolumes to a removable Btrfs disk placed inside LUKS. The disk can stay unmounted and closed most of the time.

## Runtime Flow

```text
matching LUKS partition appears
        |
        v
udev rule sets SYSTEMD_WANTS=btrfs-backup.service
        |
        v
btrfs-backup.service
        |
        |-- flock lock
        |-- daily limit and configuration fingerprint check
        |-- start the mount unit generated from fstab
        |-- validate LUKS, mapper, Btrfs, and mount point
        |-- verify that sources are on a different Btrfs filesystem than the target
        |
        |-- for each enabled source:
        |     |-- validate target paths and symlinks
        |     |-- recover pending state from a previous interruption
        |     |-- create a local read-only snapshot
        |     |-- select the incremental parent by UUID
        |     |-- btrfs receive into .incoming
        |     |-- verify read-only state and Received UUID
        |     |-- commit a read-only target snapshot
        |     `-- local and remote retention
        |
        |-- sync and write last-success
        |
        v
ExecStopPost=btrfs-backup-eject
        |
        |-- sync
        |-- unmount the expected target
        |-- check for remaining mapper mounts
        |-- stop the matching systemd-cryptsetup unit
        `-- report that the media can be disconnected
```

## Single Startup Source

The udev rule is only responsible for starting the service on an `add` event. There is no removal handler because after physical device removal it is too late to safely flush buffers and unmount.

The mount unit does not start the backup service. The service starts the mount unit itself, so there is no `service -> mount -> service` dependency cycle.

The service has no `[Install]` section and is not intended to be enabled with `systemctl enable`.

## Commit Model

Each receive first lands under:

```text
<INCOMING_ROOT>/<SOURCE_NAME>/<RUN_ID>/
```

After the transfer completes, the script verifies:

1. the expected subvolume exists;
2. it is read-only;
3. the local snapshot UUID matches the target-side `Received UUID`.

Only then is the subvolume moved into the final snapshot directory. The move happens on the same Btrfs filesystem, so data is not copied again.

## Interrupted Runs

Before creating a local snapshot, the script writes a private `pending-<source>` file in the profile state directory under `STATE_DIR/profiles/<PROFILE_ID>`. On the next run, it:

1. checks whether the local snapshot still exists;
2. searches the target for a subvolume with a matching `Received UUID`;
3. preserves the local snapshot if the remote commit already happened;
4. removes an orphaned snapshot or keeps it according to `KEEP_FAILED_LOCAL_SNAPSHOT`;
5. cleans stale `.incoming` data.

If the target becomes unavailable while handling an error, the local snapshot and pending marker are preserved. The next successful target connection resolves them.

## Incremental Parent

Snapshot names are used for sorting and display. A parent is considered common only when the local snapshot UUID equals the `Received UUID` of a target snapshot. This prevents using an unrelated directory with the same name.

## Daily Limit

`last-success` is written in the profile state directory only after all sources complete, the target is synchronized, and retention is applied. It contains the date, profile id, profile name, target LUKS UUID, and SHA-256 fingerprint of the main configuration and all source files. Changing the configuration forces a new run even on the same day.

## Filesystem and Path Boundaries

For each source, the script compares the Btrfs filesystem UUID and falls back to the device number if the UUID is unavailable. The source and local snapshot directory must belong to the same Btrfs filesystem, while the source and target must belong to different filesystems.

`REMOTE_ROOT`, `INCOMING_ROOT`, and per-source target directories are canonicalized before writes. An existing symlink that escapes the expected target directory aborts the operation instead of writing outside the backup repository.
