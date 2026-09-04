# Release Notes And Publication

## Sources Of Truth

`VERSION` is the authoritative package version. `CHANGELOG.md` is the
authoritative source for release-note content. GitHub's generated notes are not
the release description because they can omit direct commits and changes that
were not merged through pull requests.

Every release uses `MAJOR.MINOR.PATCH`, an annotated `vMAJOR.MINOR.PATCH` tag
and a matching dated changelog section.

## Changelog Template

Keep `Unreleased` at the top. Move completed entries into a dated release
section using this structure:

```markdown
## Unreleased

## X.Y.Z - YYYY-MM-DD

### Highlights

1. describe the primary user-visible outcome;
2. describe another important outcome.

### Upgrade Notes

1. state the exact migration command and when it is needed;
2. identify removed, deprecated or temporarily supported configuration.

### Component Or Workflow Name

1. group related user-visible behavior;
2. explain consequences rather than internal implementation details.

### Release And Tooling

1. include packaging or tooling changes that affect users, maintainers or
   artifact integrity.
```

Use `Highlights` when a release contains several unrelated headline changes.
When one theme dominates the release, replace it with a concrete name such as
`Table-Free Target Management`. `Upgrade Notes` follows the primary section
whenever installation, configuration, schemas, compatibility or required
operator action changes. Use additional thematic sections for meaningful
groups such as backup lifecycle, Plasma integration or security.

Do not copy raw commit subjects into the changelog. Combine related commits
into outcomes, use present tense, name commands and configuration fields
exactly, and exclude purely internal refactoring unless it changes maintenance,
packaging or verification expectations.

## Rendering GitHub Notes

Render the released changelog section instead of writing a second independent
description:

```bash
python3 tools/render_release_notes.py X.Y.Z vPREVIOUS > build/release-notes-X.Y.Z.md
```

For the first release, omit `vPREVIOUS`; the footer then links to the tagged
source tree instead of a comparison that cannot exist.

The renderer wraps changelog subsections in `What's New`, then appends the
standard artifact-verification paragraph and a comparison or tagged-source
link. Review the rendered file before publication. It must contain:

1. the standard `What's New` heading;
2. every thematic subsection from the released changelog entry;
3. actionable upgrade instructions when applicable;
4. the standard `Artifacts` section;
5. a comparison link from the previous release tag, or a tagged source link
   for the first release.

Do not use `gh release create --generate-notes` as the release body. GitHub may
generate a pull-request supplement, but it must not replace the rendered
changelog.

## Publication Checklist

1. Update `VERSION`, `CHANGELOG.md`, supported versions and versioned examples.
2. Commit the release metadata and run the required quality and test gates.
3. Build the complete artifact set with `tools/build-release.sh --target all`.
4. Verify `SHA256SUMS` from inside the artifact directory.
5. Create and push the annotated version tag.
6. Render notes from the tagged changelog section.
7. Create the GitHub Release with `--notes-file`, attaching packages,
   `SHA256SUMS` and `BUILD-REPORT.txt`.
8. Read the published body back with `gh release view` and verify every asset.

Example publication commands:

```bash
git tag -a vX.Y.Z -m "btrfs-backup X.Y.Z"
git push origin vX.Y.Z
python3 tools/render_release_notes.py X.Y.Z vPREVIOUS > build/release-notes-X.Y.Z.md
gh release create vX.Y.Z dist/* \
    --verify-tag \
    --title "btrfs-backup X.Y.Z" \
    --notes-file build/release-notes-X.Y.Z.md
```
