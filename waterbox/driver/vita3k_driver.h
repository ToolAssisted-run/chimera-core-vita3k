// The core: Vita3K booting one app and running a frame at a time on the
// machine's own scheduler and clock. The same code in both flavours; the
// exports (wbx-entry.cpp) are its face.
// SPDX-License-Identifier: MIT
#pragma once

#include "bridge_frame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chimera_vita3k {

struct Options {
    uint64_t cpu_mhz = 0; // 0: the machine's (1332)
    uint64_t rtc_start = 0; // Unix seconds; 0: the machine's (2013-01-01)
    int language = 1; // SceSystemParamLang: 1 is English (United States)
    int enter_button = 1; // SceSystemParamEnterButtonAssign: 1 Cross, 0 Circle
    // Vita3K's "Enable NGS Support": off, every NGS call answers at once
    // having done nothing, and a game that mixes with NGS is silent
    bool ngs = true;
    // the gate's negative control: the GPU's pictures stay on the GPU, and a
    // state cannot hold them
    bool no_surface_sync = false;
    // how many times the Vita's 960x544 the GPU draws at (Vita3K's
    // resolution multiplier; its OpenGL renderer takes whole numbers)
    int resolution_scale = 1;
    // the gate's negative control: an HLE call costs the machine nothing
    // (vsched_hle_call), as before 2026-10-04
    bool free_hle_calls = false;
    // a zip of save data to unpack before the machine starts (savedata.h): a
    // path natively, a mounted file's name in the sandbox; empty for none
    std::string savedata;
    // a package's licence (work.bin): what decrypts a .pkg, and without it a
    // .pkg is refused. A path natively, a mounted file's name in the sandbox;
    // empty for none
    std::string licence;
    // the system software and the font package (.PUP), installed into the
    // machine before the app when present: paths natively, mounted names in
    // the sandbox
    std::vector<std::string> firmware;
};

// The controls, in the order waterbox.config declares them: the buttons as
// bits of the packed mask, the axes by index.
//
// Buttons: Up, Down, Left, Right, Cross, Circle, Square, Triangle, L, R,
// Start, Select, Front Touch, Rear Touch.
// Axes: Left Stick X/Y, Right Stick X/Y (-128..127, up and right positive);
// Front Touch X/Y, Rear Touch X/Y (0..65535 across each panel); Accel X/Y/Z
// (thousandths of a g, lying face up reads -1000 on Z); Gyro X/Y/Z (tenths
// of a degree a second).
constexpr int BUTTONS = 14;
constexpr int AXES = 14;
// The input of the next frame, in those units.
void set_input(uint64_t buttons, const int32_t axes[AXES]);

// Boot the app read from `host_name` (a path natively, a mounted file's name
// in the sandbox). This thread becomes the machine's thread 0.
bool boot(const std::string &host_name, const Options &options, BridgeFrame &frame, std::string &error);

// Turbo: whether each present is read back into the picture. The drawing
// goes on either way - with surface sync it is part of the machine.
void set_rendering(bool on);

// One frame: frame f ends 2 us after vblank f, at f/60 s of machine time.
// The touch panels and motion sensors are sampled as it starts, so the whole
// frame reads the frame's input. After the app exits, nothing runs and the
// frame count stands still.
void frame();

// The frame's sound: 48 kHz stereo, interleaved; 800 pairs a frame.
const int16_t *audio(int &pairs);
// Whether the machine read any input in the frame.
bool input_was_read();
// The save data the machine keeps now (savedata.h), named as it is taken in:
// a snapshot, and its files by index.
size_t savedata_snapshot();
const std::string &savedata_name(size_t i);
const std::vector<uint8_t> &savedata_bytes(size_t i);

uint64_t frames();
uint64_t exited_at(); // the frame the app exited in, or 0
uint64_t time_ns();
uint64_t switches();
uint64_t yields();

// The machine's memory as an address space, for the frontend's RAM tools:
// the Vita's 4 GiB, resolved a page at a time. A page the machine has not
// allocated reads 0 and takes no write.
constexpr uint64_t BUS_SIZE = 1ull << 32;
bool bus_ready();
uint8_t bus_peek(uint32_t addr);
void bus_poke(uint32_t addr, uint8_t value);
void bus_read(uint64_t addr, uint8_t *out, size_t len);

// The machine's log so far.
bool log(std::vector<uint8_t> &out);

} // namespace chimera_vita3k
