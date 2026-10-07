# Building the Vita3K core

This repository builds Vita3K, a PlayStation Vita emulator, as a sandboxed
guest for Chimera. The result is one file, `vita3k.chimeraCore`, which
Chimera loads. The steps below are the ones `.github/workflows/chimera.yml`
runs on a fresh clone on a public Ubuntu runner; where the workflow uses a
GitHub Action, the manual equivalent is given.

Placeholders used below:

- `<core>` - this repository's checkout.
- `<chimera>` - a checkout of https://github.com/ToolAssisted-run/chimera.
- `<miniBox>` - `<chimera>/extern/chimera-common-minibox`, the sandbox host
  and the guest toolchain, a git submodule of Chimera.

## Requirements

CI builds on `ubuntu-latest`. Cores are built on Linux; the package that
comes out runs on Linux and on Windows.

The packages the workflow installs:

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 perl curl xz-utils bzip2 mono-complete libgl1-mesa-dev libegl-dev libegl-mesa0 libgl1-mesa-dri libgbm1 libx11-dev libxext-dev libasound2-dev gcc-14 g++-14
```

Toolchains:

- **GCC 14, first on `PATH`, for everything.** Vita3K uses libstdc++ 14
  (`std::ranges::to`), and miniBox builds the guest's libstdc++ for whatever
  `gcc` is on `PATH`. So miniBox, the libraries and both flavours of the core
  are all built with GCC 14. The workflow installs `gcc-14` and `g++-14` (in
  the line above) and puts links named `gcc`, `g++`, `cc`, `c++` and `cpp`
  in a directory at the front of `PATH`:

  ```
  mkdir -p "$HOME/gcc14"
  for t in gcc:gcc-14 g++:g++-14 cc:gcc-14 c++:g++-14 cpp:cpp-14; do
    ln -sf "/usr/bin/${t#*:}" "$HOME/gcc14/${t%%:*}"
  done
  export PATH="$HOME/gcc14:$PATH"
  gcc -dumpfullversion
  ```

  The workflow appends the directory to `$GITHUB_PATH`; by hand, the `export`
  line does the same and is needed in every shell that builds. The last
  command must print a 14.x version.
- **.NET SDK 8.0**: the workflow uses `actions/setup-dotnet@v4` with
  `dotnet-version: '8.0'`. By hand, Chimera's README gives
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`
  and says a distribution's own SDK lacks targets the frontend needs. Make
  sure `dotnet` is on `PATH` afterwards. It is needed only to build Chimera's
  solution and run its contract tests, not to build the package.
- **Mono** (`mono-complete`): for Chimera's managed side, which targets .NET
  Framework 4.8 and runs on Mono on Linux.
- **An OpenGL context with no display.** Vita3K has no software renderer:
  both flavours draw through the GPU bridge on an EGL context. In CI that is
  Mesa's llvmpipe (the workflow's comment on the package line says so).
  Needed to run the gate, not to build.
- **git**, for the clone and the submodules.

What the build fetches or builds by itself. The network is needed for these
steps:

- miniBox's C++ guest toolchain (`-Dguest_cpp=true`) downloads the GCC source
  that matches the host compiler (about 84 MB) with `curl` and builds
  libstdc++ for the guest from it, in miniBox's own build directory.
- `waterbox/build-deps.sh` downloads FFmpeg 7.1.2 and builds it for each
  flavour, configured the same in both: no assembly, no threads, only what
  Vita3K opens. For the guest it also downloads and builds OpenSSL 3.0.15,
  and builds zlib and Boost.Filesystem from the copies inside the Vita3K
  submodule. Each download is pinned by SHA-256. All of it lands in
  `build/deps/`.
- `waterbox/build-testapps.sh` downloads the Vita SDK's prebuilt toolchain
  and four of its prebuilt libraries (each pinned by SHA-256), clones the
  SDK's samples (pinned by commit) into `build/deps/`, and builds the test
  apps the gate runs into `build/testapps/`.

CI caches `build/deps` with `actions/cache@v4`. Everything under `build/` is
ignored by git.

## Get the sources

This repository and every submodule, nested ones included: Vita3K's CMake
wants them all present.

```
git clone https://github.com/ToolAssisted-run/chimera-core-vita3k.git <core>
cd <core>
git submodule update --init --recursive --depth 1
```

Chimera, with its submodules. The workflow uses `actions/checkout@v6` with
`repository: ToolAssisted-run/chimera`, `ref: main` and
`submodules: recursive`:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

Where the scripts look for Chimera and miniBox:

- `waterbox/build-package.sh` takes `-r <chimera>` or `CHIMERA_ROOT`. Without
  either it tries `<core>/../chimera`, then `$HOME/chimera`. It takes miniBox
  from `-m <miniBox>` or `MINIBOX_DIR`, else
  `<chimera>/extern/chimera-common-minibox`, and exports `MINIBOX_DIR` to the
  scripts it calls.
- `waterbox/run-gate.sh` has no option for either. It reads `CHIMERA_ROOT`
  (default `$HOME/chimera`), and the build scripts it calls read
  `MINIBOX_DIR`.
- The other scripts that need miniBox (`build-deps.sh`, `gen-gl.sh`,
  `build-core.sh`), the guest toolchain file and the driver's two
  `CMakeLists.txt` read `MINIBOX_DIR` and fall back to
  `$HOME/chimera/extern/chimera-common-minibox`.

So export both, as the workflow's gate step does:

```
export CHIMERA_ROOT=<chimera>
export MINIBOX_DIR=<chimera>/extern/chimera-common-minibox
```

## Build miniBox

The host library and the C++ guest toolchain, in two build directories, with
GCC 14 first on `PATH`. These are the workflow's commands:

```
mb="$MINIBOX_DIR"
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

- `build/meson-linux` holds the host library (`source/host/libminiboxhost.so`)
  that `run-wbx`, the gate's sandbox runner, links.
- `build/meson-cpp` holds the guest sysroot (`guest-sysroot/`: musl, the
  `musl-gcc.specs` file and the guest's `libstdc++.a`).

CI caches these two directories with `actions/cache@v4`, under a key that
names the compiler. By hand they simply stay where they are. A
`build/meson-cpp` that was built with another gcc holds that compiler's
libstdc++: build that directory afresh with GCC 14 on `PATH`.

## Build the core

All commands run from `<core>`, with GCC 14 on `PATH` and `MINIBOX_DIR`
exported.

**Patches.** Everything that changes Vita3K is a numbered patch in
`patches/`, applied to the `extern/vita3k` working tree by
`waterbox/apply-patches.sh`. Both build scripts run it first. It judges the
series as a whole:

- a pristine submodule gets every patch, in order (`applied: <name>` each);
- a tree that already carries the whole series is left alone
  (`already applied: all N patches`);
- anything in between is an error that names the files that differ.

Before touching the tree it tries the series on a scratch copy of the
submodule's HEAD, so a series that does not apply stops there.

**The libraries and the generated sources.** Both build scripts also run
`waterbox/build-deps.sh native|guest` (FFmpeg and, for the guest, OpenSSL,
zlib and Boost.Filesystem, into `build/deps/`) and `waterbox/gen-gl.sh` (both
halves of the GPU bridge into `build/generated-gl/`, and Vita3K's static
assets into `build/generated-assets.cpp`). A library that is already built is
not built again.

**The guest.** Vita3K's own CMake under the miniBox musl toolchain
(`waterbox/guest-toolchain.cmake`), generating for Ninja, into `build/guest`.
The driver in `waterbox/driver` is built by the same CMake, as the target
`vita3k-guest-core`:

```
waterbox/build-guest.sh
```

**core.wbx.** `waterbox/build-core.sh` links the guest archives and the
guest's libraries into `build/wbx/core.wbx`, checks it with miniBox's
`check-wbx.sh`, and builds `build/wbx/run-wbx`, the host runner the gate
uses:

```
waterbox/build-core.sh -m "$MINIBOX_DIR"
```

Its options are `-m <miniBox dir>` and `-o <output dir>`.

**The native reference.** The same driver over Vita3K's libraries, built for
the host, with the GPU bridge's host half in the same process:
`build/native/bin/vita3k-run-native`. The gate compares it with `core.wbx`
in the sandbox, frame by frame. It is also where a debugger works.

```
waterbox/build-native.sh
```

It builds with `gcc` and `g++`; `CC` and `CXX` override them. It takes
OpenSSL and zlib from the system (see Troubleshooting).

**The test apps.** Homebrew the gate runs with no firmware and no game: the
Vita SDK's samples and this repository's own apps in `tests/apps`, built
into `build/testapps/*.vpk`:

```
waterbox/build-testapps.sh
```

The package needs only the guest and `core.wbx`. The gate builds the rest.

## Build the package

```
waterbox/build-package.sh -r <chimera>
```

Options:

- `-r <chimera root>` - the Chimera checkout.
- `-m <miniBox dir>` - miniBox, when it is not `<chimera>/extern/chimera-common-minibox`.
- `-o <out dir>` - where the package file is written (default `build/package`).
- `-n` - package what `build/` already holds: no guest build, no relink.

What it does:

1. Unless `-n`: runs `waterbox/build-guest.sh`, then `waterbox/build-core.sh`.
2. Stages `build/wbx/core.wbx` with its debug sections stripped (the symbol
   table stays; `build/wbx/core.wbx` keeps everything for a debugger),
   `waterbox/waterbox.config`, `waterbox/default_keybinds.json`,
   `waterbox/file_slots.json`, the licences of everything linked (from
   `waterbox/package-licenses.json`) and `build.json` (what built the
   package: commit, compiler, miniBox commit, Vita3K pin).
3. Stamps the version, zips the result twice with fixed dates and
   permissions, and fails if the two archives differ. It prints
   `package sha1 <hash>`.

Where the file lands:

- Always `<out dir>/vita3k.chimeraCore`: `build/package/vita3k.chimeraCore`
  unless `-o` says otherwise (`packaged -> <path>`).
- If `<chimera>/build` exists, it is also copied to
  `<chimera>/build/Cores/vita3k.chimeraCore`, and
  `<chimera>/build/CoreCache/vita3k-*` is removed so Chimera does not run a
  stale extracted copy (`installed -> <path>`). If `<chimera>/build` does not
  exist, nothing is installed.

The version stamp:

- A package's version is the commit it was built from. CI sets
  `CORE_VERSION` to the commit, and that is what a published package carries.
- Without `CORE_VERSION` the script stamps `<commit>+local`, and
  `<commit>-dirty+local` when `git diff --quiet HEAD` reports a change. The
  applied patch series makes the `extern/vita3k` working tree differ from its
  commit, and git counts that, so a package built by hand normally reads
  `-dirty+local`.
- The commit's date in UTC is stamped beside it as `versionDate`.

A package built by hand is for testing. Chimera's publishing refuses a
version that carries `+local` or `-dirty`.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
is a file somebody puts in its cores folder.

- **A Chimera source checkout.** The cores folder is `<chimera>/build/Cores/`.
  `waterbox/build-package.sh -r <chimera>` copies the package there when
  `<chimera>/build` exists. Otherwise copy
  `build/package/vita3k.chimeraCore` into that folder yourself.
- **A Chimera release bundle.** Put the file in the `Cores` folder beside
  `Chimera.exe`, or in another folder chosen with File > Core Manager >
  Change folder...

File > Core Manager lists what is in the folder. Refresh List rescans it, so
a package copied in while Chimera runs is found without a restart.

The same package file works on Linux and on Windows: the guest inside it is
run by miniBox on either. This core draws on the machine's GPU, so it needs a
machine that can give Chimera an OpenGL context.

Published packages are on this repository's Releases page. CI publishes a
rolling `dev` release on every green push to main, and a dated
`nightly-YYYY-MM-DD` release from the scheduled run (04:00 UTC, only when
main moved since the last one). The asset is named
`vita3k-<version>.chimeraCore`.

## Run the gates

CI runs two things: this repository's gate, then Chimera's contract tests
against the package the gate built. CI allows the whole job 340 minutes.

### The core's gate

```
sh ./waterbox/run-gate.sh
```

It takes one option, `-n`: do not build, use what `build/` holds. It reads
`CHIMERA_ROOT`, `MINIBOX_DIR` and `VITA3K_CONTENT`. Without `-n` it first
builds the test apps, the native reference, the guest, `core.wbx` and the
package (`build-package.sh -n -r "$CHIMERA_ROOT"`), each with a log in
`build/` (`testapps-build.log`, `native-build.log`, `guest-build.log`,
`core-link.log`, `package.log`). A build step that fails prints the end of
its log and stops the gate. Then every leg prints `PASS`, `FAIL` or `SKIP`
with a line of evidence. The exit status is the number of failed legs. The
gate unsets `DISPLAY`. Its work directory is `build/gate/`.

The engine legs run the package through
`<chimera>/build/meson-linux/chimera-run`, so Chimera's native libraries must
be built (the commands are below). Without `chimera-run` those legs are
skipped, and one `SKIP` line says so.

What runs with nothing provided, which is what CI runs:

- every test app twice through the native reference (the same in every
  frame) and once in the sandbox (native == sandbox in every frame): the
  Vita SDK's samples and the apps in `tests/apps`;
- input against an oracle that predicts it from a script; whether a frame
  read its input; the sound of two known tones, measured; save data out, back
  in, and a zip that is not a save refused;
- what a system call costs the machine, and a sleeping thread waking on time;
- states: a state saved and loaded before every frame, and a state saved in
  one process and carried on in another, both equal to one native run;
- the declarations agree with what the core reads, and the check sees two
  buttons swapped;
- settings reach the machine; turbo leaves the machine the same;
- Internal Resolution: 2x and 4x are drawn, not stretched; a value that is
  not one of the four is refused;
- a package with no licence, or with a file that is not one, is refused;
- the package in `chimera-run`: a movie, the refusal to start with no GPU,
  and Internal Resolution;
- the negative controls: the CPU clock, the start date, input one frame
  late, one sound sample dropped and surface sync off must each change the
  result.

The legs that need a game or the firmware look in `build/content/` (or the
folder `VITA3K_CONTENT` names) for `alien-shooter.zip`, `alien-breed.zip`,
`PSP2UPDAT.PUP` and `PSVUPDAT.PUP`. Without them these say `SKIP` and name
what they lacked:

- `alien-shooter.zip` with `PSP2UPDAT.PUP`: native == sandbox over 300
  frames; the font package handed over as the system software is refused.
- the same with `PSVUPDAT.PUP` too: the game on the full system software,
  and the game packed as a package with its licence in the Licence slot.
  With `alien-breed.zip` there as well: the package with another game's
  licence installs nothing.
- `alien-breed.zip` with `PSP2UPDAT.PUP`: native == sandbox over 600 frames.

Run the full gate, with the content, before pushing: CI cannot.

### Chimera's contract tests

They open the package through Chimera's engine, so Chimera's native libraries
and its solution must be built first. The workflow's commands, from
`<chimera>`:

```
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

CI does this before the gate, which is why the gate's package step finds
`<chimera>/build` and installs into `<chimera>/build/Cores`. Then:

```
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

They prove the package is readable, is built for a guest ABI this frontend
runs, makes a working core factory, binds only buttons its controller
declares, and stamps a version. They need no game.

## Files the core needs at run time

Nothing below is in this repository or in the package. The user provides it.

- **Game** (required, one file): a `.vpk`; a NoNpDRM dump as a `.zip` (the
  game's folder with its `sce_sys/package/work.bin`); or the PlayStation
  Store's own package, a `.pkg`.
- **Licence** (only for a `.pkg` game): the game's `work.bin`, or the
  console's own `.rif` for the game. A package installs nothing without it.
  A `.vpk` or a `.zip` dump needs none.
- **Save data** (optional): a `.zip` as Emulator > Export Save Data... wrote
  it.
- **Firmware**, as the System Software setting says:
  - `PSVUPDAT.PUP`, the PS Vita system software update. Required when System
    Software is `full` (the default). Most games load system libraries from
    it.
  - `PSP2UPDAT.PUP`, the PS Vita font package. Required when System Software
    is `full` or `fonts`.
  - Neither when System Software is `none`, which is for homebrew built with
    the Vita SDK.

The package carries no Sony firmware, no fonts and no game. A game's licence
is its owner's: never put one in a repository, a log or a document.

The Renderer setting has one value, `opengl-hw`: Vita3K's OpenGL renderer on
the host's GPU through Chimera's GPU bridge. Handed no GPU, the core refuses
to start and says why.

## Troubleshooting

- **The native reference does not compile (`std::ranges::to`).** The
  compiler is not GCC 14. GCC 13's libstdc++ does not have it, and clang 20
  cannot compile libstdc++ 14's. Put GCC 14 first on `PATH` (Requirements).
- **One gcc for miniBox and the core.** The guest's C++ headers are looked up
  by the version `gcc -dumpfullversion` prints
  (`waterbox/guest-toolchain.cmake`, `waterbox/build-deps.sh`), and miniBox
  builds the guest libstdc++ from the GCC source that matches the host
  compiler. Build miniBox with GCC 14 on `PATH` too.
- **`miniBox guest sysroot not found`** or **`miniBox C++ guest toolchain
  missing`**. `<miniBox>/build/meson-cpp` was not built with
  `-Dguest_cpp=true`, or the script looked in the wrong place. Export
  `MINIBOX_DIR`.
- **A script looks in `$HOME/chimera`.** That is the fallback when
  `CHIMERA_ROOT` or `MINIBOX_DIR` is not set. `run-gate.sh` has no option for
  either: export both.
- **The native CMake stops at OpenSSL, or zlib is missing at link.**
  `build-deps.sh` builds OpenSSL and zlib for the guest only; natively they
  are the system's. The workflow's package line names no development package
  for either, so on a machine that lacks them install your distribution's.
- **A download fails its checksum.** `build-deps.sh` and `build-testapps.sh`
  check every archive they download against a SHA-256 and stop on a
  mismatch. The Vita SDK's prebuilt libraries are a rolling release: a
  changed one fails here on purpose, and its new hash is taken deliberately,
  in `build-testapps.sh`.
- **A dependency does not build.** `build-deps.sh` prints the last lines of
  the failing log; the whole logs are under `build/deps/<name>-obj/`. CI
  uploads `build/gate/`, `build/*.log` and the dependencies' configure logs
  as the `gate-work` artifact when the job fails.
- **`extern/vita3k is not checked out`** (`apply-patches.sh`). Run
  `git submodule update --init --recursive --depth 1`.
- **`extern/vita3k is partly patched`**. Some touched files are neither
  pristine nor what the whole series leaves. The script prints the way back:
  `git -C extern/vita3k reset --hard && git -C extern/vita3k clean -fd && waterbox/apply-patches.sh`.
  That discards edits made in the tree; turn them into a patch first
  (`waterbox/make-patch.py`).
- **`the series does not apply to the submodule's HEAD at <patch>`**. The
  submodule was moved without rebasing the patches.
- **`no build/wbx/core.wbx`** (`build-package.sh -n`). There is nothing to
  package yet: run it without `-n`.
- **`no GPU was handed over`.** The core was started without the GPU bridge.
  `chimera-run` hands it over with `--gpu`.
- **A run stops in a path check with "Invalid argument".** A dot in the work
  directory's name (`x.work`) makes one of Vita3K's path checks throw. The
  gate names its work directories `<run>-work`; do the same when running a
  harness by hand.
