# Repository Format

Status: proposed.

## Purpose

A backup target should explain what it contains without access to the original
host configuration. Metadata accelerates discovery and verification, but the
actual Btrfs subvolumes and their UUID relationships remain the storage truth.

## Layout

The first format should reserve a repository root such as:

```text
btrfs-backup/
  repository.json
  hosts/<hostId>/profiles/<profileId>/...
  catalog.json
  catalog.json.sig          optional
```

`repository.json` contains a repository id, format version, target filesystem
UUID, creation time and feature flags. It must not contain LUKS keys, profile
hook commands or other host secrets.

Catalog entries identify host, profile, source, snapshot, capture group, local
UUID, received UUID, parent UUID, creation time, engine version and verification
state. The catalog is rebuildable by inspecting Btrfs state.

## Version Policy

- Readers support the current format and an explicitly documented compatibility
  window.
- Writers emit only the current format.
- An unsupported newer format is rejected before any mutation.
- Upgrade is an explicit CLI operation with `--dry-run`.
- Connecting a medium never upgrades it implicitly.

Profile, repository, catalog, history and D-Bus versions are independent. A
single product version must not be used as their schema version.

## Mutation And Upgrade

Metadata updates use durable temporary-file, `fsync`, rename and directory
`fsync` semantics. A format upgrade writes a complete new metadata generation,
validates it against Btrfs state, then changes the active generation. Before
commit, rollback must leave the old generation readable.

No upgrade, repair or catalog rebuild deletes valid snapshots. Data deletion is
a separate retention or repair action with its own plan and authorization.

## Discovery And Repair

Discovery starts from an already mounted path and later may inspect block
devices. Verification compares metadata with readonly state, UUID chains,
`.incoming` content and free space. Repair is plan-first and distinguishes safe
metadata rebuild from destructive cleanup or full reseed.

## Open Questions

- exact repository root and namespace encoding;
- catalog sharding versus one bounded document;
- compatibility-window length;
- signature format and key ownership;
- atomic generation selection on read-only or nearly full targets.
