# Consistency Groups

Status: proposed.

## Problem

The current run snapshots and transfers one source before moving to the next.
For a multi-source profile, snapshots can therefore represent very different
points in time. This is unsuitable when `/`, `/home` and application data must
form one logical recovery point.

## Proposed Model

A consistency group is an ordered set of source ids with its own id and capture
policy. A run captures every enabled member before it starts replication of any
member:

```text
prepare group
run before-group hooks
snapshot every source
persist capture checkpoint
run after-group hooks
replicate captured snapshots
```

Each successful barrier produces a `captureId`. Snapshot catalog and history
entries carry `captureId`, group id and capture time so restore can select a
complete group.

Profiles without groups retain current behavior. A source belongs to at most
one group in the first version; independent sources form implicit one-member
groups.

## Failure And Recovery

- A failed snapshot prevents replication of that group's new capture.
- Snapshots already created for an incomplete barrier are recorded in a group
  checkpoint and cleaned or adopted deterministically on the next run.
- The after-group hook is attempted whenever the before-group hook succeeded,
  including snapshot failure and cancellation.
- Hook timeout, cancellation and cleanup results remain structured errors.
- Previously committed captures are never pruned merely because a new barrier
  failed.

## Application Hooks

Group hooks are administrator-provided executable plus argument arrays. They
follow the existing trusted-file, finite-timeout and no-shell rules. Their
purpose is a bounded quiesce window; long backup transfers happen after the
application has resumed.

## Compatibility

The first schema migration should normalize every existing source into an
implicit group without changing snapshot paths, names or incremental parents.
The runtime must ignore no source silently: duplicate and unknown source ids in
explicit groups are validation errors.

## Open Questions

- whether group membership order is configuration-significant;
- whether partial group restore is allowed with an explicit warning;
- whether local retention operates on whole captures or individual sources;
- how consistency groups interact with captures imported from Snapper.
