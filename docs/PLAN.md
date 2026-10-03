# Vita3K core: plan and design log

The PS Vita as a Chimera core (chimera#154): Vita3K, GPL-2.0-or-later,
pinned at a366df6 (2026-10-02), patched into a machine that runs a frame at a
time inside miniBox.

## Why it fits, and what it costs (feasibility, 2026-10-03)

Vita3K has the same shape as the RPCS3 port: every guest thread is a host
thread, and the picture is drawn by an OpenGL renderer. It is a fraction of the
size: about 150k lines without the Qt GUI and Android, 45 blocking wait sites,
13 `thread_local`s, and host-clock reads in about 25 files.

- CPU: dynarmic only, already proven in miniBox by Azahar, EKA2L1 and touchHLE.
  Cycle counting is off upstream and comes on for the virtual clock.
- Threads and time: the RPCS3 core's virtual-time scheduler (vsched) is the
  model. The vblank thread on the host clock becomes the frame.
- GPU: the GL 4.3 core backend, loaded through `FrameHost::get_proc_address`
  (the bridge's seam). `disable-surface-sync` defaults to true, so the machine
  never reads GPU results.
- Firmware: libc, libSceFt2, libpvf and libfiber are loaded from
  `PSVUPDAT.PUP`, so commercial games need it as project firmware. Homebrew
  built with vitasdk links newlib and needs none.
- Upstream #4145 (games at 80% speed) is host frame pacing, which a virtual
  clock replaces.

Risks: the GL rebuild after a state load (RPCS3 #43, #128, #152, #153), speed
with every guest thread on one host core, and the GL backend falling behind
Vulkan upstream.

## Milestones

- [x] M0 headless native build (2026-10-03): Vita3K's libraries under our
  own driver, no Qt, no window. All six samples boot and draw through the GL
  renderer on llvmpipe (GL 4.5 core): hello_world's text, the debug screen
  showcase in colour, ctrl's pad readout, rtc's dates, audio's tone, touch.
  The only errors logged are the two firmware kernel modules
  (os0:kd/bootimage.skprx, sysmodule.skprx) homebrew does without.
- [x] M1 virtual clock + scheduler (2026-10-03): two native runs are
  identical frame by frame for all eight test apps, ThreadTest's 77,087
  thread switches in 300 frames included, and with every host core busy.
  Negative controls: 1 MHz less CPU changes every ThreadTest picture, another
  start date every rtc picture.
- [ ] M2 the sandbox build: native == sandbox. Speed on a real game decides
  whether to go on.
- [ ] M3 GL through the bridge, rebuilt after a state load.
- [ ] M4 sound a frame at a time, input (buttons, two sticks, front and rear
  touch, motion), save data in and out.
- [ ] M5 package, gate, CI.
- [ ] M6 games.

## Decisions

### The build: Vita3K's own CMake with a headless seam (2026-10-03)

Patch 0001 adds `CHIMERA_HEADLESS`: no Qt (`qt6.cmake`, `gui-qt`), no
Discord, and no Vita3K program. In its place Vita3K's CMake adds
`CHIMERA_DRIVER_DIR`, this repository's `waterbox/native`, so the driver is
built by upstream's own target graph and links exactly the libraries
upstream's program links. `waterbox/build-native.sh` drives it with GCC (clang 20
cannot compile libstdc++ 14's `std::ranges::to`, which Vita3K uses). SDL is
a console build: no X11, Wayland or KMS (the picture is an EGL framebuffer
object).

### The driver: the session controller with a headless frame (2026-10-03)

`run-native` does what upstream's `main` does without Qt: the paths, the
config (the program's own option parser, fixed to OpenGL and never rewriting
the file), `app::init`, the libraries, the users, `install_archive` for the
`.vpk`, then `AppSessionController` - `begin_launch`, `initialize_renderer`,
`initialize_runtime`, `load_and_run`.

The window is `HeadlessFrame`: an EGL surfaceless GL 4.3 core context whose
default framebuffer is a framebuffer object of the Vita's screen size
(960x544), read back at every `swap_buffers`. The render thread takes the
context over through `make_current`; the thread that made it gives it up in
`prepare_for_render_thread`.

Everything the emulator keeps lives in a work directory made fresh for each
run (only a directory carrying run-native's mark is ever emptied), its log
included (`vita3k.log`); stdout carries only run-native's own lines.

A frame is one of the machine's vblanks (`display.vblank_count`), not a
present: the render loop presents on every pass, about every half
millisecond, picture or not, so the first measurement counted 120 "frames"
before hello_world had drawn anything. At each vblank the newest presented
picture is the frame's. Until M1 the vblank thread keeps the host clock.

### Test content: vitasdk's samples (2026-10-03)

No firmware and no game is needed to run the gate. vitasdk's prebuilt
toolchain (pinned by SHA-256) builds vitasdk's samples (CC0, pinned by
commit) into `.vpk` files: hello_world, debugscreen, ctrl, touch, rtc, audio,
SDL2's redrectangle (GXM), and our ThreadTest (`waterbox/build-testapps.sh`).

### The machine's scheduler and clock (2026-10-03)

Patch 0002. Every thread of the machine is a vsched thread (the RPCS3
core's scheduler, `vita3k/chimera`): one runs at a time, the running one
hands the machine on explicitly, and time is what the machine charges.
Upstream's sites keep their shape through `util/machine.h`: under CHIMERA
`machine::condition_variable`, `machine::thread`, `machine::clock` and
`machine::sleep_for` are the scheduler's, otherwise the standard library's.
`chimera::condition_variable` takes only virtual-time deadlines, so a host
clock left behind in a wait is a compile error; the whole conversion built
with none left.

- Threads: guest threads (no SDL thread, no semaphore hand-over: each holds
  its own copy of its parameters), the render, vblank, GXM display and
  overlay input threads.
- The CPU: dynarmic's cycle counting is on. `AddTicks` charges the
  instructions run at 1332 MHz - three cores' worth of a 444 MHz Cortex-A9
  at one instruction a cycle, one timeline for every thread - with the
  fraction of a nanosecond carried, and `GetTicksRemaining` is the thread's
  slice (100,000 instructions). A JIT out of slice gives way and carries on.
- The vblank: vblank k comes at exactly k/60 s of machine time. The
  hardware's rate is 59.94 Hz (sceDisplayGetRefreshRate says so); upstream's
  vblank thread runs at 60, and so does this one, until a game says
  otherwise.
- The render thread no longer polls (upstream waits 3 us for a command list,
  in a loop): it runs each command list as soon as it is ready and presents
  once a vblank, or at once when the game flips, waiting for a command
  list, its sync object, or 1 us past the next vblank (so the vblank thread,
  due at the same moment, always runs first).
- Time: the RTC (`rtc_ticks_since_epoch`, `rtc_base_ticks`) and every clock
  read in the modules and overlays are the machine's. The calendar starts
  at 2013-01-01 00:00:00 UTC (a setting later) and runs with machine time;
  the driver pins TZ to UTC, since Vita3K formats some dates in local time.
- Randomness: sceKernelGetRandomNumber and SceSblRng are a fixed-seed
  sequence.
- Sound: a "Chimera" audio adapter with no host device. A port keeps the
  moment its queued sound runs out, an output waits until no more than one
  buffer is queued (the hardware double-buffers), and the samples are kept
  for the frontend to take a frame at a time (M4).
- The picture: no shader-compile notices (they depend on what is cached),
  no asynchronous compiling, no shader cache, no swap interval.
- Frame f ends 2 us after vblank f (the render thread presents 1 us after
  it), and the driver is the scheduler's thread 0.

Found on the way: Vita3K ignored sceDisplaySetFrameBuf(NULL), so the display
kept reading a frame buffer the app then freed (vitasdk's debug screen does,
at exit) and the render thread crashed. A NULL frame buffer now blanks the
display, as the hardware's does. An app that exits stops the machine where
it stands (`exited=` in run-native's last line).

### The machine's filesystem (2026-10-03, M2)

The sandbox has a flat list of mounted files and lets a guest create none,
and a host disk hands the machine the host's timestamps and directory order.
So everything Vita3K keeps lives in an in-memory tree under /chimera
(`waterbox/driver/memfs.cpp`), in BOTH flavours: the Vita's partitions,
config, cache, log. It sits under the C library - open, stat, opendir,
fopen and the rest are defined there and serve every /chimera path from the
tree, passing any other path to the host - because Vita3K reaches files
through boost::filesystem, file streams, stdio, miniz and spdlog, with no
one layer of its own. A file's time is the machine's calendar, a listing is
in name order, an inode is the order files were made in. The app and the
static assets are grafted in read-only.

- libstdc++'s file streams fopen a file and then read its descriptor, so a
  stream over the tree is a fopencookie stream whose fileno is the tree's
  descriptor; natively the overrides are exported (`native/memfs.list`) so
  the shared libstdc++ reaches them.
- Trap: a distribution's GCC fortifies by default, so Boost (built by its
  own b2) calls `__open_2` and `__read_chk`, not open and read; the native
  side defines those too.

### Both builds (2026-10-03, M2)

Release with `-O2 -g` in both (Tracy only switches on in Debug and
RelWithDebInfo builds), no LTO, and FFmpeg 7.1.2 built from source for both
by `build-deps.sh` - no assembly, no threads, only the decoders Vita3K opens
(H.264, AAC, MP3, MJPEG) - in place of upstream's prebuilt download. The
guest also gets OpenSSL 3.0.15 and Boost.Filesystem from that script. Patch
0003 gates curl, nfd and X11 off and takes `CHIMERA_FFMPEG_DIR`; the one
FFmpeg-internal header Vita3K uses (codec_internal.h) still comes from
upstream's include directory. The guest toolchain disables the host's
pkg-config (SDL found dbus and ibus through it). All of Vita3K compiles for
the guest.

### The gate (2026-10-03)

`waterbox/run-gate.sh`: every test app twice, compared frame by frame, plus
the two negative controls. ThreadTest (`tests/apps/ThreadTest`, ours, CC0)
is the scheduler made visible: three workers take turns at a kernel mutex,
and the picture shows each one's share, the last 96 turns' owners, a square
on the process clock and a frame-counted background, drawn through GXM with
vita2d. vitasdk's packages (SDL2, vita2d, zlib, libpng) are a rolling
release, pinned here by SHA-256.

Trap: a dot in the work directory's name (`x.work`) makes one of Vita3K's
path checks throw (boost create_directories, "Invalid argument"); the gate
names them `<run>-work`.

## Open questions

- SDL stays linked for M0 (threads in the kernel, pads, audio). It goes in M2.
- FFmpeg is downloaded prebuilt at configure time (ffmpeg-core); M2 builds it
  from source for both flavours, as the RPCS3 and PPSSPP cores do.
- Boost's filesystem is built by Vita3K's CMake into the submodule's own tree.
- curl is fetched at configure time when the system has none (the updater
  and SceHttp); the core has no network, so it should go.
