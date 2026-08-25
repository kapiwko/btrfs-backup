# ADR 0001: Keep Btrfs Send And Receive As External Processes

- Status: accepted
- Date: 2026-08-25

## Context

The backup engine needs full and incremental Btrfs replication. The kernel send
stream is produced and consumed by mature `btrfs-progs` tools. Implementing or
embedding that protocol would add a large compatibility and data-integrity
surface unrelated to the project's primary value.

The runtime still needs bounded streaming, progress byte counts, cancellation,
diagnostics and deterministic child cleanup without invoking a shell.

## Decision

Run `btrfs send` and `btrfs receive` as separate external processes with program
and argument vectors. Connect them through the native bounded pipeline, count
transferred bytes, and control both process groups through the existing
cancellation and escalation policy.

Use `libbtrfsutil` for supported metadata, snapshot and subvolume operations,
but do not implement the send-stream format in this project.

## Alternatives

- Implement the Btrfs send-stream encoder and decoder in-process.
- Pipe a shell command such as `btrfs send | btrfs receive`.
- Spool every stream to a regular file before receive.

## Consequences

- `btrfs-progs` remains a runtime dependency and controls stream compatibility.
- The project must test argument construction and supported tool versions.
- There is no shell interpolation surface.
- Cancellation and error reporting must account for two child processes.
- Exact total progress is unavailable unless estimated separately or the stream
  is spooled, but transferred bytes remain exact.
