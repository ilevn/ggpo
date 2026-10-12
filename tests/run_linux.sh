#!/bin/sh
# Run from any directory on Linux. All products stay under the checkout's build-* directories.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
jobs=${GGPO_TEST_JOBS:-2}
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1

build_and_test() {
    name=$1
    cc=$2
    cxx=$3
    config=$4
    shared=$5
    sanitizers=$6
    build="$root/build-$name"
    # A compiler change can make CMake reset cached options. Select compilers
    # first, then apply the complete configuration before building anything.
    cmake -S "$root" -B "$build" -G Ninja \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx"
    cmake -S "$root" -B "$build" -G Ninja \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
        -DCMAKE_BUILD_TYPE="$config" -DBUILD_SHARED_LIBS="$shared" \
        -DGGPO_BUILD_VECTORWAR=OFF -DGGPO_BUILD_TESTS=ON \
        -DGGPO_SANITIZERS="$sanitizers"
    cmake --build "$build" --parallel "$jobs"
    ctest --test-dir "$build" --output-on-failure --parallel "$jobs"
    (cd "$build/tests" && "../bin/x64/$config/ggpo_checks") > "$build/checks.txt"
    sed -n '/^packet /p' "$build/checks.txt" > "$build/packets.txt"
}

if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    echo 'This validation matrix targets x86_64 Linux.' >&2
    exit 1
fi
build_and_test gcc-debug gcc g++ Debug OFF OFF
build_and_test gcc-release gcc g++ Release OFF OFF
build_and_test clang-debug clang clang++ Debug OFF OFF
build_and_test clang-sanitize clang clang++ Debug OFF ON
build_and_test gcc-shared gcc g++ Release ON OFF
for name in gcc-release clang-debug clang-sanitize gcc-shared; do
    diff -u "$root/build-gcc-debug/packets.txt" "$root/build-$name/packets.txt"
done

# Consume the installed public header and shared library outside the CMake target.
prefix="$root/build-gcc-shared/installed"
cmake --install "$root/build-gcc-shared" --prefix "$prefix"
gcc -std=c99 -Wall -Wextra -Werror "$root/tests/c_header.c" \
    -I"$prefix/include" -L"$prefix/lib" -Wl,-rpath,"$prefix/lib" \
    -lGGPO -o "$root/build-gcc-shared/installed-consumer"
"$root/build-gcc-shared/installed-consumer"
echo 'Linux matrix, packet fixtures, and installed shared-library consumer passed.'
