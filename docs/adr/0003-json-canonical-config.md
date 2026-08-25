# ADR 0003: Use JSON As Canonical Profile Configuration

- Status: accepted
- Date: 2026-08-25

## Context

The project previously had multiple representations of profile and source
configuration. Multiple writable formats create drift, duplicate validation
and make CLI, runtime and future D-Bus/KDE clients disagree about defaults and
schema evolution.

Configuration contains privileged paths and hook definitions, so it must be
treated as validated data rather than executable shell input.

## Decision

Use one versioned JSON profile document per profile as the canonical runtime
configuration. Parse, normalize and semantically validate it through the shared
C++ configuration model. Reject unknown fields unless a future schema defines
an explicit extension mechanism.

Publish profile and derived installation artifacts transactionally with trusted
ownership, restrictive modes, durable file writes and rollback diagnostics.
Never source profile data as shell code.

## Alternatives

- Keep `.env` and per-source `.conf` files as equivalent writable inputs.
- Use a systemd environment file as the canonical runtime representation.
- Store profiles only in a daemon-owned database.

## Consequences

- Schema versions and migrations are explicit compatibility contracts.
- CLI, runner and future manager reuse one model and validator.
- Installed systemd/udev artifacts are derived state and may be regenerated.
- Profile writes need atomic multi-artifact publication and rollback handling.
- User-facing clients cannot bypass privileged persistence by editing derived
  files.
