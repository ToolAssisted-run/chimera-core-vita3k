#!/bin/sh
# Builds the test apps: vitasdk's own samples (CC0) and tests/apps (ours,
# CC0), compiled from source with vitasdk's prebuilt toolchain and libraries.
# Nothing here needs PS Vita firmware or a game.
#
#   build/deps/vitasdk          the toolchain, pinned by SHA-256
#   build/deps/vitasdk-samples  the samples, pinned by commit
#   build/testapps/*.vpk        what the gate runs
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
deps="$root/build/deps"
out="$root/build/testapps"

SDK_URL=https://github.com/vitasdk/autobuilds/releases/download/sdk-snapshot-20260926.790.1/vitasdk-x86_64-linux-gnu-2026-09-26_08-31-53.tar.bz2
SDK_SHA256=87b111ff350c1f36bde1c2923e57467c1f86b044d54bc616631260486f2a512c
SAMPLES_URL=https://github.com/vitasdk/samples.git
SAMPLES_COMMIT=fe8fbef
# vitasdk's prebuilt libraries: a rolling release, so each is pinned by
# SHA-256 (a changed package fails here, and is taken on purpose)
PKG_URL=https://github.com/vitasdk/packages/releases/download/master
PKGS="sdl2:f7318878817e5268fd991a2796d83a544c7688d0c98f510e4d6c9d3b07725e5b
libvita2d:f2688386ca15e030450223812c65beea4d1ce930a79b8a99d04f99b55ab1a455
zlib:f39cea16c176f93dc744597113f093db0d48adf23dd25a14ee0d2ec47df17fa6
libpng:e502fac413e17b75a06f8f2a88f87daec13d83a33e9206713a0612ee245b72b8"

mkdir -p "$deps" "$out"
if [ ! -x "$deps/vitasdk/bin/arm-vita-eabi-gcc" ]; then
	curl -sL -o "$deps/vitasdk.tar.bz2" "$SDK_URL"
	echo "$SDK_SHA256  $deps/vitasdk.tar.bz2" | sha256sum -c - >/dev/null
	tar xjf "$deps/vitasdk.tar.bz2" -C "$deps"
	rm "$deps/vitasdk.tar.bz2"
fi
for p in $PKGS; do
	name="${p%%:*}"
	sum="${p#*:}"
	[ -f "$deps/vitasdk/.pkg-$name" ] && continue
	curl -sL -o "$deps/$name.tar.xz" "$PKG_URL/$name.tar.xz"
	echo "$sum  $deps/$name.tar.xz" | sha256sum -c - >/dev/null
	tar xJf "$deps/$name.tar.xz" -C "$deps/vitasdk/arm-vita-eabi"
	rm "$deps/$name.tar.xz"
	touch "$deps/vitasdk/.pkg-$name"
done
if [ ! -d "$deps/vitasdk-samples/.git" ]; then
	git clone -q "$SAMPLES_URL" "$deps/vitasdk-samples"
fi
git -C "$deps/vitasdk-samples" checkout -q "$SAMPLES_COMMIT"

VITASDK="$deps/vitasdk"
PATH="$VITASDK/bin:$PATH"
export VITASDK PATH
build() {  # <source dir> <build name>
	cmake -S "$1" -B "$deps/samples-build/$2" -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build "$deps/samples-build/$2" -j >/dev/null
	cp "$deps/samples-build/$2"/*.vpk "$out/"
}
for s in hello_world debugscreen ctrl touch rtc audio; do
	build "$deps/vitasdk-samples/$s" "$s"
done
# GXM: SDL2's renderer, and our own app that draws its threads' interleaving
build "$deps/vitasdk-samples/sdl2/redrectangle" redrectangle
build "$root/tests/apps/ThreadTest" threadtest
build "$root/tests/apps/InputTest" inputtest
build "$root/tests/apps/AudioTest" audiotest
build "$root/tests/apps/HleTest" hletest
ls "$out"
