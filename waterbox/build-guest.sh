#!/bin/sh
# The guest (waterbox) build: Vita3K's own CMake under the miniBox musl
# toolchain, the libraries only (build/guest); build-core.sh links core.wbx.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
deps="$root/build/deps"

sh "$here/apply-patches.sh"
sh "$here/build-deps.sh" guest
sh "$here/gen-gl.sh"

cmake -S "$root/extern/vita3k" -B "$root/build/guest" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$here/guest-toolchain.cmake" \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O2 -g -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -g -DNDEBUG" \
	-DCHIMERA_HEADLESS=ON -DCHIMERA_DRIVER_DIR="$here/guest" -DCHIMERA_FFMPEG_DIR="$deps/ffmpeg-guest" \
	-DUSE_DISCORD_RICH_PRESENCE=OFF -DUSE_LTO=NEVER -DVITA3K_FORCE_SYSTEM_BOOST=ON -DBoost_ROOT="$deps/boost-guest" -DBoost_USE_STATIC_LIBS=ON -DBoost_USE_STATIC_RUNTIME=ON \
	-DOPENSSL_ROOT_DIR="$deps/openssl-guest" -DOPENSSL_USE_STATIC_LIBS=TRUE \
	-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_SHARED=OFF -DSDL_STATIC=ON \
	-DSDL_AUDIO=OFF -DSDL_VIDEO=OFF -DSDL_GPU=OFF -DSDL_RENDER=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF \
	-DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF \
	-DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_PTHREADS=ON -DSDL_LIBC=ON -DSDL_LOADSO=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF
cmake --build "$root/build/guest" --target vita3k-guest-core "$@"
