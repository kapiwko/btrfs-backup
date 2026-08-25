# Scheduling And Request Queue

Status: proposed.

## Goals

Scheduling must work without a graphical session and must not create competing
runs. Local capture and target replication need independent triggers once those
operations are separated.

## Trigger Model

Requests carry a stable reason, requested time, profile id and desired work:

```text
device-connected
manual
scheduled
pre-upgrade
stale-backup
restore-drill
retry-after-failure
```

Reasons are recorded in status and history. A request may ask for capture,
replication, or both. Current manual and udev behavior maps to a combined
request until capture and replication are separated.

## systemd Ownership

Calendar scheduling uses generated systemd timer units. The project does not
run a permanent scheduler loop. Timers invoke the same request boundary as
udev and manual control, and therefore share locking, validation and history.

Timers remain useful when the system manager is unavailable. If a persistent
queue needs privileged arbitration, a short request-ingest command can durably
append work without requiring the long-lived manager to execute the backup.

## Queue Semantics

- Requests for the same profile are merged rather than run concurrently.
- A force request dominates an ordinary request but does not bypass safety
  validation.
- Multiple reasons are retained for diagnostics.
- Pending work survives reboot and target disconnect.
- Replication waits for an eligible target without blocking new local capture.
- Retry uses bounded backoff and never loops indefinitely on invalid config.
- Per-profile and per-target locks remain the final concurrency authority.

Queue records use durable, versioned JSON with an idempotency key. Completion
is acknowledged only after terminal state and history are durable.

## Power And Resource Policy

Optional policy can require AC power or minimum battery, inhibit sleep only for
bounded critical phases, and assign CPU/I/O weights through systemd. Critical
battery defers or cancels at a recoverable boundary; it must not abandon a
commit halfway through.

## Open Questions

- exact merge precedence among force, retry and scheduled reasons;
- queue ownership when the manager is absent;
- catch-up behavior after long downtime;
- timezone and daylight-saving representation;
- fairness across profiles sharing one target.
