# Contributing To btrfs-backup

Thank you for helping improve `btrfs-backup`. This is a privileged backup tool,
so changes are reviewed first for recoverability, compatibility and filesystem
safety, then for convenience.

Use GitHub Issues for reproducible bugs and scoped feature proposals. Report a
potential vulnerability privately as described in [SECURITY.md](SECURITY.md).

## Development Setup

The primary development platform is Arch Linux. A source build needs CMake, a
C++23 compiler, `pkg-config`, `nlohmann-json`, and development files for
`libmount`, `libblkid`, `libudev` and `libbtrfsutil`. On Arch Linux:

```bash
sudo pacman -S --needed \
  base-devel \
  btrfs-progs \
  clang \
  cmake \
  nlohmann-json \
  pkgconf \
  systemd \
  util-linux
```

The optional Plasma integration additionally needs Qt 6 QML/Quick, Extra CMake
Modules, Kirigami, KPackage, KI18n and libplasma development packages.

Configure and build the base project with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Enable the optional KDE integration explicitly:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_KDE_INTEGRATION=ON
cmake --build build --target btrfs-backup-kde --parallel
```

## Architecture

Start with these documents before changing runtime behavior:

- [architecture and runtime flow](docs/architecture.md);
- [C++ component ownership](docs/cpp-layout.md);
- [C++ readability and file organization](docs/cpp-readability.md);
- [engine compatibility contract](docs/engine-contract.md);
- [security model](docs/security.md);
- [configuration contract](docs/configuration.md);
- [testing strategy](docs/testing.md).
- [release notes and publication](docs/releasing.md).

Current sprint status, including the absence of an active sprint, is in
[TODO.md](TODO.md). Long-term direction is in [ROADMAP.md](ROADMAP.md), proposed
designs are under `docs/design/`, and accepted architectural decisions are
under `docs/adr/`.

Keep CLI entry points thin. Reusable behavior belongs in the owning domain
under `src/`. The base runtime must not acquire a KDE, QML or graphical-session
dependency. Invoke external programs with a program and argument vector, never
through a shell command string.

## Testing

Run the default suite for ordinary changes:

```bash
./tests/run-tests.sh
```

Run a focused CTest while iterating:

```bash
ctest --test-dir build --output-on-failure -R '<test-name>'
```

The non-root static/rendering mode is:

```bash
./tests/run-tests.sh --static-only
```

Storage, packaging, systemd or recovery changes may require the opt-in real
Btrfs test:

```bash
cmake --build build --target real-btrfs-integration
```

That test uses a privileged container with disposable loop devices. Do not run
destructive Btrfs, LUKS, mount, package-installation or service-control commands
against host configuration or a real backup target as part of a test.

## Change Requirements

- Add focused tests for new behavior and failure paths.
- Preserve profile, status, history and checkpoint compatibility, or provide a
  documented and tested migration.
- For storage changes, cover interruption before and after snapshot commit and
  explain how the next run recovers.
- For privileged changes, revalidate identity, authorization, paths, ownership
  and active state at the boundary that performs the operation.
- Update schemas, examples, validators and contract tests together.
- Update installation templates, package inventories and documentation when
  installed files or commands change.
- Keep code warning-clean under `-Wall -Wextra -Wpedantic`, format modified C++
  lines with the repository `.clang-format`, and keep production code clean
  under the repository `.clang-tidy` baseline.
- Add dependencies only when they have a clear owner and reduce more risk or
  complexity than they introduce.

## Commit Messages

New commits and pull-request titles follow Conventional Commits:

```text
<type>(<scope>): <description>
```

The scope is optional. Allowed types are:

```text
feat fix refactor perf test docs build ci chore style revert
```

Use stable scopes when they clarify ownership:

```text
backup config state platform cli kde dbus systemd udev packaging cmake ci docs
```

Examples:

```text
feat(backup): add repository verification
fix(config): reject symlinked profile paths
refactor(platform): split POSIX process handling
test(backup): cover cancellation during receive
docs(recovery): document full restore workflow
```

Write the description in English, use the imperative mood, start lowercase
after the colon, omit the trailing period, and keep the complete subject at or
below 72 characters when practical. Do not invent a multi-component scope; omit
the scope when a coherent change genuinely crosses components.

Mark an incompatible public change with `!` and explain the migration in a
`BREAKING CHANGE:` footer:

```text
feat(config)!: replace legacy retention fields

BREAKING CHANGE: profiles using the legacy fields must be migrated to the
retention policy object.
```

Use `feat` for new public behavior, not merely for adding a class. Use
`refactor` only when observable behavior is unchanged. Security fixes retain
their semantic type, normally `fix`; there is no separate `security` type.

Each commit must represent one reviewable and revertible logical change and
leave the branch buildable and tested. Separate a mechanical move or pure
refactor from a behavioral fix when practical. Tests and documentation directly
required by a feature or fix belong in the same commit. A test-only expansion
may use `test`, and a documentation-only change may use `docs`.

For a commit dominated by file moves, first verify that the worktree contains
only the intended logical change, then stage the complete change and inspect
rename detection explicitly:

```bash
git add -A
git diff --cached --find-renames=20% --name-status
```

Review unexpected additions or deletions before committing. The staged diff is
the authoritative view because Git can detect a rename only when both the old
path deletion and the new path addition are present in the compared snapshots.

Use a commit body when the subject cannot explain why the change is safe. Bodies
are particularly useful for security boundaries, recovery ordering,
concurrency, schema changes and transactional persistence. Explain the reason
and consequences rather than restating the diff.

Squash merge is the default for ordinary pull requests. The pull-request title
therefore follows the same convention and becomes the final commit subject.
Atomic commit series may be retained when every commit is independently useful,
buildable and tested. Existing history predating this policy is not rewritten.

## C++23 Style

Follow the established formatting in the component being changed. The
repository `.clang-format` captures that style, but its adoption deliberately
does not reformat untouched code. CI checks only C++ lines changed against the
pull-request base. Format those lines locally with:

```bash
git clang-format HEAD -- apps src tests integrations/kde
```

The repository `.clang-tidy` is a warning-as-error baseline for production code
under `apps/` and `src/`. Run both enforced checks with:

```bash
make quality
```

`make check-format` checks uncommitted lines by default and accepts an optional
base revision through `./tools/check-cpp-format.sh <revision>`. Do not combine a
functional change with an unrelated repository-wide reformat.

Naming conventions are:

- types: `PascalCase`;
- functions and methods: `snake_case`;
- variables: `snake_case`;
- private data members: trailing underscore;

### C++ filenames

The complete rules for primary abstractions, ownership, lifecycle methods, and
high-level flow are in [C++ readability and file organization](docs/cpp-readability.md).

C++ source and header filenames use `PascalCase`.

A file containing one primary class, struct, enum or interface must be named
exactly after that type:

```text
RunExecutionContext.hpp
RunExecutionContext.cpp
BackupService.hpp
ICommandRunner.hpp
PosixCommandRunner.hpp
```

A file containing a small, cohesive group of closely related value types or
free functions uses a precise `PascalCase` concept name, for example
`Identifiers.hpp`, `Errors.hpp`, `BackupRunSerialization.hpp` or
`ApplicationPaths.hpp`.

Do not introduce a class solely to justify a filename. Entry points named
`main.cpp`, generated files and conventional build-system filenames are exempt.
Keep one primary abstraction per file; closely related supporting enums, result
types and small value types may remain beside it when splitting them would
reduce locality.

Whenever you add a class, decide explicitly whether it should have its own
files. Classes with state, lifecycle or RAII ownership, independent invariants,
a reusable contract, or focused tests use `ClassName.hpp` and, when needed,
`ClassName.cpp` by default. Keep a class local only when it is a small,
non-growing implementation detail of the file's primary abstraction.

List source files, headers, and targets alphabetically within their logical
CMake section. Preserve meaningful `PUBLIC`, `PRIVATE`, and `INTERFACE`
grouping; do not reorder entries across visibility boundaries merely to obtain
one global sort order.

Use C++23 features when they make ownership and domain invariants clearer:

- own file descriptors, processes, locks, temporary files and other resources
  through RAII; avoid raw `new`, `delete` and unowned handles;
- use `enum class`, `std::optional`, `std::variant` and strong identifiers when
  they prevent invalid or ambiguous states;
- use `std::chrono` durations for timeouts and intervals and `std::filesystem`
  for paths;
- mark results `[[nodiscard]]` when ignoring them is almost certainly an error;
- prefer named option structures or domain enums over positional boolean
  arguments;
- use `final`, `override` and single-argument `explicit` constructors where
  their meaning applies;
- use `const` and `auto` when they improve comprehension, not mechanically;
- consider `std::jthread` and `std::stop_token` only when they fit the existing
  cancellation model.

Do not create a strong type for every string or an enum for every boolean. Add
types where they enforce a real invariant. Predictable outcomes such as busy,
cancelled, skipped or recovery-required should use typed results when possible;
exceptions remain appropriate at infrastructure boundaries and for invariant
violations.

The existing `ICommandRunner`, `IFileSystem` and similar `I`-prefixed ports are
accepted project style. Do not rename interfaces solely for cosmetic
consistency; reconsider naming only as part of a substantive ownership change.

A source file normally includes its own header first, followed by standard or
third-party headers and then project headers. Headers include what they use and
must not rely on transitive includes. Keep one main concept per file, but do not
split closely related value types solely to satisfy a line-count rule.

Comments explain why an ordering, syscall or recovery rule exists. They should
not narrate obvious statements. In particular, document non-obvious Btrfs UUID
semantics, durable-write ordering, signal/process lifetime and recovery
invariants.

New source, build and script files must carry an SPDX header identifying the
actual copyright holder:

<!-- REUSE-IgnoreStart -->

```text
SPDX-FileCopyrightText: YEAR Copyright Holder <contact@example.com>
SPDX-License-Identifier: GPL-3.0-or-later
```

<!-- REUSE-IgnoreEnd -->

Use the year in which the contribution was first published and the name and
contact details of its copyright holder. Preserve existing copyright notices;
add another `SPDX-FileCopyrightText` line when a contribution has a different
copyright holder. Use the comment syntax appropriate for the file and keep an
interpreter shebang on the first line. Files that cannot safely contain
comments must be covered by `REUSE.toml`. Run `reuse lint` when changing
licensing metadata or adding a new file.

Test names should state the behavior or safety property under test. Prefer
names such as `restores_previous_generation_when_activation_fails` over generic
sequence labels such as `test_case_17`.

## Pull Requests

Keep each pull request focused on one coherent change. The description must
explain the problem, behavior change, tests run and any unverified environment.
Complete the security and recovery prompts in the pull-request template even
when the answer is "not applicable".

Do not include generated `build/` or `dist/` output. Before submitting, run:

```bash
git diff --check
make quality
./tests/run-tests.sh
```

Run the heavier checks required by the affected boundary and report their exact
results rather than stating only that the change was tested.
