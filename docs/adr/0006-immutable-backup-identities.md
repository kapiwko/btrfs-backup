# ADR 0006: Keep Target And Source Identities Immutable

- Status: accepted
- Date: 2026-08-31

## Context

A profile binds an encrypted target and each source identifier to persistent
state outside the profile document. Target identity controls udev matching,
systemd activation, locking and repository validation. A source identifier
controls local snapshot paths, pending recovery, history, incremental chains
and the target repository directory.

Presenting these values as ordinary editable fields makes a small UI change
look harmless while it can actually orphan state or attach existing state to a
different device or subvolume.

## Decision

The target device identity and a source's subvolume binding are immutable after
creation.

The KCM exposes target identity, source identifiers and generated storage paths
as read-only technical details. Replacing a target requires deleting the
profile and creating a new one. Replacing a source subvolume requires removing
that source and adding a new source with a newly generated identifier.

Display names, retention, enablement and other explicitly supported behavioral
settings remain mutable through narrow domain operations. Removing a profile or
source does not implicitly delete snapshots, history or recovery state.

## Alternatives

- Allow direct edits and attempt to migrate every dependent path and state file.
- Treat identifiers as labels and discover relationships from directory names.
- Keep the JSON editor as an unsupported advanced mode.

## Consequences

- Administrative APIs can validate a small, explicit patch instead of accepting
  arbitrary profile JSON.
- Device and source replacement are deliberate workflows with clear data
  retention semantics.
- CLI access to technical configuration does not weaken runtime identity
  validation.
- A future migration feature must be designed as a transactional operation,
  not as a field edit.
