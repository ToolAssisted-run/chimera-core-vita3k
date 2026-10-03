#!/bin/sh
# Builds the native reference (build/native/bin/vita3k-run-native): Vita3K's
# libraries with CHIMERA_HEADLESS, our driver in place of the program.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
build="$root/build/native"

sh "$here/apply-patches.sh"
sh "$here/build-deps.sh" native
sh "$here/gen-gl.sh"

# GCC: clang 20 cannot compile libstdc++ 14's std::ranges::to, which
# Vita3K's string utilities use (and Boost's bootstrap, run by Vita3K's CMake,
# finds gcc by its plain name, where a distribution may ship only clang-NN)
cc="${CC:-gcc}"
cxx="${CXX:-g++}"

cmake -S "$root/extern/vita3k" -B "$build" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O2 -g -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -g -DNDEBUG" \
	-DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
	-DCHIMERA_HEADLESS=ON -DCHIMERA_DRIVER_DIR="$here/native" -DCHIMERA_FFMPEG_DIR="$root/build/deps/ffmpeg-native" \
	-DUSE_DISCORD_RICH_PRESENCE=OFF -DUSE_LTO=NEVER \
	-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build "$build" --target vita3k-run-native
