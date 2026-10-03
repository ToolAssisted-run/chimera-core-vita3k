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
    // the gate's negative control: the GPU's pictures stay on the GPU, and a
    // state cannot hold them
    bool no_surface_sync = false;
};

// Boot the app read from `host_name` (a path natively, a mounted file's name
// in the sandbox). This thread becomes the machine's thread 0.
bool boot(const std::string &host_name, const Options &options, BridgeFrame &frame, std::string &error);

// One frame: frame f ends 2 us after vblank f, at f/60 s of machine time.
// After the app exits, nothing runs and the frame count stands still.
void frame();

uint64_t frames();
uint64_t exited_at(); // the frame the app exited in, or 0
uint64_t time_ns();
uint64_t switches();

// The machine's log so far.
bool log(std::vector<uint8_t> &out);

} // namespace chimera_vita3k
