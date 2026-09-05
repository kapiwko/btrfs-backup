# Development Workflow

CMake targets and presets are the source of truth for building and testing
native code. Python entry points orchestrate disposable containers, QEMU,
screenshots and release artifacts; they do not form part of the installed
backup runtime.

## Everyday Development

Configure, build and test the base project with GCC:

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug --parallel
ctest --preset gcc-debug --parallel
```

Use a focused CTest while iterating:

```bash
ctest --test-dir build/gcc-debug --output-on-failure -R '<test-name>'
```

The optional Plasma integration has its own preset and test selection:

```bash
cmake --preset kde-debug
cmake --build --preset kde-debug --parallel
ctest --preset kde --parallel
```

## Architecture And Quality Gates

Run the complete architecture contract with Clang:

```bash
cmake --preset architecture
ctest --preset architecture --parallel
```

This contract checks public headers, component dependencies and build side
channels. In particular, test executables must be owned by CMake, native code
must not be compiled ad hoc, and Python subprocesses must not use
`shell=True`.

Run formatting and clang-tidy checks before committing:

```bash
make quality
```

For faster feedback while editing tracked C++ implementation files, use
`make quality-changed`. Compiler warnings and sanitizer configurations remain
normal CMake workflows:

```bash
cmake --preset strict-warnings
cmake --build --preset strict-warnings --parallel
ctest --preset strict-warnings --parallel

cmake --preset asan-ubsan
cmake --build --preset asan-ubsan --parallel
ctest --preset asan-ubsan --parallel
```

## Privileged Integration Tests

The default suite does not need real block devices. Storage and provisioning
changes must additionally run against disposable environments:

```bash
cmake --build --preset gcc-debug --target real-btrfs-integration
cmake --build --preset gcc-debug --target qemu-hotplug-integration
```

The equivalent orchestration entry points are:

```bash
python3 tests/integration/docker/run_real_btrfs.py
python3 tests/qemu/run_hotplug.py
```

Both paths use privileged containers or root-equivalent virtualization
facilities. Access to the Docker daemon is itself root-equivalent. The tests
must operate only on their disposable loop devices, images and guests, never
on a real backup target or host configuration.

## Release Artifacts

Build the complete artifact set through the release orchestrator:

```bash
python3 tools/release.py --target all --static-tests
```

Before publication, run its full test mode and verify the artifacts in the
supported release containers:

```bash
python3 tools/release.py --target all --full-tests
python3 tests/integration/docker/run_release_matrix.py
```

The release path stages installation with CMake/CPack, then uses native package
definitions where a format needs them. Package lifecycle behavior is kept
declarative and audited as described in
[Packaging Scriptlets](packaging-scriptlets.md). Follow the complete
[release checklist](releasing.md) before tagging or publishing.

## Deliberate Shell Boundaries

Repository automation must not add shell launchers or hidden compiler entry
points. The QEMU host runner and guest scenario are Python modules. The host
copies the guest module and a JSON configuration onto the disposable setup disk;
the cached systemd unit starts it directly with the guest Python interpreter.

Two C++ process tests also invoke `sh -c` as test input to exercise process
groups and pipelines. These exact call sites are allowlisted by the
architecture contract. They are not production execution paths and must not be
used as a model for new subprocess code.
