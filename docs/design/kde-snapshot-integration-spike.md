# KDE Snapshot Integration Spike

Status: implemented in 1.0.0.

Date: 2026-08-31.

Upstream examined: KDE `kio-snapshot` 1.0.0, revision
[`c0801089f9a15a4e24d625d6ea371406e780a8df`](https://invent.kde.org/system/kio-snapshot/-/commit/c0801089f9a15a4e24d625d6ea371406e780a8df).

## Decision

Do not make `kio-snapshot` the repository browser for `btrfs-backup` and do
not fork it. Keep both integrations installable side by side:

- `snapshot:` continues to expose snapshots that belong to a currently mounted
  local Btrfs filesystem;
- `btrfsbackup:` exposes catalog entries through an explicit, authorized,
  read-only browse session;
- the Dolphin plugin may present both actions when both providers apply;
- propose a provider boundary upstream after the `btrfsbackup:` implementation has proved
  its catalog and session contracts.

This is an interoperability decision, not a rejection of upstream. The worker,
read-only KIO metadata and Dolphin interaction patterns are useful references.
Its current storage and authorization assumptions are not interchangeable with
an encrypted removable backup repository.

## Evidence

The upstream [README](https://invent.kde.org/system/kio-snapshot/-/blob/c0801089f9a15a4e24d625d6ea371406e780a8df/README.md)
defines two features: a worker for local subvolume/file snapshots and a
`KFileItemActions` plugin. The source contains no provider interface or external
catalog registration point. `BtrfsSnapshots` is a concrete static helper over
`libbtrfsutil`, libmount and Solid, and the worker calls it directly.

The local probe `kio-snapshot-interop-probe` compiles against Qt only and checks
the observable URL requirements against the proposed repository URL. Run it
with:

```bash
ctest --test-dir build -R kio-snapshot-interop-probe --output-on-failure
```

It demonstrates the structural mismatch: an upstream URL needs a mounted
filesystem UUID plus numeric origin and snapshot subvolume IDs; a backup URL
needs stable repository/snapshot IDs and an authorization session, without a
public device authority.

## URL Analysis

The parser implements these forms:

```text
snapshot:///file/<absolute-local-path>
snapshot://<filesystem-uuid>/subvolume/<origin-subvol-id>
snapshot://<filesystem-uuid>/subvolume/<origin-subvol-id>/<snapshot-subvol-id>/<path>
```

The host is interpreted as a filesystem UUID. Solid resolves it to the current
mount path; an empty or unresolved host falls back to `/`. The subvolume form
uses filesystem-local numeric IDs. The file form embeds an absolute local path.
See upstream
[`snapshoturl.cpp`](https://invent.kde.org/system/kio-snapshot/-/blob/c0801089f9a15a4e24d625d6ea371406e780a8df/kioworker/snapshoturl.cpp).

These identifiers do not model a detached repository. A received snapshot can
live on another filesystem, its numeric subvolume ID is local to that target,
and the original path may no longer exist. A public backup URL must instead be
stable across target disconnects and must not reveal a mapper, mount path or
device UUID. The selected form is:

```text
btrfsbackup:/repositories/<repository-id>/sessions/<session-id>/snapshots/<snapshot-id>/<relative-path>
```

The session segment is an opaque capability reference, not a filesystem path.

## Worker And Plugin Analysis

The worker derives directly from `KIO::WorkerBase`. It opens the caller-bound
session root once and retains that directory descriptor while the session is
cached. Repository discovery uses `/proc/self/fd/<n>`, and subsequent listing,
metadata and file reads resolve every relative path component with
descriptor-relative `openat()` calls and `O_NOFOLLOW`. The worker therefore
does not re-resolve a local mount path between validation and access. An open
KIO file also keeps the browse session pinned until `close()`.

The protocol metadata declares reading and listing and disables writing,
moving, deletion, linking and directory creation. A D-Bus descriptor broker
could later remove the initial local session-path handoff as well; it is not
required for descriptor-stable access inside the current worker. The upstream
`kio-snapshot` reference instead forwards operations to `file:`; see
[`snapshot.cpp`](https://invent.kde.org/system/kio-snapshot/-/blob/c0801089f9a15a4e24d625d6ea371406e780a8df/kioworker/snapshot.cpp)
and
[`snapshot.json`](https://invent.kde.org/system/kio-snapshot/-/blob/c0801089f9a15a4e24d625d6ea371406e780a8df/kioworker/snapshot.json).

The context-menu plugin accepts one item, asks Solid for its mounted storage
volume, scans Btrfs snapshot ancestry, and only then creates `snapshot:` URLs.
It performs this scan while constructing the menu. See
[`snapshotfileitemaction.cpp`](https://invent.kde.org/system/kio-snapshot/-/blob/c0801089f9a15a4e24d625d6ea371406e780a8df/contextmenu/snapshotfileitemaction.cpp).

That behavior is correct for local snapshots but violates the backup product
boundary if copied directly: a Dolphin menu query must not activate, mount or
scan a removable target. The `btrfs-backup` action will use cached manager
metadata for visibility and open a session only after the user triggers it.

## Permission Model

`kio-snapshot` has no privileged broker or Polkit flow. It operates with the
worker user's permissions and only lists file versions for which `QFileInfo`
reports readability. The README tells Snapper users to configure `ALLOW_USERS`
and `SYNC_ACL`. File entries may publish `UDS_LOCAL_PATH` and a `file:` target.

The backup repository can contain root-owned files, historical UIDs and mode
`0600` data. Its manager must therefore authorize opening the repository,
validate the target identity, bind a lease to the D-Bus caller, enforce a
timeout and return only a session-scoped root. The 1.0 implementation retains
normal Unix read checks; broader privileged reads require a separate FD
broker and threat-model review. Raw repository paths are never part of the
public URL.

## Provider API Feasibility

There is currently no provider API to implement. A viable upstream extension
would have to separate:

1. applicability checks that are cheap and side-effect free;
2. stable snapshot and version enumeration;
3. opening and closing an authorized read capability;
4. mapping an opaque item to a KIO-readable stream or descriptor;
5. provider-specific lifecycle and errors;
6. a common presentation model that does not require numeric Btrfs IDs.

Retrofitting only a callback that returns local paths would be insufficient: it
would leak the session root and would not express caller binding, expiry or
target loss. The provider should be out-of-process or D-Bus based, with opaque
IDs and asynchronous jobs. Until KDE accepts and ships such a contract, a thin
dedicated worker has lower security and compatibility risk.

## Target Lifecycle Differences

| Concern | `kio-snapshot` | `btrfs-backup` repository |
|---|---|---|
| Storage | already mounted local Btrfs | removable, usually LUKS-encrypted |
| Discovery | enumerate current filesystem | validate format v1 catalog and Btrfs identity |
| Identity | filesystem UUID and numeric subvolume ID | repository and snapshot IDs plus UUID chain |
| Activation | none in worker | explicit manager operation and Polkit |
| Lifetime | follows local mount | caller-bound lease, timeout and disconnect cleanup |
| Public path | local absolute path may be exposed | only session-scoped relative paths |
| Failure | local item disappears | target disconnect, mapper loss, lease expiry, manager restart |

## Maintenance Cost

| Option | Initial cost | Ongoing cost | Risk |
|---|---:|---:|---|
| use upstream unchanged | low | low | cannot represent the repository lifecycle |
| fork `kio-snapshot` | high | high | permanent merge and security divergence |
| add a provider API upstream first | medium-high | medium | review and release timing block the product |
| thin `btrfsbackup:` worker, then upstream proposal | medium | low-medium | small duplicated KIO adapter, bounded by shared restore/catalog core |

The selected option keeps repository logic in the desktop-neutral engine. The
worker owns URL parsing and KIO result mapping only. This limits the code that
tracks KIO API changes and prevents a second snapshot catalog implementation.

## Upstream Plan

1. Collect concrete traces for local snapshot, removable repository, target
   disconnect and expired-session behavior.
2. Open a KDE issue describing provider use cases without proposing a
   `btrfs-backup`-specific API.
3. Offer a small provider interface and tests with local Btrfs as the reference
   provider and opaque session-backed storage as the second implementation.
4. Keep `snapshot:` URLs backward compatible; do not reinterpret their host or
   numeric path segments.
5. If accepted and released, adapt `btrfs-backup` to the provider boundary and
   deprecate only the duplicated presentation code, not the manager session API.
