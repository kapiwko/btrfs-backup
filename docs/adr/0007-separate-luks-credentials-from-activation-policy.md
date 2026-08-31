# ADR 0007: Separate LUKS Credentials From Activation Policy

- Status: accepted
- Date: 2026-08-31

## Context

LUKS supports multiple keyslots, while the current profile activation object
describes only how the service attempts to unlock a target. A keyslot does not
itself identify whether its bytes came from a passphrase, a key file or another
enrollment mechanism. Treating one activation setting as the complete set of
credentials prevents safe rotation and encourages secret transport through
configuration JSON.

## Decision

Managed LUKS credentials and automatic activation policy are separate domain
concepts.

The LUKS header remains the authority for occupied keyslots. Application
metadata may label only credentials created or explicitly registered by the
application. Activation policy selects one managed mechanism for unattended
use or requires an interactive prompt; it does not claim ownership of every
keyslot.

Passphrases and key material never enter profile JSON, textual D-Bus arguments,
logs or history. Credential mutations require a dedicated privileged backend
and a bounded binary transport such as a Unix file descriptor. The backend
must prevent removal of the last usable credential and revalidate the target
identity immediately before every mutation.

## Alternatives

- Extend `target.activation` with a list of passphrases and key files.
- Let the KCM invoke `cryptsetup` directly.
- Infer credential type and ownership from keyslot numbers.

## Consequences

- Credential rotation does not require replacing a profile or target identity.
- Profile editing remains free of secret values.
- LUKS administration needs dedicated metadata, authorization, recovery and
  disposable-device tests before it can be exposed in the KCM.
- Unknown keyslots remain visible only as unowned credentials and are never
  assigned misleading labels.
