# System D-Bus Authorization Contract

## Security Boundary

The system manager is a privileged service and its system D-Bus API is a
security boundary. The implemented interface is
`io.github.btrfsbackup.Manager1` and is protected by a default-deny bus policy.
Mutating methods obtain the caller identity from the active D-Bus
connection and polkit subject; they must never trust a UID, PID, user name, or
authorization result supplied as a method argument.

Read methods expose only the same presentation-safe information that is
currently public. They must not return paths, UUIDs, device nodes, hook
commands, private diagnostics, or unsanitized history details.

## Implemented API

The service owns `io.github.btrfsbackup.Manager1` on the system bus and exports
`/io/github/btrfsbackup/Manager1`. Every method returns one UTF-8 JSON document
in a D-Bus string. JSON schema versions are independent from the D-Bus
interface version.

The canonical introspection document is
`data/dbus/io.github.btrfsbackup.Manager1.xml`. It is installed under
`share/dbus-1/interfaces`, drives the generated Qt proxy used by the KDE
integration, and is checked against the daemon vtable during the architecture
tests.

`profileSchemaVersion` describes canonical profile compatibility,
`publicStatusSchemaVersion` describes `GetStatus`, `historySchemaVersion`
describes the sanitized history rows returned over D-Bus, and
`deviceStateSchemaVersion` describes `GetDeviceState`. Private persistence
schema versions are not advertised as public API versions.

| Method | Input signature | Output signature | Result |
|---|---|---|---|
| `GetCapabilities` | `()` | `(s)` | API/schema versions, features and `readOnly: false` |
| `ListProfiles` | `()` | `(s)` | sanitized public profile array |
| `GetStatus` | `(s profileId)` | `(s)` | public status schema 5, including run state, source position, timing and backup freshness timestamps |
| `GetHistorySanitized` | `(s profileId, u offset, u limit)` | `(s)` | sanitized history array |
| `GetDeviceState` | `(s profileId)` | `(s)` | labels, lifecycle booleans and optional filesystem usage without storage identifiers |
| `StartBackup` | `(s profileId)` | `(s)` | accepted systemd runner start |
| `CancelBackup` | `(s profileId, s runId)` | `(s)` | accepted run-scoped cancellation |
| `ValidateTarget` | `(s profileId)` | `(s)` | completed target validation |
| `EjectTarget` | `(s profileId)` | `(s)` | completed target eject |
| `SetProfileEnabled` | `(s profileId, b enabled)` | `(s)` | transactionally enables or disables automatic activation only |
| `GetProfileDetails` | `(s profileId)` | `(s)` | read-only profile details without hooks or key-file paths |
| `UpdateProfileSettings` | `(s profileId, s generation, s fingerprint, s request)` | `(s)` | changes the display name, daily limit and automatic eject policy |
| `AddProfileSource` | `(s profileId, s generation, s fingerprint, s request)` | `(s)` | adds a source from its name, subvolume and retention policy |
| `UpdateProfileSource` | `(s profileId, s sourceId, s generation, s fingerprint, s request)` | `(s)` | changes a source name and retention policy |
| `RemoveProfileSource` | `(s profileId, s sourceId, s generation, s fingerprint)` | `(s)` | removes a source definition without deleting backup data |
| `DeleteProfile` | `(s profileId, s generation, s fingerprint)` | `(s)` | transactionally removed profile artifacts |
| `OpenBrowseSession` | `(s profileId)` | `(s)` | caller-bound, expiring read-only repository session |
| `RenewBrowseSession` | `(s sessionId)` | `(s)` | extends the monotonic TTL of a session owned by the caller |
| `SetBrowseSessionActive` | `(s sessionId, b active)` | `(s)` | pins or unpins an owned session during an active transfer |
| `CloseBrowseSession` | `(s sessionId)` | `(s)` | closes a session owned by the caller |
| `ResolveBackupCoverage` | `(s localPath)` | `(s)` | presentation-safe profile/source coverage for a local path |
| `ListTargetCredentials` | `(s profileId)` | `(s)` | occupied LUKS2 keyslots with labels only for managed credentials |
| `AddTargetPassphrase` | `(s profileId, h authorization, h newSecret, s label)` | `(s)` | adds a LUKS2 passphrase through file descriptors |
| `AddTargetKey` | `(s profileId, h authorization, h key, s label, b automatic)` | `(s)` | imports and optionally activates a protected key file |
| `GenerateTargetKey` | `(s profileId, h authorization, s label, b automatic)` | `(s)` | generates a protected key without returning its bytes |
| `RemoveTargetCredential` | `(s profileId, s credentialId, h authorization)` | `(s)` | removes a managed non-automatic keyslot, never the last slot |
| `ListProvisioningDevices` | `()` | `(s)` | block-device candidates and destructive-operation warnings |
| `InspectStorageTopology` | `()` | `(s)` | caller-bound device, partition, and unallocated-region snapshot |
| `BuildDevicePreparationPlan` | `(s request)` | `(s)` | revalidates topology and creates a caller-bound whole-device or existing-partition before/after plan |
| `ListSourceCandidates` | `()` | `(s)` | mounted Btrfs subvolumes eligible as an initial source |
| `StartDevicePreparation` | `(s request, h passphrase)` | `(s)` | starts an asynchronous destructive device-preparation operation |
| `GetDevicePreparation` | `(s operationId)` | `(s)` | preparation state, phase, stable error and cancellation capability |
| `CancelDevicePreparation` | `(s operationId)` | `(s)` | requests cancellation before the first destructive phase |

History `limit` must be between 1 and 100 and `offset` must not exceed 10000.
Manager input files are regular, non-symlink files no larger than 1 MiB and
must not be writable by group or others. The daemon reads state for every
request, so a restart reconstructs the same visible state from current status
or durable history. Operational methods return schema-versioned
`OperationResult` documents.

API major version 2 removes the full-document editing methods and replaces them
with bounded profile settings and source operations. Each mutation validates and
atomically republishes the complete profile internally, while preserving fields
that are not part of its request. All successful responses use the sanitized
profile envelope with a new generation and fingerprint. The minor version
started at 0 because clients built for API major version 1 are not compatible
with this contract.

API minor version 1 advances profile summaries and profile-details envelopes to
schema version 2. Both expose a path-free configuration health result through
`configurationValid` and `configurationErrorCode`. Profile details additionally
include editable `sourceCandidates` discovered from mounted Btrfs subvolumes.
Adding a source verifies that the path exists and is a Btrfs subvolume before
requesting authorization. Existing profiles are checked on every summary or
details query, so clients can report stale source configuration independently
of backup execution.

API minor version 2 adds the `target-credentials` and `device-provisioning`
features. Secrets use Unix file descriptors (`h`) and never enter JSON or a
textual D-Bus argument. Device preparation is an asynchronous operation; the
initial call returns after the secret has been copied into protected memory.
Clients poll the operation document and may cancel only while `canCancel` is
true. The daemon revalidates the selected disk path, size, serial, mount state
and initial Btrfs source after polkit authorization and before erasing data.

API minor version 4 adds caller-owned browse-session renewal and active-operation
pinning. Session expiry uses a monotonic clock; the wall-clock expiry in the
response is informational. The daemon limits sessions globally and per UID,
keeps each view below a per-UID directory, and persists incomplete cleanup for
retry after failures or restart.

API minor version 5 adds `InspectStorageTopology` and
`BuildDevicePreparationPlan`. The first method returns opaque candidates bound
to the caller and a short-lived topology generation. The second method rescans
storage, rejects a changed generation, and returns a stored before/after plan.
It does not authorize a write and does not replace the separate preparation
authorization and revalidation performed immediately before execution.

The `target-storage-usage` feature is part of the current major-version
baseline. The device-state parent remains schema version 1 and may contain this
optional, independently versioned block:

```json
{
  "schemaVersion": 1,
  "profileId": "default",
  "targetName": "backupdisk",
  "state": "mounted",
  "connected": true,
  "unlocked": true,
  "mounted": true,
  "safeToRemove": false,
  "storage": {
    "schemaVersion": 1,
    "capacityBytes": 4000787030016,
    "usedBytes": 1280251849600,
    "availableBytes": 2720535180416,
    "usagePercent": 32,
    "measuredAt": "2026-08-30T12:34:56Z",
    "live": true,
    "spaceState": "normal"
  }
}
```

`storage` is absent when no verified live measurement or identity-matching
cache is available. `live: false` identifies the last persisted measurement;
`measuredAt` is always its UTC timestamp. `spaceState` is `normal` or
`below-configured-minimum`, calculated against the current profile. Clients
must ignore an unsupported or malformed optional storage block while retaining
valid lifecycle state. Storage presentation is enabled only when the manager
advertises the corresponding feature.

The capacity is the mounted Btrfs filesystem capacity, not block-device size.
The manager performs a live read only for an already mounted, identity-verified
target. `GetDeviceState` never unlocks or mounts a target. Cached measurements
are private, atomically stored by the runner before cleanup and rejected after
the configured target identity changes. No paths, UUIDs or device nodes cross
this API boundary.

The `mounted` lifecycle flag is true only when the mount at the configured
target path is Btrfs and matches both the configured filesystem UUID and mapper.
If another filesystem or device occupies that path, `mounted` is false and
`state` is `unexpected-mount`. In that state an identity-matching cached storage
measurement may still be present, but it is never marked as live.

The `change-signals` capability advertises the event-driven invalidation
surface. Signals carry only a public profile identifier; clients obtain the
current sanitized document with the corresponding read method:

| Signal | Signature | Invalidates |
|---|---|---|
| `ProfilesChanged` | `()` | `ListProfiles`, and `GetProfileDetails` for an open profile |
| `StatusChanged` | `(s profileId)` | `GetStatus` |
| `HistoryChanged` | `(s profileId)` | `GetHistorySanitized` |
| `DeviceStateChanged` | `(s profileId)` | `GetDeviceState` |

The manager derives these signals from inotify changes to public profiles,
runtime status and private history, plus udev block events and pollable kernel
mount notifications. Clients load state once after connecting and then react to
signals; a signal received during an outstanding read must schedule a coalesced
follow-up so the last change cannot be lost. A mutating response already carries
the newly published generation. The KCM refreshes both its profile list and the
currently open profile, including changes published by the CLI.

## Method Classes

| Method | Authorization | Polkit action |
|---|---|---|
| `GetCapabilities` | none | none |
| `GetStatus` | none | none |
| `GetHistorySanitized` | none | none |
| `ListProfiles` | none | none |
| `GetDeviceState` | none | none |
| `StartBackup` | operational | `io.github.btrfsbackup.start-backup` |
| `CancelBackup` | operational | `io.github.btrfsbackup.cancel-backup` |
| `EjectTarget` | operational | `io.github.btrfsbackup.eject-target` |
| `ValidateTarget` | operational | `io.github.btrfsbackup.validate-target` |
| `SetProfileEnabled` | operational | `io.github.btrfsbackup.set-profile-enabled` |
| `GetProfileDetails` | none | none |
| `UpdateProfileSettings` | administrative | `io.github.btrfsbackup.manage-profile-configuration` |
| `AddProfileSource` | administrative | `io.github.btrfsbackup.manage-profile-configuration` |
| `UpdateProfileSource` | administrative | `io.github.btrfsbackup.manage-profile-configuration` |
| `RemoveProfileSource` | administrative | `io.github.btrfsbackup.manage-profile-configuration` |
| `DeleteProfile` | administrative | `io.github.btrfsbackup.delete-profile-configuration` |
| `OpenBrowseSession` | repository access | `io.github.btrfsbackup.open-browse-session` |
| `RenewBrowseSession` | session ownership | none |
| `SetBrowseSessionActive` | session ownership | none |
| `CloseBrowseSession` | session ownership | none |
| `ResolveBackupCoverage` | none | none |

Operational backup controls are allowed without a password from the active
local session. The profile and hooks remain root-owned, so this grants control
over an already approved backup definition, not configuration or arbitrary
code execution. Eject still acquires the target lease and refuses to run while
the target is in use:

```xml
<defaults>
  <allow_any>no</allow_any>
  <allow_inactive>auth_admin</allow_inactive>
  <allow_active>yes</allow_active>
</defaults>
```

Ordinary profile mutations share one retained administrator authorization so a
short editing session does not prompt for every save:

```xml
<defaults>
  <allow_any>no</allow_any>
  <allow_inactive>auth_admin</allow_inactive>
  <allow_active>auth_admin_keep</allow_active>
</defaults>
```

The daemon still asks polkit to authorize every mutating call. Polkit scopes the
temporary retention to the same caller, subject and action identifier. Deleting
a profile and future hook or device-provisioning operations keep distinct,
stronger actions.

## Profile Writes

Domain requests are JSON objects with an explicit allowlist and a 64 KiB size
limit. The manager loads the current private profile, checks its generation and
fingerprint, applies only the fields owned by the operation, validates the full
canonical profile, authorizes the caller, checks for a race again and publishes
all managed artifacts atomically. These methods cannot create, erase or modify
hooks, target identity, key paths or technical layout fields.

Profile writes expose `configuration.save_failed` when the transaction fails
and the previous configuration is restored. If any rollback operation fails,
it exposes `configuration.rollback_incomplete` together with the primary save
failure and per-artifact rollback diagnostics. Clients must present the latter
as an operator-actionable failure; they must not retry automatically or infer
success from any individual file because generation validation deliberately
keeps a mixed installation inactive.

## Operation Rules

These rules describe the implemented API unless explicitly marked otherwise.

- `GetHistorySanitized` uses bounded pagination and returns stable codes plus
  presentation-safe labels, never the private history document.
- `ListProfiles` returns the sanitized public profile representation only.
- `GetDeviceState` returns lifecycle, safe-removal and optional filesystem-space
  state, not device nodes or storage identifiers. Its Btrfs capacity and usage
  values are `statvfs` approximations, not chunk or qgroup accounting.
- `StartBackup`, `CancelBackup`, `EjectTarget`, `ValidateTarget`, and
  `SetProfileEnabled` re-check the selected profile and target after
  authorization; object paths or identifiers cannot stand in for
  authorization.
- `SetProfileEnabled` loads the current root-owned profile and republishes its
  managed artifacts transactionally, changing only automatic activation.
- Profile saves and deletes compare the submitted generation and fingerprint
  before authorization and immediately before commit.
- Browse sessions are bound to the unique caller bus name and UID, expose only
  a verified read-only root and close on request, disconnect, expiry or daemon
  shutdown.
- `StartDevicePreparation` requires separate non-retained administrator
  authorization and a caller-bound, unexpired, single-use `planId`. The KCM
  additionally requires an explicit `ERASE` confirmation. The daemon rescans
  the topology generation before authorization and revalidates the exact
  device identity and use before the first destructive command.
- Existing-partition plans are currently preview-only. The start method rejects
  them before authorization until the partition-scoped executor and recovery
  path are available, so they cannot enter the whole-device executor.
- Authorization success is not persisted by the daemon. Only polkit controls
  caching: credential management uses `auth_admin_keep` for a short sequence of
  keyslot changes, while destructive device preparation and other high-risk
  administrative actions use non-keep authorization.
- After polkit returns, the daemon verifies that the unique caller bus name is
  still owned. A disconnected caller cannot complete a pending operation.
- Operational effects compare the profile generation and fingerprint captured
  before authorization with a fresh profile read immediately before the effect.
  A change during the authorization prompt returns `Conflict`.
- `ValidateTarget` runs in `btrfs-backup-validate@.service`, acquires the normal
  profile and target leases, and restores the mount and LUKS mapper state that
  existed before validation. The unit receives the authorized generation and
  fingerprint through a root-only runtime environment file.
- Mutating methods retain the existing profile and target lock boundaries.
- Structured, secret-free audit records are appended to the root-only manager
  audit log for every operational request. Records contain the D-Bus caller UID,
  action, profile, result and the same stable error code returned to clients.
  Normal service diagnostics in journald are not the audit contract.

## Required Tests

The system API tests verify unauthenticated reads, all four change signals,
recreation of an initially absent status root, distinct action identifiers and
caller subjects, caller disappearance during authorization, profile-version
races, hook authorization, browse-session ownership and cleanup, mismatched
cancellation, malformed input and manager restart. Inactive-session behavior
and cross-action policy delegation remain packaging/system integration
concerns.
