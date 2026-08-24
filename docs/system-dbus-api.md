# System D-Bus Authorization Contract

## Security Boundary

The future system manager is a privileged service and its system D-Bus API is
a security boundary. It must use a versioned interface such as
`io.github.btrfsbackup.Manager1` and default-deny bus policy. The service must
obtain the caller identity from the D-Bus connection and polkit subject; it
must never trust a UID, PID, user name, or authorization result supplied as a
method argument.

Read methods expose only the same presentation-safe information that is
currently public. They must not return paths, UUIDs, device nodes, hook
commands, private diagnostics, or unsanitized history details.

## Method Classes

| Method | Authorization | Polkit action |
|---|---|---|
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

The operational actions should use these defaults:

```xml
<defaults>
  <allow_any>no</allow_any>
  <allow_inactive>auth_admin</allow_inactive>
  <allow_active>auth_admin_keep</allow_active>
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
operation without implicitly delegating the others. The daemon must authorize
every call, including calls from the active graphical session.

## Profile And Hook Writes

`SaveProfile` must parse and fully validate canonical profile data before it
requests authorization. System paths are application configuration and are not
accepted in profile input.

The manager must compare the validated hook set with the currently stored hook
set. A save that adds, removes, reorders, or changes a hook program, argument,
or timeout requires both `io.github.btrfsbackup.save-profile` and
`io.github.btrfsbackup.change-hooks`. The same rule applies when creating a new
profile containing hooks. An empty or omitted hook list must not erase existing
hooks unless the hook-change authorization was granted.

`ChangeHooks` is a separate high-risk method for clients that edit hooks
directly. It is not an alternative path around `SaveProfile`: both methods use
the same validation, trusted hook directory restrictions, atomic persistence,
and audit event generation. Authorization is checked immediately before the
commit and is bound to the calling D-Bus connection.

`SaveProfile` exposes `configuration.save_failed` when the transaction fails
and the previous configuration is restored. If any rollback operation fails,
it exposes `configuration.rollback_incomplete` together with the primary save
failure and per-artifact rollback diagnostics. Clients must present the latter
as an operator-actionable failure; they must not retry automatically or infer
success from any individual file because generation validation deliberately
keeps a mixed installation inactive.

## Operation Rules

- `GetHistorySanitized` uses bounded pagination and returns stable codes plus
  presentation-safe labels, never the private history document.
- `ListProfiles` returns the sanitized public profile representation only.
- `GetDeviceState` returns lifecycle and safe-removal state, not device nodes or
  storage identifiers.
- `StartBackup`, `CancelBackup`, `EjectTarget`, and `ValidateTarget` re-check the
  selected profile and target after authorization; object paths or identifiers
  cannot stand in for authorization.
- `PrepareDevice` requires explicit destructive-operation confirmation in
  addition to polkit authorization and revalidates the selected block device
  immediately before modification.
- Authorization success is not persisted by the daemon. Only polkit controls
  any permitted caching, and administrative actions request non-keep actions.
- Mutating methods are serialized with the existing profile and target locks
  and emit structured audit records without secrets or private diagnostics.

## Required Tests

The system API test target must verify unauthenticated reads, denial of every
mutating method without its exact action, cross-action denial, inactive-session
behavior, cancellation and disconnect races, caller disappearance during an
authorization prompt, malformed and oversized messages, and manager restart.
It must also prove that `SaveProfile` cannot add or alter hooks with only the
profile-save authorization.
