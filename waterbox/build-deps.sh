#!/bin/sh
# The libraries Vita3K takes from outside its tree, built for one flavour.
#   sh build-deps.sh native|guest
#
#   build/deps/ffmpeg-<flavour>   FFmpeg 7.1.2 (the version Vita3K's headers
#                                 are), identically configured for both
#                                 flavours so decoded media is the same in
#                                 each: no assembly, no threads, only what
#                                 Vita3K opens
#   build/deps/openssl-guest      OpenSSL (libcrypto: hashes, PKG/PFS keys; libssl links, unused: no network);
#                                 natively the system's
#   build/deps/boost-guest        Boost.Filesystem from Vita3K's own Boost;
#                                 natively Vita3K's CMake builds it
# Every source is pinned by SHA-256 (or by the submodule).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
flavour="${1:?native|guest}"
deps="$root/build/deps"
jobs="$(nproc)"
mkdir -p "$deps"

FFMPEG_URL=https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz
FFMPEG_SHA256=089bc60fb59d6aecc5d994ff530fd0dcb3ee39aa55867849a2bbc4e555f9c304
OPENSSL_URL=https://github.com/openssl/openssl/releases/download/openssl-3.0.15/openssl-3.0.15.tar.gz
OPENSSL_SHA256=23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533

SR="${MINIBOX_SYSROOT:-${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}/build/meson-cpp/guest-sysroot}"
GUEST_CFLAGS="-fvisibility=hidden -mcmodel=large -mstack-protector-guard=global -fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -O2"

fetch() {  # <url> <sha256> <file>
	[ -f "$deps/$3" ] || curl -sL -o "$deps/$3" "$1"
	echo "$2  $deps/$3" | sha256sum -c - >/dev/null
}

# ---- FFmpeg ----
out="$deps/ffmpeg-$flavour"
if [ ! -f "$out/lib/libavcodec.a" ]; then
	fetch "$FFMPEG_URL" "$FFMPEG_SHA256" ffmpeg-7.1.2.tar.xz
	src="$deps/ffmpeg-7.1.2"
	[ -d "$src" ] || tar xJf "$deps/ffmpeg-7.1.2.tar.xz" -C "$deps"
	bld="$deps/ffmpeg-$flavour-obj"
	rm -rf "$bld"; mkdir -p "$bld"
	if [ "$flavour" = guest ]; then
		cc="gcc -specs=$SR/lib/musl-gcc.specs"
		cflags="$GUEST_CFLAGS"
		cross="--enable-cross-compile --target-os=linux --arch=x86_64 --pkg-config=false"
	else
		cc=gcc
		cflags="-O2"
		cross="--enable-pic"
	fi
	(cd "$bld" && "$src/configure" --prefix="$out" --cc="$cc" --extra-cflags="$cflags" $cross \
		--disable-asm --disable-x86asm --disable-inline-asm --disable-programs --disable-doc \
		--disable-network --disable-shared --enable-static --disable-autodetect --disable-debug \
		--disable-everything --disable-avdevice --disable-avfilter \
		--enable-avcodec --enable-avformat --enable-swscale --enable-swresample \
		--enable-decoder=h264,aac,aac_latm,mp3,mp3float,mjpeg,pcm_s16le \
		--enable-parser=h264,aac,aac_latm,mpegaudio,mjpeg \
		--enable-demuxer=mov,mp3,aac,h264,mjpeg,wav \
		--enable-protocol=file \
		--disable-pthreads --disable-w32threads --disable-os2threads \
		--disable-iconv --disable-zlib --disable-bzlib --disable-lzma --disable-sdl2 --disable-xlib \
		--disable-runtime-cpudetect) > "$bld/configure.log" 2>&1 || { tail -20 "$bld/configure.log"; exit 1; }
	make -C "$bld" -j"$jobs" > "$bld/make.log" 2>&1 || { tail -20 "$bld/make.log"; exit 1; }
	make -C "$bld" install > "$bld/install.log" 2>&1
fi
echo "ffmpeg-$flavour: $(ls "$out/lib" | tr '\n' ' ')"

[ "$flavour" = guest ] || exit 0

# ---- OpenSSL: libcrypto only ----
out="$deps/openssl-guest"
if [ ! -f "$out/lib64/libcrypto.a" ] && [ ! -f "$out/lib/libcrypto.a" ]; then
	fetch "$OPENSSL_URL" "$OPENSSL_SHA256" openssl-3.0.15.tar.gz
	bld="$deps/openssl-guest-obj"
	rm -rf "$bld"; mkdir -p "$bld"
	tar xzf "$deps/openssl-3.0.15.tar.gz" -C "$bld" --strip-components=1
	(cd "$bld" && CC="gcc -specs=$SR/lib/musl-gcc.specs" ./Configure linux-x86_64 --prefix="$out" --libdir=lib \
		no-shared no-asm no-threads no-dso no-engine no-async no-tests \
		no-module no-legacy no-autoload-config no-secure-memory no-afalgeng no-devcryptoeng \
		$GUEST_CFLAGS) > "$bld/configure.log" 2>&1 || { tail -20 "$bld/configure.log"; exit 1; }
	make -C "$bld" -j"$jobs" build_libs > "$bld/make.log" 2>&1 || { tail -20 "$bld/make.log"; exit 1; }
	make -C "$bld" install_dev > "$bld/install.log" 2>&1
fi
echo "openssl-guest: $(ls "$out"/lib*/ 2>/dev/null | grep -E '\.a$' | tr '\n' ' ')"

# ---- Boost.Filesystem, from Vita3K's Boost ----
out="$deps/boost-guest"
if [ ! -f "$out/lib/libboost_filesystem.a" ]; then
	boost="$root/extern/vita3k/external/boost"
	bld="$deps/boost-guest-obj"
	rm -rf "$bld"; mkdir -p "$bld"
	# b2 is a host program: bootstrapped with the host's compiler, in a copy
	# (Boost's bootstrap writes into its own tree)
	cp -r "$boost/." "$bld/src"
	(cd "$bld/src" && sh bootstrap.sh --with-toolset=gcc) > "$bld/bootstrap.log" 2>&1 || { tail -20 "$bld/bootstrap.log"; exit 1; }
	cat > "$bld/user-config.jam" <<JAM
using gcc : guest : gcc : <compileflags>"-specs=$SR/lib/musl-gcc.specs $GUEST_CFLAGS" <cxxflags>"-nostdinc++ -I$SR/include/c++/$(gcc -dumpfullversion) -I$SR/include/c++/$(gcc -dumpfullversion)/x86_64-linux-musl" ;
JAM
	(cd "$bld/src" && ./b2 --user-config="$bld/user-config.jam" --build-dir="$bld/obj" --prefix="$out" \
		toolset=gcc-guest link=static runtime-link=static threading=single variant=release \
		--with-filesystem -j"$jobs" install) > "$bld/b2.log" 2>&1 || { tail -20 "$bld/b2.log"; exit 1; }
fi
echo "boost-guest: $(ls "$out/lib" | grep -E '\.a$' | tr '\n' ' ')"
