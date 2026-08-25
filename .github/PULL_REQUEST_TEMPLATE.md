<!-- Title: <type>(<optional-scope>): <imperative lowercase description> -->

## What changed?

<!-- Describe the observable change and the owning component. -->

## Why?

<!-- Link the issue, sprint item, roadmap direction, design, or ADR. -->

## Verification

<!-- List exact commands and results. State what could not be tested and why. -->

- [ ] Focused tests pass.
- [ ] `git diff --check` passes.
- [ ] Documentation and examples are updated where required.

## Compatibility

- [ ] Profile, status, history, checkpoint, CLI and package contracts are unchanged.
- [ ] Any intentional contract change has a documented, tested migration.
- [ ] Not applicable; this change does not affect a compatibility boundary.

## Security

- [ ] Privileged inputs, identity, authorization, paths, ownership and active state are revalidated at the performing boundary.
- [ ] Diagnostics and public status do not expose secrets or private recovery data.
- [ ] New external commands use separate program/argument values without a shell.
- [ ] Not applicable; explain why below.

Security notes:

<!-- Describe trust-boundary changes, threat cases, or why security is unaffected. -->

## Failure And Recovery

- [ ] Failure before snapshot creation is covered.
- [ ] Interruption during transfer or cleanup is covered.
- [ ] Failure before and after commit is recoverable and tested.
- [ ] Cancellation cannot affect unrelated processes, profiles or targets.
- [ ] Not applicable; explain why below.

Recovery notes:

<!-- Describe retained state, retry behavior, cleanup, and restore evidence. -->
