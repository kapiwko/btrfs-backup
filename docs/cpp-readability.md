# C++ Readability And File Organization

This document defines how native code should expose ownership, lifecycle, and
the order of an operation. It complements the component boundaries in
[`cpp-layout.md`](cpp-layout.md); it does not replace domain design or safety
requirements.

## Primary Abstraction

Each maintained C++ file has one readily identifiable primary abstraction: a
class, operation, service, value-type group, or cohesive free-function module.
The `PascalCase` filename names that abstraction. A primary type uses its exact
type name; cohesive modules use a precise concept name such as
`BackupRunSerialization`.

Small enums, result structures, and value types may remain next to the
abstraction whose contract they describe. Do not split them merely to satisfy a
count, and do not introduce a class solely to justify a filename. Conversely, a
type deserves its own file when it has independent state, lifecycle,
invariants, cleanup, or tests.

Whenever a class is added, explicitly decide whether it needs its own
`ClassName.hpp` and, when non-trivial, `ClassName.cpp`. A class with state, an
RAII responsibility, an independently useful contract, non-trivial invariants,
or likely independent tests belongs in separate files by default. A class may
remain local only when it is a small implementation detail of the file's
primary abstraction, has no reusable contract, and is unlikely to grow. Record
that decision in review instead of adding a second primary abstraction by
accident.

Conventional entry points named `main.cpp`, generated sources, and names
required by a framework or build system are exceptions. Directories remain
lowercase and continue to describe domain ownership.

## High-Level Flow

An orchestration method should read as the ordered history of the operation.
Use private methods named after domain or lifecycle stages when POSIX calls,
JSON mapping, D-Bus callbacks, rollback mechanics, or detailed diagnostics
would otherwise obscure that history.

Keep one level of abstraction in a function. This is a review criterion, not a
line limit. A longer function may be correct when the explicit order is a
safety property; a short function is not automatically readable when generic
callbacks hide what it does.

Anonymous namespaces contain small stateless helpers and C API trampolines.
They must not hide a stateful module, lifecycle-owning type, or most of the
file's business logic before the named abstraction appears.

## Ownership And Lifecycle

Ownership must be visible in types and signatures:

- pass `std::unique_ptr<T>` by value when ownership is transferred;
- use references for required borrowing and pointers only for optional
  borrowing;
- do not expose `std::unique_ptr<T>&` as a public ownership protocol;
- use `std::shared_ptr` only when multiple asynchronous owners genuinely keep
  the same object alive;
- model absence with `std::optional` only when it is a valid non-error state.

Classes owning processes, descriptors, locks, threads, mounted sessions, or
transactional files provide an explicit, idempotent close/finish operation when
callers need cleanup diagnostics or ordering. Destructors are non-throwing
fallbacks. Critical cleanup is performed explicitly while its dependencies are
still alive, and partial failures preserve all useful diagnostics.

Prefer the rule of zero. An explicit destructor, copy operation, or move
operation is a reviewed lifecycle decision and must be present in the inventory
in `cpp_special_member_tests.cmake`. A defined move operation and every
non-default destructor are declared `noexcept`; deleted move operations need no
exception specification. The inventory is deliberately not inferred from a
base class: adding a resource owner or making a type immobile requires an
explicit review.

Do not replace differently named cleanup steps with a generic callback helper
when the steps have different ordering, failure consequences, or recovery
meaning.

## Domain Values

Use nominal types for identifiers, UUIDs, fingerprints, paths, and operation
states once data crosses from an input DTO into domain code. Normalize UUIDs
and other canonical values in their constructors. Raw strings belong at JSON,
D-Bus, CLI, and configuration input boundaries; codecs perform the conversion
to and from domain values.

Use `RuntimeTimePoint` and `std::chrono` durations inside the application.
Formatting and parsing timestamps is a serialization concern. Represent
mutually exclusive states with `std::variant` and state-specific structures so
invalid combinations cannot be created through the public API.

`OperationPathValue` remains a protected implementation base for the nominal
operation paths. Replacing it with member composition alone would retain the
declaration macro and duplicate forwarding without strengthening invariants or
the public API. Reconsider it only with a macro-free nominal template or
another design that preserves the separately named validating constructors.

## Errors

Choose the error channel from the caller's required control flow:

| Situation | Contract |
| --- | --- |
| A synchronous operation cannot complete | Throw `BtrfsBackupError` or a more specific coded exception. |
| A value is legitimately absent | Return `std::optional<T>`. |
| The caller must handle one of several expected outcomes | Return a named `std::variant`. |
| An expected failure must be inspected directly | Use `std::expected<T, E>` for new, coherent boundaries; do not migrate isolated functions mechanically. |
| Cleanup must preserve diagnostics without throwing | Return a named close/cleanup result and keep the destructor as a `noexcept` fallback. |

Exceptions must not cross C callbacks or D-Bus callbacks. Boundary adapters
catch them and map them to the protocol's error representation.

Every C-facing trampoline is `noexcept`, performs no business logic, and
delegates immediately to a named owner. The boundary catches coded application
errors, standard exceptions, and `...`; the last case maps to a stable internal
error without exposing private diagnostics. Logging and audit attempts inside
the catch path must themselves be contained so they cannot break the boundary.

The systemd unit-control port is the reference `std::expected<void,
SystemdJobError>` boundary: accepting a job is success, while a rejected job is
an expected typed failure inspected by the operational backend. Higher-level
manager operations still translate that failure into their established coded
exception contract.

## C++23 Library Use

Prefer standard-library facilities when they state the existing intent more
directly:

- use `std::print` and `std::println` for formatted CLI output, including their
  `std::ostream&` overloads where tests inject a stream;
- use `string::contains`, `string_view::contains`, and
  `std::ranges::contains` for presence checks that do not need a position;
- use `std::to_underlying` when an enum's numeric representation is genuinely
  required, primarily for diagnostics, indexing, or protocol conversion;
- use `std::expected` for a complete boundary whose expected failure is part of
  the caller's control flow, not as a local replacement for one thrown call.

Monadic `optional` and `expected` operations are appropriate for a short linear
pipeline. Keep guard clauses when several independently named validations must
remain visible, as in timestamp parsing. Use `std::ranges::to` only when a view
is already the clearest expression of a pure transformation; a loop remains
preferable when it reserves capacity, validates elements, preserves partial
diagnostics, or performs ordered effects.

Use `std::move_only_function` only when the callback contract itself transfers
or retains move-only state. Current callbacks are copyable borrowed policies,
so converting them would add a restriction without expressing real ownership.
`std::stacktrace` is not part of normal user-visible errors. It may be added as
an opt-in, build-probed diagnostic for private logs after its runtime and
toolchain cost is measured; stack frames must never cross the public D-Bus or
status boundary.

## Boundaries

Keep system details in their adapters:

- POSIX, Btrfs, systemd, and file-descriptor mechanics stay under
  `platform/linux`;
- JSON encoding and decoding stay in named codec or serialization modules;
- CLI presenters map typed results to exact output and exit codes;
- D-Bus C callbacks catch every exception before returning across the C
  boundary;
- application services depend on ports and domain values, not adapter details.

Composition roots may expose a production overload that constructs system
adapters. The testable application overload takes required dependencies by
reference. In particular, `TargetService` has one-argument production
overloads, while its injected overloads require `TargetServiceDependencies&`;
nullable dependency pointers do not enter the service logic.

The runner is the reference composition shape: parse adapter options, construct
one object that owns dependencies in destruction order, then pass only the
application service to command logic.

```cpp
const RunnerOptions options = parse_runner_options(args);
RunnerComposition composition(config_root, options, cancellation);
return run_with_service(options, output, composition.service());
```

Do not construct Linux, JSON, or file-backed adapters inside `BackupService` or
hide production/test selection behind nullable dependencies.

Avoid generic `utils` modules. Extract shared code only when it represents a
specific concept with a stable invariant and at least two real consumers, or
when one safety-critical invariant requires a single implementation.

## Tests And Refactoring

Separate mechanical moves from behavioral changes. Before restructuring a
security-sensitive lifecycle, lock down observable ordering, cancellation,
partial failure, cleanup, and diagnostic behavior with focused tests.

Test files should group one subject and one coherent behavior area. Shared
fixtures provide a valid default scenario and expose domain-named modifiers;
they do not reproduce production logic or hide important inputs. Keep a fake
local until at least two test files genuinely need it.

There is no enforced maximum file length, function length, anonymous-namespace
length, or class count. These are review signals only. Architecture tests are
reserved for objective rules such as filenames, namespaces, dependency
direction, header ownership, ownership signatures, and the reviewed inventory
of explicit special members. Clang-tidy additionally rejects member functions
that should be `const` and defined destructors or move constructors missing
`noexcept`.
