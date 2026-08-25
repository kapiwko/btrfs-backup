# ADR 0004: Keep The Runner Independent Of The System Manager

- Status: accepted
- Date: 2026-08-25

## Context

A system D-Bus manager is useful for status, authorization and desktop control,
but removable-media backup must also run before login and survive desktop or
manager failure. Making the manager the execution host would turn an optional
control plane into a single point of failure for data protection.

## Decision

The systemd profile service executes a standalone runner. udev can start that
service directly. The future `btrfs-backupd` process is an outer adapter that
observes durable state and invokes existing use cases or systemd units; it is
not a parent process or required communication channel for the runner.

The runner owns its transaction, cancellation token, status and history writes.
The manager reconstructs presentation state after restart and exposes it to
clients without changing the result of an active run.

## Alternatives

- Execute all backups inside a long-running D-Bus daemon.
- Require the manager to spawn every runner directly.
- Use a user-session service as the primary runtime coordinator.

## Consequences

- Automatic backup works without D-Bus activation, KDE or a logged-in user.
- Manager restart cannot cancel an active backup by loss of ownership.
- Durable status/history and systemd unit state form the recovery boundary.
- Live signals may be delayed or reconstructed after manager downtime.
- Control methods must verify unit and run identity server-side to avoid acting
  on stale reconstructed state.
