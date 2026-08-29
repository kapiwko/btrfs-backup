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

`profileSchemaVersion` describes canonical profile compatibility,
`publicStatusSchemaVersion` describes `GetStatus`, `historySchemaVersion`
describes the sanitized history rows returned over D-Bus, and
`deviceStateSchemaVersion` describes `GetDeviceState`. Private persistence
schema versions are not advertised as public API versions.

| Method | Input signature | Output signature | Result |
|---|---|---|---|
| `GetCapabilities` | `()` | `(s)` | API/schema versions, features and `readOnly: false` |
| `ListProfiles` | `()` | `(s)` | sanitized public profile array |
| `GetStatus` | `(s profileId)` | `(s)` | public status schema 3, including run phase and cancellable run id, or an unavailable status |
| `GetHistorySanitized` | `(s profileId, u offset, u limit)` | `(s)` | sanitized history array |
| `GetDeviceState` | `(s profileId)` | `(s)` | labels and lifecycle booleans without storage identifiers |
| `StartBackup` | `(s profileId)` | `(s)` | accepted systemd runner start |
| `CancelBackup` | `(s profileId, s runId)` | `(s)` | accepted run-scoped cancellation |
| `ValidateTarget` | `(s profileId)` | `(s)` | completed target validation |
| `EjectTarget` | `(s profileId)` | `(s)` | completed target eject |

History `limit` must be between 1 and 100 and `offset` must not exceed 10000.
Manager input files are regular, non-symlink files no larger than 1 MiB and
must not be writable by group or others. The daemon reads state for every
request, so a restart reconstructs the same visible state from current status
or durable history. Operational methods return schema-versioned `OperationResult` documents.

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
| `SaveProfile` | administrative | `io.github.btrfsbackup.save-profile` |
| `DeleteProfile` | administrative | `io.github.btrfsbackup.delete-profile` |
| `PrepareDevice` | administrative | `io.github.btrfsbackup.prepare-device` |
| `ChangeHooks` | code-execution risk | `io.github.btrfsbackup.change-hooks` |

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

The administrative and code-execution-risk actions must require a fresh
administrator decision and must not use `yes`, `auth_self`, `auth_self_keep`,
or any implicit active-session grant:

```xml
<defaults>
  <allow_any>auth_admin</allow_any>
  <allow_inactive>auth_admin</allow_inactive>
  <allow_active>auth_admin</allow_active>
</defaults>
```

Each row has a distinct action identifier so an administrator can delegate one
operation without implicitly delegating the others. The daemon still asks
polkit to authorize every call from the active graphical session; the policy,
rather than the daemon, provides the narrow passwordless grants described
above.

## Planned Profile And Hook Writes

The methods in this section are an accepted API design, not part of the
implemented `Manager1` surface. Their implementation remains roadmap work.

`SaveProfile` must parse and fully validate canonical profile data before it
requests authorization. System paths are application configuration and are not
accepted in profile input.

The manager must compare the validated hook set with the currently stored hook
set. A save that adds, removes, reorders, or changes a hook program, argument,
or timeout requires both `io.github.btrfsbackup.save-profile` and
`io.github.btrfsbackup.change-hooks`. The same rule applies when creating a new
profile containing hooks. An empty or omitted hook list must not erase existing
hooks unless the hook-change authorization was granted.

`ChangeHooks` will be a separate high-risk method for clients that edit hooks
directly. It is not an alternative path around `SaveProfile`: both methods use
the same validation, trusted hook directory restrictions, atomic persistence,
and, once delivered, stable audit event generation. Authorization must be
checked immediately before the commit and bound to the calling D-Bus
connection.

`SaveProfile` exposes `configuration.save_failed` when the transaction fails
and the previous configuration is restored. If any rollback operation fails,
it exposes `configuration.rollback_incomplete` together with the primary save
failure and per-artifact rollback diagnostics. Clients must present the latter
as an operator-actionable failure; they must not retry automatically or infer
success from any individual file because generation validation deliberately
keeps a mixed installation inactive.

## Operation Rules

Unless a rule is marked as planned, it describes the implemented operational
API.

- `GetHistorySanitized` uses bounded pagination and returns stable codes plus
  presentation-safe labels, never the private history document.
- `ListProfiles` returns the sanitized public profile representation only.
- `GetDeviceState` returns lifecycle and safe-removal state, not device nodes or
  storage identifiers.
- `StartBackup`, `CancelBackup`, `EjectTarget`, and `ValidateTarget` re-check the
  selected profile and target after authorization; object paths or identifiers
  cannot stand in for authorization.
- Planned: `PrepareDevice` requires explicit destructive-operation confirmation
  in addition to polkit authorization and revalidates the selected block device
  immediately before modification.
- Authorization success is not persisted by the daemon. Only polkit controls
  any permitted caching, and administrative actions request non-keep actions.
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

The system API test target verifies unauthenticated reads, distinct action
identifiers and caller subjects, caller disappearance during an authorization
prompt, profile-version races, mismatched cancellation, malformed input and
manager restart. Inactive-session behavior and cross-action policy delegation
remain packaging/system integration concerns.
Planned administrative API tests must also prove that `SaveProfile` cannot add
or alter hooks with only the profile-save authorization.
