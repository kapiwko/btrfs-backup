# Security Policy

## Supported Versions

Security fixes are provided for the latest `4.0.x` release line. Older release
lines and unreleased development snapshots are not supported security branches.

| Version | Supported |
|---|---|
| `4.0.x` | Yes |
| `< 4.0` | No |

This table will be updated when a newer release line becomes supported.

## Reporting A Vulnerability

Do not open a public issue for a suspected vulnerability.

Use the repository's
[private vulnerability reporting form](https://github.com/kapiwko/btrfs-backup/security/advisories/new).
Include:

- affected version and commit, if known;
- required privileges and deployment assumptions;
- reproducible steps or a minimal proof of concept;
- impact on confidentiality, integrity, backup availability or restoreability;
- whether active configuration, keys or backup data may have been exposed;
- any suggested mitigation.

Remove passwords, passphrases, LUKS key material, private profile contents and
personal backup data from the report unless they are strictly necessary to
demonstrate the issue. If private vulnerability reporting is unavailable,
contact the repository owner through GitHub without disclosing vulnerability
details publicly and request a private reporting channel.

Maintainers aim to acknowledge a report within seven days and provide an
initial assessment within fourteen days. Complex storage or privilege-boundary
issues may require more time to reproduce safely. Please coordinate public
disclosure until a fix or mitigation is available.

## Scope

Security-sensitive areas include:

- privileged path traversal and symbolic-link handling;
- profile, hook, status and history permissions;
- target identity, LUKS, mount and eject behavior;
- snapshot deletion, retention, receive and interrupted-run recovery;
- process execution, cancellation and environment handling;
- D-Bus caller identity, polkit authorization and privileged audit records;
- package and release artifact integrity.

The implementation's trust boundaries and hardening requirements are described
in the [technical security model](docs/security.md). That document is design
documentation; this file defines how vulnerabilities are reported and which
versions receive fixes.
