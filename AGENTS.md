# AGENTS.md - Vita3K core for Chimera

This repository builds Vita3K (https://github.com/Vita3K/Vita3K), a
PlayStation Vita emulator, as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It produces one file, `vita3k.chimeraCore`: Vita3K's libraries
compiled as a guest for Chimera's sandbox (miniBox), a driver that runs the
machine a frame at a time on its own scheduler and clock, and the
declarations Chimera reads. Upstream is a pinned submodule; every change to
it is a numbered patch. The picture is drawn by Vita3K's OpenGL renderer on
the host's GPU, through Chimera's GPU bridge.

## Layout

- `extern/vita3k` - upstream Vita3K, a pinned git submodule.
- `patches/` - the numbered series applied to `extern/vita3k`.
- `waterbox/driver/` - the core, the same code in both flavours: boot and
  the frame, the frame host over the GPU bridge, the exports Chimera calls
  (`wbx-entry.cpp`), the machine's filesystem, save data.
- `waterbox/guest/`, `waterbox/native/` - what Vita3K's own CMake includes
  for each flavour; `native/run-native.cpp` is the native harness.
- `waterbox/run-wbx.c`, `gate-harness.h`, `gl-host.c`, `glad/` - the sandbox
  harness, the options both harnesses share, the GPU bridge's host half.
- `waterbox/*.sh` - the builds and the gate. `make-patch.py` writes a patch.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json` -
  the declarations. Written by hand here.
- `waterbox/tests/` - the gate's oracles and checks. `tests/apps/` - this
  repository's own test apps (CC0 homebrew).
- `docs/PLAN.md` - the design log. `docs/BUILDING.md` - the build in detail.
- `.github/workflows/chimera.yml` - CI: gate, contract tests, release.
- `build/` - all build output, downloads and test content. Ignored by git.

## Set up the build environment

Ubuntu, as CI. `<chimera>` is a Chimera checkout; `<core>` is this one. GCC
14 must be first on `PATH` in every shell that builds anything below,
miniBox included.

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 perl curl xz-utils bzip2 mono-complete libgl1-mesa-dev libegl-dev libegl-mesa0 libgl1-mesa-dri libgbm1 libx11-dev libxext-dev libasound2-dev gcc-14 g++-14

mkdir -p "$HOME/gcc14"
for t in gcc:gcc-14 g++:g++-14 cc:gcc-14 c++:g++-14 cpp:cpp-14; do
  ln -sf "/usr/bin/${t#*:}" "$HOME/gcc14/${t%%:*}"
done
export PATH="$HOME/gcc14:$PATH"
gcc -dumpfullversion

git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
export CHIMERA_ROOT=<chimera>
export MINIBOX_DIR=<chimera>/extern/chimera-common-minibox

cd <core>
git submodule update --init --recursive --depth 1

mb="$MINIBOX_DIR"
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

The build downloads sources (GCC for miniBox's guest libstdc++, FFmpeg,
OpenSSL, the Vita SDK). The gate's engine legs and Chimera's contract tests
also need Chimera's own build and the .NET SDK 8.0: see `docs/BUILDING.md`.

## Build

```
cd <core>
waterbox/build-package.sh -r "$CHIMERA_ROOT"
```

It applies the patches, builds the guest's libraries (`build/deps`), the
guest (`build/guest`) and `build/wbx/core.wbx`, and writes
`build/package/vita3k.chimeraCore`. `-n` packages what `build/` already
holds. `-o <out dir>` changes where the file is written. The native
reference and the test apps, for debugging and for the gate:

```
waterbox/build-native.sh
waterbox/build-testapps.sh
```

## Install the core into Chimera

Chimera ships no cores and downloads nothing. A core is a file in its cores
folder: `<chimera>/build/Cores/` in a source checkout, or the `Cores` folder
beside `Chimera.exe` in a release bundle, or the folder chosen in File >
Core Manager > Change folder... File > Core Manager lists the folder;
Refresh List rescans it. The same package works on Linux and on Windows.
`build-package.sh -r <chimera>` copies the package into
`<chimera>/build/Cores/` when `<chimera>/build` exists; otherwise copy
`build/package/vita3k.chimeraCore` there yourself. A package built by hand
stamps `<commit>+local` (`-dirty` with changes in the tree, which the applied
patches count as) and is for testing. Only CI's packages are published.

## Test before you commit

```
sh ./waterbox/run-gate.sh
```

It builds the test apps, the native reference, the guest, `core.wbx` and the
package, then prints `PASS`, `FAIL` or `SKIP` for every leg. `-n` skips the
builds. The exit status is the number of failed legs: it must be 0. Logs:
`build/*.log` for the builds, `build/gate/` for the legs.

- With nothing provided it runs on homebrew: every test app native twice and
  sandboxed once, states, input, sound, save data, settings, the
  declarations, the package through `chimera-run`, and the negative
  controls. That is what CI runs.
- The game and firmware legs look in `build/content/` (or the folder
  `VITA3K_CONTENT` names) for `alien-shooter.zip`, `alien-breed.zip`,
  `PSP2UPDAT.PUP` and `PSVUPDAT.PUP`, and say `SKIP` without them. A change
  to the machine must be run with them. If you do not have them, say so in
  your report.
- The gate needs an EGL that gives an OpenGL context with no display
  (Mesa's llvmpipe in CI). It unsets `DISPLAY` itself.
- CI then runs Chimera's contract tests against the package
  (`docs/BUILDING.md`, Run the gates).

## Rules of this repository

- `extern/vita3k` is a submodule. Never commit inside it. A change to Vita3K
  is a numbered patch in `patches/`, applied by `waterbox/apply-patches.sh`.
  The series is all or nothing: a half-patched tree is an error. Edit the
  submodule's working tree, then `waterbox/make-patch.py <NNNN-chimera-name>`
  writes the next patch from what the tree holds beyond the series
  (`--check` lists it and writes nothing; `--amend <NNNN>` rewrites one).
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate
  must round-trip. The gate checks it. A change that breaks it is a bug.
- Run the gate before committing. A new leg needs a negative control: show
  that it fails when the thing it checks is broken (Chimera's
  `docs/gates.md`).
- Never commit game files, firmware or a game's licence, and never print a
  licence into a log or a document. Never add network access: the machine
  has none (patch 0008).
- The declarations must agree with the driver: the gate runs
  `python3 waterbox/tests/check-declaration.py waterbox`.
- Test content must be distributable: homebrew built from source by
  `waterbox/build-testapps.sh`. Every download a script makes is pinned by
  SHA-256 or by commit; keep it so.
- Shell scripts stay executable (git mode 100755). Prose is plain ASCII.
- Commit messages: a type prefix with an optional scope (`feat:`, `fix:`,
  `fix(gate):`, `perf:`, `test(gate):`), then a sentence in lower case that
  says what is now true; a Chimera issue is cited as `(chimera#NNN)`. The
  body says what was wrong, what was measured, and the gate's result.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - every build step, option and error message.
- `docs/PLAN.md` - milestones and decisions, and what is open.
- `waterbox/gate-harness.h` - the options of both harnesses.
- In a Chimera checkout: `docs/porting-a-core.md`, `docs/gates.md`,
  `docs/core-manager.md`.
