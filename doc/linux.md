# Linux build and test guide

This branch ports [Ekspym/ggpo](https://github.com/Ekspym/ggpo)'s
`tekken6-rollback` revision `2fc5dbb57abb95d1681f6e6242173431f48a3b08` to
x86_64 Linux. GGPO remains under its existing [MIT license](../LICENSE).

The tests need no game, firmware, GPU, display server, or RPCN account. A small
Linux VM is sufficient. Building and testing this library is separate from
building RPCS3 or establishing Tekken multiplayer compatibility. Follow the
RPCS3 Linux prerelease instructions for the matching source revisions and
emulator setup; ps3recomp is not required for the emulated rollback path.

## Prerequisites

The library requires CMake 3.15+, a C++ compiler, and a build tool. The commands
below also install dependencies for the test matrix: GCC, Clang, Ninja, Python 3,
and system zlib development headers. Tests require a C++11-capable compiler;
the library remains C++98. Use CMake 3.20+ for `ctest --test-dir` below; with older
CTest, run `ctest` from inside the build directory.

Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build clang python3 zlib1g-dev
```

Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja clang compiler-rt python zlib git
```

Fedora:

```sh
sudo dnf install gcc gcc-c++ cmake ninja-build clang compiler-rt python3 zlib-devel git
```

Arch publishes [Clang and its runtime dependencies](https://archlinux.org/packages/extra/x86_64/clang/)
and [zlib headers](https://archlinux.org/packages/core/x86_64/zlib/).
Fedora packages the [Ninja build tool](https://packages.fedoraproject.org/pkgs/ninja-build/ninja-build/)
and [zlib development headers](https://packages.fedoraproject.org/pkgs/zlib/zlib-devel/);
newer Fedora releases may satisfy `zlib-devel` through a compatible provider.
Package instructions are starting points; see the validated environments below.

## Build and run tests

Run these commands from this checkout's root:

```sh
cmake -S . -B build-gcc-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF \
  -DGGPO_BUILD_VECTORWAR=OFF -DGGPO_BUILD_TESTS=ON
cmake --build build-gcc-debug --parallel 2
ctest --test-dir build-gcc-debug --output-on-failure --parallel 2
```

For a release library use `-DCMAKE_BUILD_TYPE=Release`; for a shared library
use `-DBUILD_SHARED_LIBS=ON`. Vector War remains Windows-only. To install a
shared build into a user-owned directory:

```sh
cmake --install build-gcc-shared --prefix "$HOME/.local"
```

The `build-gcc-shared` directory is produced by the matrix below; when building
manually, substitute the directory you configured with `BUILD_SHARED_LIBS=ON`.

For the full validation matrix:

```sh
sh tests/run_linux.sh
```

It runs GCC Debug/Release, Clang Debug, Clang AddressSanitizer +
UndefinedBehaviorSanitizer (with leak detection), and GCC Release shared-library
builds. Each runs all eleven CTest tests. It compares literal packet fixture bytes
across builds, installs the shared library under its build directory, and compiles
and runs a separate C99 consumer using only the installed header/library.
`GGPO_TEST_JOBS=2` is the default parallelism; for example,
`GGPO_TEST_JOBS=4 sh tests/run_linux.sh` uses four jobs. Build products remain
under `build-*`. The script deliberately requires x86_64 Linux.

## Validated behavior and remaining work

On Ubuntu 26.04.1 x86_64, the complete matrix passed all 55 tests: eleven each
in GCC Debug/Release, Clang Debug, Clang ASan/UBSan with leak detection, and
GCC shared Release. Packet fixtures matched across all five configurations,
and the installed shared-library C99 consumer passed. These scenarios produced
no sanitizer or leak reports. Clang reports existing warnings about an empty
public union member and incomplete enum switches; public layouts are preserved.

The Linux RPCS3 integration has reached Tekken 6 practice mode on an Ubuntu
VM. This does not verify Linux-to-Linux Tekken matches or interoperability with
the Windows release. Windows compilation/execution, mixed-platform matches,
macOS support, and architectures other than little-endian x86_64 are not
validated by this test result. Arch and Fedora results should be recorded
separately when their clean-environment checks have completed.

For a reproducible test report, include `git rev-parse HEAD`, the distribution,
architecture, `gcc --version`, `clang --version`, `cmake --version`, the command
used, and failing CTest output. Review logs before sharing them. Do not attach
game/firmware files, RPCN credentials, or emulator configuration containing
personal information.

## What the tests verify

- A C translation unit includes `ggponet.h`, links, and calls the public API.
  It also includes system `poll.h` and `zlib.h`, checking that GGPO does not
  expose private headers that shadow consumer dependencies.
- Monotonic timing, environment settings, zero-timeout polling, periodic
  callbacks, interrupted waits/sleeps, and bounded input descriptions.
- Occupied UDP ports return `GGPO_ERRORCODE_GENERAL_FAILURE` with a null session;
  repeated failed starts and successful shutdown do not leak file descriptors.
- Packed packet fixtures cover every message type, header/payload offsets,
  negative frame sentinels, and 10-bit input indices. Fixtures describe the
  existing little-endian x86 layout; the wire format has not been redesigned.
- Two separate processes run a deterministic integer-only simulation with
  56-byte inputs per player. An independent reference checks every one of the
  first 600 frames, including corrected history after rollbacks.
- A local Python UDP proxy adds input latency, loss, duplication and reordering.
  Fault schedules use packet counts and elapsed time; exact packet interleavings
  and rollback counts can vary with process scheduling. Delayed/impaired tests
  require actual state loads and resimulation, not merely matching final hashes.
- A temporary outage recovers, resumes play, and exercises the prediction limit.
  Permanent outages verify interruption/disconnect notifications, including a
  separate test before any remote input arrives.
- Dense 56-byte peer and 112-byte spectator input deltas exercise packet
  capacity, exact boundaries, retries, duplicates, ACK progress and backlog
  exhaustion. A full pending queue disconnects the endpoint without silently
  dropping input or asserting.
- A third spectator process matches the same 600-frame reference. Synctest
  verifies replay checksums and closes between batches to check snapshot cleanup.

Tests bind local ephemeral UDP ports and inject faults only inside their own
proxy; they do not change firewall or system network configuration. Each peer
has a deadline, and CTest also enforces a timeout. Per-peer stdout, GGPO logs,
and synctest diagnostics remain under `build-*/tests/results/<scenario>/peer*/`.
CTest summaries are under `build-*/Testing/Temporary/LastTest.log`.

For an individual scenario:

```sh
ctest --test-dir build-gcc-debug -R '^impaired$' --output-on-failure
```

## Implementation and compatibility

The Linux platform uses `CLOCK_MONOTONIC`, environment configuration, and
interruptible POSIX sleep/poll. UDP sockets use POSIX descriptors, `fcntl`
nonblocking mode, `socklen_t`, `close`, and errno handling. Windows keeps its
Winsock and handle paths. `GGPO_CALL` preserves `__cdecl` on Windows and is empty
on Linux. Public structure layouts and packet field definitions are unchanged.

The fork's 56-byte input limit, 10-bit input indices, and internal three-frame
prediction limit are retained. The public `GGPO_MAX_PREDICTION_FRAMES` constant
remains eight, as in the source fork. The existing `rand()` handshake use and
Windows DLL seeding remain unchanged; no new RNG or packet encoding is added.

Portability tests also exposed existing bugs fixed in this branch: uninitialized
protocol state/statistics, a startup bandwidth division by zero, a varargs log
format mismatch/reused `va_list`, a disconnect rollback to nonexistent frame -1,
and a pending synctest snapshot leak. Bounded formatting replaces Windows-only
CRT calls, and assertions report failure with a nonzero process status.

Environment variable names contain dots; set them with `env`, for example:

```sh
env 'ggpo.log=true' 'ggpo.log.timestamps=true' your-program
```

Passing these tests establishes Linux library behavior for the exercised cases.
It does not establish interoperability with the released Windows build or
Tekken/RPCS3 determinism. Windows compilation/execution and mixed Windows/Linux
sessions still need validation. The raw packed bitfield protocol is retained,
so other architectures/endianness are not certified. The integration harness uses sparse
input changes; a separate dense-input regression covers compressed-packet
capacity. Malformed network packets remain outside this port's validation.
