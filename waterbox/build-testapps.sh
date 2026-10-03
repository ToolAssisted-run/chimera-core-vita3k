#!/bin/sh
# Builds the test apps: vitasdk's own samples (CC0), compiled from source with
# vitasdk's prebuilt toolchain. Nothing here needs PS Vita firmware or a game.
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

mkdir -p "$deps" "$out"
if [ ! -x "$deps/vitasdk/bin/arm-vita-eabi-gcc" ]; then
	curl -sL -o "$deps/vitasdk.tar.bz2" "$SDK_URL"
	echo "$SDK_SHA256  $deps/vitasdk.tar.bz2" | sha256sum -c - >/dev/null
	tar xjf "$deps/vitasdk.tar.bz2" -C "$deps"
	rm "$deps/vitasdk.tar.bz2"
fi
if [ ! -d "$deps/vitasdk-samples/.git" ]; then
	git clone -q "$SAMPLES_URL" "$deps/vitasdk-samples"
fi
git -C "$deps/vitasdk-samples" checkout -q "$SAMPLES_COMMIT"

VITASDK="$deps/vitasdk"
PATH="$VITASDK/bin:$PATH"
export VITASDK PATH
for s in hello_world debugscreen ctrl touch rtc audio; do
	cmake -S "$deps/vitasdk-samples/$s" -B "$deps/samples-build/$s" -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build "$deps/samples-build/$s" -j >/dev/null
	cp "$deps/samples-build/$s"/*.vpk "$out/"
done
ls "$out"
