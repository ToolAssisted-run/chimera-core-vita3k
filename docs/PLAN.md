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
- [ ] M1 virtual clock + scheduler: two native runs are identical.
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
commit) into `.vpk` files: hello_world, debugscreen, ctrl, touch, rtc, audio
(`waterbox/build-testapps.sh`).

## Open questions

- SDL stays linked for M0 (threads in the kernel, pads, audio). It goes in M2.
- FFmpeg is downloaded prebuilt at configure time (ffmpeg-core); M2 builds it
  from source for both flavours, as the RPCS3 and PPSSPP cores do.
- Boost's filesystem is built by Vita3K's CMake into the submodule's own tree.
- curl is fetched at configure time when the system has none (the updater
  and SceHttp); the core has no network, so it should go.
