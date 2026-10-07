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
  never reads GPU results. (M3 turns surface sync on: it is what lets a state
  hold the pictures.)
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
- [x] M2 the sandbox build (2026-10-03): core.wbx runs all eight test apps
  in miniBox and its lines equal the native reference's in every frame -
  pictures, machine time and thread switches (ThreadTest's 77,087 included).
  Speed on homebrew: ThreadTest's 600 frames in 12.6 s native, 13.0 s
  sandboxed (3.5% apart). Speed on a real game still decides whether to go on
  (no game here yet).
- [x] M3 GL through the bridge, rebuilt after a state load (2026-10-03):
  ThreadTest with a state saved and loaded before every frame, and ThreadTest
  saved at frame 150 and carried on in another process, both print what one
  native run printed, pictures included. A state is 315 MiB (homebrew, no
  game yet). Negative control: without surface sync a load loses the
  pictures.
- [x] M4 sound a frame at a time, input (buttons, two sticks, front and rear
  touch, motion), save data in and out (2026-10-03): InputTest reads exactly
  the input an independent oracle predicts from its script, in all 299
  frames (both kinds of pad read, both sticks, both panels with their touch
  ids, the accelerometer, the gyroscope); AudioTest's two ports come out as
  played (left: 1000 Hz at 8000.0 and 250 Hz at 3998.8; right: 250 Hz at
  1999.4, at half volume), 800 pairs a frame; save data exports and comes
  back in. All of it native == sandbox, and under a load every frame.
  Negative controls: the script one frame late, and one sample taken out of
  the sound, both fail their oracles.
- [x] M5 package, gate, CI (2026-10-03): vita3k.chimeraCore builds
  deterministically (26 licence components), declares what the core reads (a
  gate leg checks it, and sees a swap), and runs in Chimera's own engine:
  InputTest driven by a movie through chimera-run reads exactly the input
  the oracle predicts. Turbo leaves the machine untouched. The CI workflow
  is written and untested (there is no GitHub repository yet).
- [ ] M6 games. Begun 2026-10-03: Alien Shooter (PCSE00445, Playable on
  Vita3K's list) runs with the font package alone - its publishers' logos,
  its music - and is the same native and sandboxed in every digest line of
  900 frames, once miniBox's thread-local storage was fixed (below). About
  2.5 frames a second on llvmpipe; a real GPU is unmeasured. Alien Breed
  (PCSE00210, Ingame) asks for the network at boot and threads heavily
  (about 1500 switches a frame); with no network (patch 0008) and miniBox's
  fault handler fixed (below) it reaches its developer's logo and is the
  same native and sandboxed in 600 frames (36 s sandboxed).
  2026-10-04: a system call costs machine time (patch 0009, below), and
  Alien Shooter went from 3 frames a second to about 30 on its logos and 18
  on its main menu (llvmpipe), which it now reaches in 45 s.

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
  for the frontend to take a frame at a time (M4). (Found in M4: the adapter
  never set a port's buffer length, so until then an output neither waited
  nor kept anything. See the M4 entry.)
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

### One core, two runners (2026-10-03, M2)

`waterbox/driver` is the core, the same objects in both flavours:
`vita3k_driver` (boot and the frame, moved out of run-native), `bridge_frame`
(the FrameHost), `wbx-entry` (the exports), the machine's filesystem and the
built-in assets. run-native calls the exports directly and run-wbx through
miniBox, both through `gate-harness.h`, so they print the same lines from the
same loop. The app comes in as the file `rom.name` names, the settings as the
JSON in `settings`: mounted files in the sandbox, files in the work directory
natively.

- GL goes through the GPU bridge in BOTH flavours (miniBox's generated
  wrappers; natively the host half's dispatcher in-process), so both issue
  the same GL stream. The frame host's get_proc_address is the bridge's
  lookup, and `gen-gl.sh` takes the install step's assignments to glad's
  pointers out: Vita3K's glad (1) and the host half's (2) define the same
  glad_gl* globals, so natively the host half is one relocatable object with
  only chimera_gl_host_* left global.
- Natively the GL context is a real EGL context held by one thread at a
  time: the frame host releases it in prepare_for_render_thread and the
  render thread binds it in make_current (SetGlThreadHooks). In the sandbox
  every guest thread is one host thread.
- Vita3K's static assets (data/, shaders-builtin/, 165 KB) are built into the
  core (`gen-assets.py`) and grafted into memfs.

### The sandbox, case by case (2026-10-03, M2)

Patch 0004 and the guest-only `guest-libc.cpp`:
- dynarmic's POSIX exception handler installs a signal stack (sigaltstack,
  which miniBox does not provide) for fastmem: both flavours build its
  generic handler instead, swapped into the target from Vita3K's CMake, and
  the page table does the guest's addressing in both (late_init).
- Vita3K's write tracking (add_protect) still faults: in the sandbox
  register_access_violation_handler only keeps the handler, and the
  GuestFaultHandler export (miniBox calls it on the faulting thread) runs it.
- The 4 GiB reservation's address hint (16 GiB) is outside miniBox's arena,
  which refuses it: no hint in the guest.
- The log is synchronous, with no duplicate filter: spdlog's async worker is
  a thread outside the machine's scheduler (in the sandbox it never ran) and
  the filter drops repeats by host seconds - both would make the machine's
  filesystem depend on the host.
- uname (Boost.Filesystem asks at start-up): a fixed answer.
- libstdc++'s file streams write with writev: memfs serves writev and readv.
- zlib for the guest from psvpfstools' copy: its zRIF code calls zlib
  without linking it (natively the system's libz fills in).
- Boost for the guest is built without statx, sendfile and copy_file_range
  (system calls memfs would never see).

### The gate (2026-10-03)

`waterbox/run-gate.sh`: every test app twice natively and once in the
sandbox, compared frame by frame (picture, machine time, each frame's sound
and whether it read input); ThreadTest's two state legs (M3) against the
native run; InputTest and AudioTest against oracles that predict what they
read and play, plain and under a load every frame, and the save data round
trip (M4); and five negative controls. InputTest (CC0) writes down what it
reads into ux0:data, which the save data export brings out; AudioTest (CC0)
plays a known tone on each of two ports. ThreadTest (`tests/apps/ThreadTest`, ours, CC0)
is the scheduler made visible: three workers take turns at a kernel mutex,
and the picture shows each one's share, the last 96 turns' owners, a square
on the process clock and a frame-counted background, drawn through GXM with
vita2d. vitasdk's packages (SDL2, vita2d, zlib, libpng) are a rolling
release, pinned here by SHA-256.

Trap: a dot in the work directory's name (`x.work`) makes one of Vita3K's
path checks throw (boost create_directories, "Invalid argument"); the gate
names them `<run>-work`.

### States: the GL rebuild, and the pictures in memory (2026-10-03, M3)

run-wbx seals the machine after Init and takes `--rerecord` (a state saved
and loaded before every frame, one process), `--save-state F` and `--state F`
(another process carries on). After every load it mints a new GL context id,
as Chimera's session does (`ce_gl_state_loaded`).

Patch 0005: the GL renderer compares the frame host's context id with the
one its objects were made in and, when it moved, rebuilds. First every GL
object it remembers is dropped - the program and shader caches, the texture
and surface caches, the screen and overlay renderers, each context's vertex
array and six ring buffers, each render target's attachments, and the frame
host's framebuffer - deleting only names this machine remembers, before a
single new name is made, so a delete never lands on a name a new object was
given (the RPCS3 core's #43 trap). Sync objects are forgotten, not deleted:
in another process a remembered GLsync is a pointer that driver never made.
Then everything is made again, in the order the machine's own allocator
decides. Contexts and render targets are found through live sets their
constructors and destructors keep.

- Where it asks: the render thread blocks only waiting for a command list,
  for a sync object, or for the next vblank, and those waits are where a
  state is saved - so a load wakes it inside its loop. It asks before every
  command list and before every present. The first version asked at the top
  of the loop: after a load in another process the thread first finished the
  present it was waiting to make, with the old context's names, and frame 61
  read back black.
- The pictures: Vita3K keeps what the GPU draws on the GPU (surface sync is
  off upstream), so after a load the surfaces were gone and the display,
  falling back to memory, showed memory the GPU never wrote. A double-
  buffered app draws in one frame and flips in the next, so a load before
  every frame froze ThreadTest's picture for good (101 of 120 frames wrong).
  With surface sync on, every scene the GPU finishes is written back to the
  machine's memory, as the Vita's GPU writes to its own: a state then holds
  every picture, the rebuilt renderer finds them there, and both state legs
  match native exactly. It costs nothing measurable on homebrew (ThreadTest
  120 frames in 3.5 s either way) and changes no picture of an unbroken run.
  The core turns it on for the machine; the setting `no_surface_sync` exists
  only for the gate's negative control.
- What it does not cover: Vita3K's GL backend refuses surface sync at a
  non-integer resolution multiplier, and the core runs at 1x. Speed with
  surface sync on a real game is unmeasured (M6).

### The frame's input, sound and save data (2026-10-03, M4)

Patch 0006 and `waterbox/driver` (`set_input`, `savedata.cpp`).

- The controls: Up, Down, Left, Right, Cross, Circle, Square, Triangle, L,
  R, Start, Select, Front Touch and Rear Touch as buttons; both sticks
  (-128..127, up and right positive, as the PSP and 3DS cores have them),
  each touch panel's point (0..65535 across it), the accelerometer
  (thousandths of a g; lying face up reads -1000 on Z) and the gyroscope
  (tenths of a degree a second, to +-18000: Vita3K's limit is 5 revolutions
  a second) as axes. The core turns them into the Vita's units: sticks
  0..255 centred on 128 (Y down), the front panel 1920x1088 points, the rear
  1920 across and 108..889 down, L/R as L1/R1 to the 2/Ext2 reads.
- One place holds the frame's input (`chimera/input.h`). The pad reads it
  live; the touch panels and motion sensors are sampled as the frame
  STARTS, by the core, instead of at the vblank inside it - otherwise a
  frame's touches would reach the game a frame late. The system dialogs
  Vita3K draws itself (save data, messages) read the same pad and the front
  panel. No host keyboard, gamepad or sensor reaches the machine.
- Motion: Vita3K makes gyro readings up from the accelerometer until a
  device reports a non-zero gyro (for host devices that have none). The
  machine always has one: at rest it reads zero, so nothing is made up.
- InputWasRead: set by any read of the pad, a panel, the sensors or a
  dialog's input.
- Sound: every port is mixed into one 48 kHz stereo stream, each buffer
  placed at the machine time it plays. A port keeps an exact clock of the
  samples queued on it (a run starts again where a port that ran dry is fed),
  BGM and voice ports at other rates are resampled linearly, and the port's
  left and right volumes apply; Vita3K's global volume does not (the
  frontend owns the volume). All integer arithmetic, so both flavours agree
  to the sample. The core takes the mix up to each frame's end: 800 pairs.
- Save data lives where the Vita keeps it, in the machine's filesystem:
  ux0:user/<id>/savedata (the save data system's) and ux0:data (where
  homebrew keeps its). It goes out as savedata/<TITLE ID>/... and data/...
  (the frontend's Export Save Data) and comes back in under the same names,
  from a zip mounted as "savedata", unpacked before the app starts and so
  before the machine is sealed. A leading ux0:/ and user/<id>/ are taken
  off, and a bare <TITLE ID>/ folder is a title's save; anything else
  refuses the zip. The user the save belongs to is made first, as the
  launch would a moment later.
- Not covered: the PS TV's controls (L2/R2/L3/R3 and more pads), the PS
  button, a second finger on a panel, the microphone and the camera.

### The package (2026-10-03, M5)

- systemId PSV. The controls, sound and states as M3/M4 made them;
  `gpuStatesSurviveTheContext` (the fresh-process state leg proves it). Not
  `drawEveryFrame`: with surface sync the drawing is machine state, so turbo
  (`SetRenderingEnabled`) skips only the readback of each present and the
  GPU goes on drawing; the gate's turbo leg leaves the machine identical in
  every frame and the pictures outside the window.
- Settings: System Software (full / fonts / none), System Language (the
  Vita's twenty), Enter Button (cross / circle), Clock at Power-On, CPU
  Clock. Language and enter button are the system parameters games read
  (sceAppUtilSystemParamGetInt); InputTest prints them, and a leg sees them
  arrive.
- Firmware: the system software (PSVUPDAT.PUP) and the font package
  (PSP2UPDAT.PUP), required as System Software says, installed with Vita3K's
  own install_pup into the machine's filesystem before the game, so before
  the seal. The two are easy to swap by name, so each must install its own
  partition (vs0, sa0) or the load says which it looks like.
- Slots: the game (.vpk, or a NoNpDRM .zip, which Vita3K decrypts with its
  zRIF) and the save data. The core reads the project's slot map, as
  touchHLE's does, and takes a slot's file under either spelling of its
  mount ("name" or "/name").
- Memory: no domain (the game allocates its own as it runs); one bus over
  the Vita's 4 GiB, a page at a time, with ReadBus for runs.
- Found on the way, all in the core's own filesystem or log: boost's
  remove_all opens a directory and hands the descriptor to fdopendir, which
  must keep it (it closed it); libstdc++'s copy_file copies through
  sendfile/copy_file_range and advises with posix_fadvise, which the sandbox
  does not have; the log carried the host's time natively; and the PFS
  decryption talks on stdout, which now goes to the log.

### miniBox: thread locals had no room (2026-10-03, M6)

Alien Shooter's start-up picture came out upside down in the sandbox and
upright natively: stb_image keeps its flip-on-load switch in thread locals,
and on the render thread a zeroed thread_local read garbage. The waterbox
musl starts with an empty auxiliary vector, so __init_tls never saw the
program's PT_TLS and reserved no room for thread locals: on a new thread
they lay on the top of its own stack, on the main thread on the static data
before musl's builtin TLS. Fixed in miniBox (a1c7a42, branch guest-tls-phdr,
local): its start-up hands musl the PT_TLS header, found through
__ehdr_start, and nothing else; a test in miniBox's threads suite, negative-
controlled. Every core that uses thread locals changes when rebuilt against
it.

### The machine has no network (2026-10-03, M6)

Patch 0008. Alien Breed died at boot asking the sandbox for a socket: Vita3K
reached the host's network from several places - every game socket was a
host socket, the connection state said "IP obtained", the interface list
was the host's (getifaddrs, over a netlink socket), the resolver asked the
host's name servers, HTTP opened host sockets, and the system's user name
was the host's name. Under CHIMERA: a game socket is made, as a Vita out of
range makes one, with nothing behind it (sends, receives and connects say
the network is down or unreachable); the state is disconnected and the
connection info "not connected"; the interfaces are the loopback alone; the
resolver times out; HTTP finds no name server; the MAC address is a fixed,
locally administered one; the user name is the machine's user.

### miniBox: the guest's fault handler ran on the host's %fs (2026-10-03, M6)

Vita3K's write tracking unprotects pages from GuestFaultHandler, which
miniBox calls on the faulting thread after putting the host's %fs back:
with thread locals real (above), Alien Breed's first watched write ran the
handler on the host's thread pointer and its mprotect was refused
("something outside a fault took it"). miniBox 4852e35 (same local branch)
runs the handler under the base the guest faulted on; a test in the threads
suite, negative-controlled.

### A system call costs machine time (2026-10-04, M6)

Patch 0009, `vsched_hle_call` (user-decided, 2026-10-04).

- What was slow: Alien Shooter ran at 3 frames a second, on llvmpipe and on
  the 1060 alike. One of its threads polls a system function in a loop,
  `while (flag) sceNpCheckCallback();` (ARM code at 0x81006cb4), waiting for
  a sign-in its callback never reports. Machine time moved only with the
  instructions the JIT ran, and an HLE call cost none, so one second of
  machine time took 48 million calls of about 27 instructions each: the CPU
  was 100% busy in that loop, at a microsecond or more of host time a call.
  Upstream never sees it: there the poller only keeps a host core busy.
- The rule: every HLE call (`call_import`) costs 1332 instructions, one
  microsecond at the machine's rate, about what a system call costs the
  console. It is charged as instructions, so it counts against the thread's
  slice too. Charged as time alone (tried), the poller kept 3.7 ms turns and
  starved the loading thread: the game stopped at its first picture.
- Measured on llvmpipe: the publishers' logos went from 3 to about 30 frames
  a second, and the main menu (frame 1290) runs at about 18. Five
  microseconds a call was faster again (85 and 50), but loading then took
  longer in machine time; the pictures come in the same order either way.
- What it costs: every call pays it, a lightweight mutex too, which the
  console takes in userland in tens of nanoseconds. Alien Shooter's boot
  makes about 90,000 of those a second, so 18% of its machine time goes to
  them. Exempting the userland fast paths is open.
- Found on the way, in the same patch: a thread handed the machine back to
  itself (no other thread could run) kept an empty slice, so it was stopped
  after every block to look for another thread - about seven times per HLE
  call, a quarter of the host's time. A thread handed the machine now gets a
  fresh slice, cut short at the next timed waiter's deadline; without the
  cut, a thread sleeping while another spins woke up to 53 us late.
- And a thread whose deadline passes runs next (the earliest deadline,
  then the longest wait), as a timer interrupt would have it, not when round
  robin reaches it. With wake-ups on time this mattered at once: a frame ends
  2 us after its vblank, and round robin let other threads, then the frame's
  end, run before the render thread woken at vblank, so about one in seven
  of ThreadTest's frames had no present of their own and repeated the last
  picture. The gate's turbo leg caught it: after a turbo window that frame
  shows the picture from before the window. Now every frame has its
  vblank's present (ThreadTest: one or two in each of 300 frames; the seven
  repeated pictures are frames where it did not flip).
- Gate: HleTest (CHMR00004, tests/apps) times 10000 calls with the
  machine's own clock: 10038 us, and 37 us with `--free-hle-calls` (the
  control); the two differ by exactly the 10000 calls and the closing clock
  read, a microsecond each. Its sleeper, 50 sleeps of 100 us while the main
  thread spins, ends each at most 2 us late (its own two calls), and the run
  gives way 234 times. Negative controls: the old slice gives way 306,817
  times; a fresh slice without the deadline cut wakes the sleeper 53 us late.
- A machine change: every game's timing moves, so a movie made on an earlier
  build does not replay on this one (a project pins its core).

### Internal Resolution (chimera#200, 2026-10-07)

A setting, `internal_resolution`: 1x, 2x, 3x, 4x. Vita3K's renderer has the
multiplier already (its OpenGL one takes whole numbers) and its surface sync
knows about it; what was missing was this side. The driver sets
`resolution_multiplier` and makes the frame's framebuffer N times 960x544,
the readback and `GetVideoWidth/Height` follow, and the declaration's
`video.width/height` are now the most the picture can be (3840x2176) with
the shape left at 960x544. Touch is in panel units and does not change.

What it does, measured:

- The picture is DRAWN bigger. ResTest (CHMR00005, tests/apps) is slopes -
  eight lines, a disc, a tilted triangle, four colours, no antialiasing -
  and a slope's steps are the grid's. At 2x, 3815 of the 2x2 blocks of the
  picture are more than one colour (finer steps), every other block is the
  1x picture's colour there, and no colour is new. `tests/check-resolution.py`
  holds a picture to that, and three stretched pictures fail it: the 1x one
  with pixels repeated (no finer steps - the control the script runs itself),
  a bilinear stretch (1187 new colours), another app's picture.
- The machine, on what is here, does not change: Alien Breed for 1500 frames
  and Alien Shooter for 300 give the same time, thread switches and sound at
  2x as at 1x. Both draw sprites; a picture made for 960x544 is only
  enlarged.
- It is still a setting of the MACHINE and not of the display (chimera's
  docs/graphics-settings.md, bin C). Surface sync writes what the GPU
  finishes back into the Vita's memory at the Vita's size, and at 2x that is
  every second pixel of the bigger picture, not what a 1x render leaves.
  Those bytes differ and a game that reads its picture back can tell. Not
  measured: the harness digests no memory. So it is chosen with the project.
- States hold the machine, and not the bigger picture. What a state has of
  a picture is what the machine has: the Vita's 960x544, written to its
  memory by surface sync. ThreadTest at 2x with a state saved and loaded
  before every frame is the native 2x run's machine in every frame (time,
  sound, input), and every picture it shows is 1920x1088 - but not the
  native run's picture: the frame after a load shows the buffer that was
  finished BEFORE it, rebuilt from memory and enlarged, where the unbroken
  run shows the drawn one. The first version of the gate's leg expected the
  pictures to match and failed; the leg now holds what is true. In use: a
  seek shows a soft picture for the frame after its load and drawn ones
  from then on, and a movie played from power-on is drawn throughout. The
  same cost Dolphin's Internal Resolution has, said in the setting's text.
  Native == sandbox at 2x in every frame.
- Cost: the GPU's, with the pixels.

### A game as a package (chimera#203, 2026-10-07)

The Game slot takes a `.pkg`, the form the PlayStation Store sends a game
in, and a new Licence slot (shown when the game is a `.pkg`) takes the
game's `work.bin` or `.rif`. A file is a package by its two headers, not its
name. Vita3K's `install_pkg` wants the licence as a zRIF string and its
`find_pkg_zrif` looks for the `.rif` in the machine, at
`ux0:license/<TITLE ID>/<content id>.rif`; the driver puts the project's
file there, under the package's own content id whatever it was called, and
asks. Converting it prints the licence as text on stdout: that output is
thrown away, not logged.

Refused by name: a package with no licence; a licence that is not one; a
package that does not install (damaged, or another game's licence); a
package that is not a game (add-on content, a patch, a theme - these belong
to a game, and nothing here installs them yet).

Proof, and its limit. There is no bought package here. A NoNpDRM dump is
what a package unpacks to, still encrypted as the console keeps it, plus the
licence; `tests/make-pkg.py` puts a dump back into the container
`install_pkg` reads (outer headers, three info blocks, the item table, names
and data under AES-128-CTR with Vita3K's own key, read from its header) and
takes the licence out. Alien Shooter packed that way and given its licence
starts the machine the dump starts: every line of 300 frames, native and
sandbox. With Alien Breed's licence it installs nothing. What this does NOT
show is a package as Sony made it - the container here is ours, checked
only by Vita3K's reader. The reporter has real ones.

## Open questions

- SDL stays linked (threads in the kernel, pads, audio): M4 decides whether
  input and sound still need it.
- A game's speed with every guest thread on one host core, and with surface
  sync on, on a real GPU (M6).
- psvpfstools states no licence (Vita3K ships it in every release); it is
  what decrypts NoNpDRM dumps. Publishing the package is the owner's call.
- Publishing also needs miniBox's thread-local fix pushed, or the package CI
  builds has the bug.
