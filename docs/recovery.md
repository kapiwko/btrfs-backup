# Recovery

## Basic Rule

Do not delete or modify the only backup copy before verifying recovered data. First open the target, mount it read-only, or work on additional storage.

## Open the Target

Manual example:

```bash
sudo cryptsetup open /dev/disk/by-uuid/<LUKS-UUID> backupdisk
sudo mount -o ro /dev/mapper/backupdisk /mnt/btrfs-backup/default
```

## Back Up The LUKS Header

Keep a header backup for every encrypted target. Use the persistent LUKS UUID
path and write the backup directly to separate trusted storage:

```bash
sudo install -d -m 0700 /root/luks-header-backups
sudo cryptsetup luksHeaderBackup \
  --header-backup-file /root/luks-header-backups/backupdisk.header \
  /dev/disk/by-uuid/<LUKS-UUID>
sudo chmod 0600 /root/luks-header-backups/backupdisk.header
```

Copy that file to protected offline storage which is not on the backup disk.
The header contains the keyslot material from the moment it was created. The
header file together with a passphrase that was valid then can still unlock the
data even after that passphrase is later removed from the live device. Treat
every old header copy as a sensitive recovery credential and securely retire
copies that are no longer part of the recovery plan.

Record the LUKS UUID, physical disk identity, header-backup checksum and backup
date separately. Repeat the backup after creating or changing keyslots and
keep at least one verified earlier copy until the replacement has been copied
off the machine.

Restore a header only after confirming the exact inactive target device and
making a byte-for-byte image when the damaged media is still readable:

```bash
sudo cryptsetup luksHeaderRestore \
  --header-backup-file /path/to/backupdisk.header \
  /dev/disk/by-id/<EXACT-TARGET-PARTITION>
```

This replaces every current keyslot with the keyslots stored in the backup.
After restoration, only passphrases represented by that saved header work.
Never test this command against the sole copy of the encrypted data; use a
clone or a disposable recovery drill first.

## Discover And Browse A Repository

For a format v1 repository, inspect its identity and snapshots before choosing
data to restore:

```bash
btrfs-backupctl restore catalog \
  --repository /mnt/btrfs-backup/default/btrfs-backup

btrfs-backupctl restore list \
  --repository /mnt/btrfs-backup/default/btrfs-backup \
  --snapshot <snapshot-id> \
  --source Documents

btrfs-backupctl restore versions \
  --repository /mnt/btrfs-backup/default/btrfs-backup \
  --host <host-id> \
  --profile <profile-id> \
  --source-id <source-id> \
  --source Documents/report.odt
```

Discovery validates `repository.json`, `catalog.json`, snapshot paths,
read-only state and Btrfs UUID identity. It starts only from an already mounted
repository and never unlocks or mounts a target.

Preview a transactional file or directory restore, then execute the same
request:

```bash
btrfs-backupctl restore plan \
  --repository /mnt/btrfs-backup/default/btrfs-backup \
  --snapshot <snapshot-id> \
  --source Documents \
  --destination /mnt/restore/Documents \
  --transaction restore-20260831

btrfs-backupctl restore execute \
  --repository /mnt/btrfs-backup/default/btrfs-backup \
  --snapshot <snapshot-id> \
  --source Documents \
  --destination /mnt/restore/Documents \
  --transaction restore-20260831
```

Add `--subvolume` to create a restored Btrfs subvolume. Existing destinations
are rejected unless `--replace` is explicit. The engine stages the result,
rejects traversal, symlinks, special files and nested mount boundaries, and
publishes only a complete restore.

With the matching KDE package installed, the same repository can be opened
read-only from the plasmoid or KCM. Dolphin's previous-versions action and
`btrfs-backup-kde-restore` use caller-bound manager sessions and the same
restore engine.

List snapshots:

```bash
sudo btrfs subvolume list -r /mnt/btrfs-backup/default
sudo btrfs subvolume show /mnt/btrfs-backup/default/snapshots/home/<snapshot>
```

## Restore Individual Files

A read-only snapshot is available like a normal directory:

```bash
sudo rsync -aHAX --numeric-ids \
  /mnt/btrfs-backup/default/snapshots/home/<snapshot>/Documents/ \
  /mnt/restore/Documents/
```

Copy into a working directory first. Overwriting an active `/home` while the system is running can create inconsistencies.

## Restore a Whole Subvolume to a New Btrfs Filesystem

Assume the new Btrfs filesystem is mounted at `/mnt/new-btrfs`:

```bash
sudo mkdir -p /mnt/new-btrfs/receive
sudo btrfs send \
  /mnt/btrfs-backup/default/snapshots/home/<snapshot> \
  | sudo btrfs receive /mnt/new-btrfs/receive
```

The received subvolume remains read-only. Create a writable snapshot from it:

```bash
sudo btrfs subvolume snapshot \
  /mnt/new-btrfs/receive/<snapshot> \
  /mnt/new-btrfs/@home-restored
```

Check ownership, ACLs, extended attributes, and representative file reads before changing mount configuration.

## Consistency Check

For the local snapshot used in the transfer:

```bash
sudo btrfs subvolume show <local-snapshot>
```

For the target copy:

```bash
sudo btrfs subvolume show <remote-snapshot>
```

The local snapshot `UUID` should match the remote snapshot `Received UUID`.

## Interrupted State

Device preparation transactions are stored privately under:

```text
/var/lib/btrfs-backup/device-preparations/<OPERATION_ID>.json
```

The preparation helper writes the record atomically after each phase and after
discovering the partition, mapper, and filesystem UUIDs. A manager restart does
not terminate an active helper; status is reconstructed from the durable
record. If the helper exits before recording a terminal state, the manager
changes the operation to `interrupted` and launches a separate helper instance
to close a recorded mapper. `GetDevicePreparation` then returns a
`recoveryAction`; use the recorded stable device identity, last completed phase,
UUIDs, configuration state, credential state, and cleanup result to inspect the
partial device manually. Neither process resumes a destructive phase
automatically because the passphrase is not persisted.

The record also carries revisioned device-access requests and acknowledgements.
After a restart the manager replaces the unit's runtime allow list from the
current record before cleanup; it never reuses permissions left by another
operation.

Do not delete an interrupted transaction until the partial block structures and
installed profile or credential artifacts have been reconciled. Completed and
interrupted records are retained for 30 days, with a maximum of 128 records.

Files named `/var/lib/btrfs-backup/profiles/<profile>/pending-*` mark a run whose final state has not been resolved. Do not delete them manually without checking both the local and remote snapshots.

After reconnecting the correct target, run:

```bash
sudo btrfs-backup --force
```

The runtime checks `Received UUID`, preserves a committed local parent, or removes an orphaned snapshot according to configuration. A pending marker also records the planned final target path. If an earlier commit left an unverified snapshot at that canonical path, recovery removes it before clearing the marker; a failed removal leaves the marker in place for another recovery attempt.

Status code `repository.recovery_required` means final-snapshot verification
failed and the runtime could not remove the unverified snapshot immediately.
Keep the target connected and run the forced backup above to retry the normal
recovery path. Inspect the reported final path if recovery fails again.

Remaining data under `.incoming` is uncommitted and is cleaned during the next real run. Do not treat it as a valid backup.

## Restore Drill

At least periodically, perform a full drill:

1. choose the latest snapshot for each source;
2. receive it onto an empty test Btrfs filesystem;
3. create a writable snapshot;
4. check file counts, permissions, ACLs, xattrs, and a sample of checksums;
5. start applications that use the recovered data or perform a boot test in an isolated environment;
6. record the date and result.

Only this kind of test proves that the backup is practically usable.
