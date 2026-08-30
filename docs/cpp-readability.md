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
