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

Before the effectful receive, run the identical `btrfs send` command into
`btrfs receive --dump`. Count that stream without publishing its bytes as
transferred data, then use that count as the expected total for the second,
effectful pass. The read-only snapshot and parent, identical arguments and
retained directory handles keep both stream lengths stable. The runtime calls
the value `bytesTotalEstimated`: it is exact for the completed sizing pass, but
the Btrfs interface does not provide a separate size query or promise that two
independently generated streams are byte-for-byte identical.

Use `libbtrfsutil` for supported metadata, snapshot and subvolume operations,
but do not implement the send-stream format in this project.

## Why `--no-data` Is Not The Progress Total

`BTRFS_IOC_SEND` supports `BTRFS_SEND_FLAG_NO_FILE_DATA`, exposed by
`btrfs send --no-data`. It generates an operation stream without file payloads.
Dumping that stream can identify affected paths and reports changed logical
ranges through `UPDATE_EXTENT` commands, so it may support a future change
preview.

The no-data stream cannot provide the denominator for the current byte
progress. It describes protocol operations rather than a final, unique file
list, and one path may participate in several extent, rename, link, clone or
metadata operations. More importantly, `UPDATE_EXTENT` lengths are logical
ranges. The effectful pipeline counts encoded send-stream bytes. Compression,
clone commands, sparse extents, extended attributes and protocol framing make
those quantities incomparable. Summing file sizes or `UPDATE_EXTENT` lengths
could therefore make progress finish far below or above 100 percent.

An implementation that prefers one source traversal can omit sizing and report
transferred bytes, speed and elapsed time without a percentage. Spooling the
complete stream would preserve an exact total while avoiding a second send,
but would require temporary space for the whole stream, write sensitive backup
data to an intermediate file and add another full write/read cycle.

## Alternatives

- Implement the Btrfs send-stream encoder and decoder in-process.
- Pipe a shell command such as `btrfs send | btrfs receive`.
- Spool every stream to a regular file before receive.
- Estimate the transfer from file sizes or a `btrfs send --no-data` stream.

## Consequences

- `btrfs-progs` remains a runtime dependency and controls stream compatibility.
- The project must test argument construction and supported tool versions.
- There is no shell interpolation surface.
- Cancellation and error reporting must account for two child processes.
- Every transfer runs the send producer twice, increasing source I/O and CPU
  work in exchange for exact total progress without spooling the stream.
- The sizing consumer parses the stream without creating a received subvolume.
