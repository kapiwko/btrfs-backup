# Remote Transport

Status: proposed and deferred until repository and restore semantics stabilize.

## Boundary

Backup orchestration should depend on a transport capable of receiving,
verifying, committing, listing and pruning repository snapshots. The current
mounted local Btrfs target is the reference implementation. Transport does not
own capture planning, retention policy, run status or profile parsing.

The boundary must preserve the same transaction states as local receive:

```text
begin incoming
stream
verify identity and readonly state
commit
catalog
prune through an explicit policy action
```

## SSH Transport

The first remote implementation should use a constrained, versioned helper.
Arguments and messages are structured; neither side constructs an arbitrary
shell command. The helper confines all paths to one configured repository and
validates repository identity before accepting a stream.

Protocol negotiation covers helper version, repository format, send-stream
features, available space and optional operations. Loss of the connection
leaves an identifiable incoming transaction that verification or recovery can
inspect later.

## Append-Only Credentials

A source credential may create and inspect new backup transactions but cannot
delete committed history or change retention. Pruning runs under separate
server-side authority, for example a timer or administrator credential. This
limits damage after compromise of the source host.

## Security Requirements

- host key verification is mandatory;
- no user-provided repository path reaches a shell;
- repository paths are resolved beneath a pinned root;
- helper requests and responses have size and timeout limits;
- destructive operations require separate capability and authorization;
- logs omit stream data, keys and sensitive profile fields;
- a retry cannot commit the same logical transaction twice.

## Deferred Transports

Mounted unencrypted local targets and stream archives may reuse parts of the
boundary later. Cloud object storage is not assumed to have Btrfs semantics and
would require a separate repository design rather than pretending to be a
mounted Btrfs target.

## Open Questions

- framing and authentication of the helper protocol;
- reconnect/resume versus restart of a send stream;
- server-side parent selection and catalog authority;
- bandwidth limiting and concurrency ownership;
- deployment and upgrade compatibility of the remote helper.
