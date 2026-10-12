![](doc/images/ggpo_header.png)

&nbsp; _[![Appveyor build status](https://img.shields.io/appveyor/ci/pond3r/ggpo/master.svg?logo=appveyor)](https://ci.appveyor.com/project/pond3r/ggpo/branch/master)_

(日本語ドキュメントは[こちら](README.ja.md))

## What's GGPO?

Traditional techniques account for network transmission time by adding delay to a players input, resulting in a sluggish, laggy game-feel.  Rollback networking uses input prediction and speculative execution to send player inputs to the game immediately, providing the illusion of a zero-latency network.  Using rollback, the same timings, reactions, visual and audio queues, and muscle memory your players build up playing offline will translate directly online.  The GGPO networking SDK is designed to make incorporating rollback networking into new and existing games as easy as possible.  

For more information about the history of GGPO, check out http://ggpo.net/

This repository contains the code, documentation, and sample applications for the SDK.

## Building

This branch ports the Tekken rollback fork's GGPO library to x86_64 Linux.
Windows build support is retained but has not been revalidated after this port.
Vector War remains Windows-only. Building the modified RPCS3 and testing
Tekken matches are separate steps.

### Linux

Requires CMake 3.15+, a C++ compiler, and a build tool. The headless tests also
require Python 3, system zlib development headers, and a C++11-capable compiler;
the library remains C++98. Use CMake 3.20+ for the `ctest --test-dir` command
below, or run `ctest` from the build directory with older CTest versions.

```sh
cmake -S . -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DGGPO_BUILD_VECTORWAR=OFF -DGGPO_BUILD_TESTS=ON
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
```

Use `-DBUILD_SHARED_LIBS=ON` for a shared library. See [Linux development and
testing](doc/linux.md) for prerequisites, the full compiler/sanitizer matrix,
test coverage, and compatibility limits.

### Windows

Windows builds requires both [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/) and [CMake](https://cmake.org/download/).  Make sure you've installed both before starting.  Make sure to add CMake to your path when installing.

- Run the `build_windows.cmd` in the root directory of the SDK to generate the Visual Studio 2019 solution files.   
- Open `build/GGPO.sln` solution for Visual Studio 2019 to compile.

You can also run the `cmake-gui` tool if you prefer. 

## Sample Application

The Vector War application in the source directory contains a simple application which uses GGPO to synchronize the two clients.  The command line arguments are:

```
vectorwar.exe  <localport>  <num players> ('local' | <remote ip>:<remote port>) for each player
```

See the .cmd files in the bin directory for examples on how to start 2, 3, and 4 player games.

## Licensing

GGPO is available under The MIT License. This means GGPO is free for commercial and non-commercial use. Attribution is not required, but appreciated. 
