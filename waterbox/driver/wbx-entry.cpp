// The core's exports: what the frontend (and both runners of the gate) call.
// The same file in both flavours: natively they are called directly, in the
// sandbox through miniBox. A project names its files in "slots" ({"game":
// ["x.vpk"], "savedata": ["y.zip"]}, each mounted under that name); a file
// opened on its own arrives as what "rom.name" names. The settings are the
// JSON object in "settings". Mounted files in the sandbox, files in the
// working directory natively.
// SPDX-License-Identifier: MIT
#include "vita3k_driver.h"
#include "memfs.h"

#include "gl-bridge.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef CHIMERA_GUEST
#include <emulibc.h>
#else
#define ECL_EXPORT __attribute__((visibility("default")))
#define ECL_INVISIBLE
#endif

namespace {

constexpr int SCREEN_W = 960, SCREEN_H = 544;
// Internal Resolution: the picture is this many screens wide and high
constexpr int MAX_SCALE = 4;
int g_scale = 1;

chimera_vita3k::BridgeFrame g_frame(SCREEN_W, SCREEN_H);
std::string g_error;
std::vector<uint8_t> g_video(SCREEN_W * SCREEN_H * 4);
std::vector<uint8_t> g_log;
int32_t g_axes[chimera_vita3k::AXES] = { 0, 0, 0, 0, 32768, 32768, 32768, 32768, 0, 0, -1000, 0, 0, 0 };

std::string read_file(const char *name) {
    std::string out;
    if (FILE *f = fopen(name, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            out.append(buf, n);
        fclose(f);
    }
    return out;
}

// The string value of a key in a flat JSON object, or "".
std::string json_string(const std::string &json, const char *key) {
    const std::string k = std::string("\"") + key + "\"";
    const auto at = json.find(k);
    if (at == std::string::npos)
        return "";
    size_t i = json.find(':', at + k.size());
    if (i == std::string::npos)
        return "";
    i = json.find('"', i);
    if (i == std::string::npos)
        return "";
    const size_t end = json.find('"', i + 1);
    return end == std::string::npos ? "" : json.substr(i + 1, end - i - 1);
}

// The first name in a slot of the project's slot map, or "".
std::string slot_first(const std::string &slots, const char *id) {
    const std::string k = std::string("\"") + id + "\"";
    const auto at = slots.find(k);
    if (at == std::string::npos)
        return "";
    size_t i = slots.find('[', at + k.size());
    if (i == std::string::npos)
        return "";
    i = slots.find_first_not_of(" \t\r\n", i + 1);
    if (i == std::string::npos || slots[i] != '"')
        return "";
    std::string out;
    for (size_t j = i + 1; j < slots.size() && slots[j] != '"'; j++) {
        if (slots[j] == '\\' && j + 1 < slots.size())
            j++;
        out += slots[j];
    }
    return out;
}

// A mounted file's name as it opens: a project mounts a slot's file under
// the name the map gives, a file opened on its own sits at "/<name>" too.
std::string mounted(const std::string &name) {
    if (name.empty() || name[0] == '/')
        return name;
    if (FILE *f = fopen(name.c_str(), "rb")) {
        fclose(f);
        return name;
    }
    if (FILE *f = fopen(("/" + name).c_str(), "rb")) {
        fclose(f);
        return "/" + name;
    }
    return name;
}

// the settings' option names, in the order of the Vita's own values
constexpr const char *LANGUAGES[] = { "japanese", "english-us", "french", "spanish", "german", "italian", "dutch",
    "portuguese-pt", "russian", "korean", "chinese-traditional", "chinese-simplified", "finnish", "swedish",
    "danish", "norwegian", "polish", "portuguese-br", "english-gb", "turkish" };

// A number from a flat JSON object ("key": 123 or "key": "123"), or 0.
uint64_t json_number(const std::string &json, const char *key) {
    const std::string k = std::string("\"") + key + "\"";
    const auto at = json.find(k);
    if (at == std::string::npos)
        return 0;
    size_t i = json.find(':', at + k.size());
    if (i == std::string::npos)
        return 0;
    i++;
    while (i < json.size() && (json[i] == ' ' || json[i] == '"'))
        i++;
    return strtoull(json.c_str() + i, nullptr, 10);
}

} // namespace

extern "C" {

ECL_EXPORT const char *GetLoadError(void) {
    return g_error.c_str();
}

// The GPU bridge: the host's dispatcher, handed over before Init. Vita3K
// draws with OpenGL and nothing else, so without it there is no machine.
bool g_bridge = false;
ECL_EXPORT void SetGpuBridge(uint64_t addr) {
    g_bridge = chimera_gl_install(reinterpret_cast<chimera_gl_bridge_fn>(static_cast<uintptr_t>(addr)));
}

#ifdef CHIMERA_GUEST
extern "C" bool chimera_mem_access_violation(uint8_t *addr, bool write);

// miniBox calls this on the faulting thread when a guest instruction hits a
// page this guest protected (Vita3K's write tracking); nonzero: retry.
ECL_EXPORT int GuestFaultHandler(uint64_t addr, uint64_t is_write) {
    return chimera_mem_access_violation(reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(addr)), is_write != 0) ? 1 : 0;
}
#endif

#ifndef CHIMERA_GUEST
// Natively the GL context is a real one held by one thread at a time.
ECL_EXPORT void SetGlThreadHooks(void (*bind)(), void (*release)()) {
    g_frame.bind_current = bind;
    g_frame.release_current = release;
}
#endif

ECL_EXPORT int Init(void) {
    const std::string slots = read_file("slots");
    std::string name = mounted(slot_first(slots, "game"));
    if (name.empty()) {
        name = read_file("rom.name");
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            name.pop_back();
    }
    if (name.empty()) {
        g_error = "no game: neither a \"game\" slot nor rom.name";
        return 0;
    }
    const std::string settings = read_file("settings");
    // The renderer: Vita3K's OpenGL one on the machine's GPU is the only one
    // (it has no software renderer), named "-hw" so a frontend knows to hand
    // the GPU over.
    const std::string renderer = json_string(settings, "renderer");
    if (!renderer.empty() && renderer != "opengl-hw") {
        g_error = "no such renderer: " + renderer;
        return 0;
    }
    if (!g_bridge) {
        g_error = "Vita3K draws with OpenGL on the GPU, and no GPU was handed over (the renderer setting is opengl-hw; "
                  "this machine may have no OpenGL context to give)";
        return 0;
    }
    chimera_vita3k::Options options;
    // "2x": the GPU draws, and the frontend is handed, 1920x1088. Read before
    // the first GL call, because the framebuffer is made at this size.
    const std::string resolution = json_string(settings, "internal_resolution");
    if (!resolution.empty()) {
        const int scale = resolution.size() == 2 && resolution[1] == 'x' ? resolution[0] - '0' : 0;
        if (scale < 1 || scale > MAX_SCALE) {
            g_error = "no such internal resolution: " + resolution;
            return 0;
        }
        g_scale = scale;
    }
    if (!g_frame.resize(SCREEN_W * g_scale, SCREEN_H * g_scale)) {
        g_error = "the picture's size cannot change once the machine has drawn";
        return 0;
    }
    g_video.assign(static_cast<size_t>(SCREEN_W) * g_scale * SCREEN_H * g_scale * 4, 0);
    options.resolution_scale = g_scale;
    options.cpu_mhz = json_number(settings, "cpu_mhz");
    options.rtc_start = json_number(settings, "rtc_start");
    options.no_surface_sync = json_number(settings, "no_surface_sync") != 0;
    options.free_hle_calls = json_number(settings, "free_hle_calls") != 0;
    const std::string language = json_string(settings, "language");
    if (!language.empty()) {
        options.language = -1;
        for (int i = 0; i < static_cast<int>(sizeof LANGUAGES / sizeof *LANGUAGES); i++)
            if (language == LANGUAGES[i])
                options.language = i;
        if (options.language < 0) {
            g_error = "no such language: " + language;
            return 0;
        }
    }
    const std::string enter = json_string(settings, "enter_button");
    if (!enter.empty()) {
        if (enter != "cross" && enter != "circle") {
            g_error = "no such enter button: " + enter;
            return 0;
        }
        options.enter_button = enter == "cross" ? 1 : 0;
    }
    options.savedata = mounted(slot_first(slots, "savedata"));
    options.licence = mounted(slot_first(slots, "licence"));
    for (const char *id : { "PSVUPDAT.PUP", "PSP2UPDAT.PUP" })
        if (FILE *f = fopen(id, "rb")) {
            fclose(f);
            options.firmware.push_back(id);
        }
    return chimera_vita3k::boot(name, options, g_frame, g_error) ? 1 : 0;
}

// Turbo (the frontend's fast-forward and seeks): off, a present is not read
// back. Nothing else changes: the GPU still draws, because what it draws is
// written to the machine's memory (surface sync) and so is part of it.
ECL_EXPORT void SetRenderingEnabled(int on) {
    chimera_vita3k::set_rendering(on != 0);
}

// An axis of the next frame (the frontend sets every one before each frame).
ECL_EXPORT void SetAxis(int32_t index, int32_t value) {
    if (index >= 0 && index < chimera_vita3k::AXES)
        g_axes[index] = value;
}

ECL_EXPORT void FrameAdvance(uint64_t buttons) {
    chimera_vita3k::set_input(buttons, g_axes);
    chimera_vita3k::frame();
    const auto &picture = g_frame.picture();
    if (picture.size() == g_video.size())
        memcpy(g_video.data(), picture.data(), g_video.size());
}

ECL_EXPORT uint32_t *GetVideoBgra(void) {
    return reinterpret_cast<uint32_t *>(g_video.data());
}
ECL_EXPORT int GetVideoWidth(void) {
    return SCREEN_W * g_scale;
}
ECL_EXPORT int GetVideoHeight(void) {
    return SCREEN_H * g_scale;
}

ECL_EXPORT int16_t *GetAudio(void) {
    int pairs;
    return const_cast<int16_t *>(chimera_vita3k::audio(pairs));
}
ECL_EXPORT int GetAudioSampleCount(void) {
    int pairs;
    chimera_vita3k::audio(pairs);
    return pairs;
}

ECL_EXPORT int InputWasRead(void) {
    return chimera_vita3k::input_was_read() ? 1 : 0;
}

// Save data out (the frontend's Export Save Data, docs/save-data.md): a
// snapshot of what the machine keeps, then each file by index.
ECL_EXPORT int32_t GetSaveDataFileCount(void) {
    return static_cast<int32_t>(chimera_vita3k::savedata_snapshot());
}
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) {
    return chimera_vita3k::savedata_name(static_cast<size_t>(i)).c_str();
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) {
    return static_cast<int64_t>(chimera_vita3k::savedata_bytes(static_cast<size_t>(i)).size());
}
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) {
    return chimera_vita3k::savedata_bytes(static_cast<size_t>(i)).data();
}

// No memory domain: the Vita's memory is not one block whose address and size
// hold for a session (the game allocates its own as it runs), so the RAM tools
// get a bus instead, resolved per access in the machine's own addresses.
ECL_EXPORT int GetMemoryDomainCount(void) {
    return 0;
}
ECL_EXPORT const char *GetMemoryDomainName(int32_t) {
    return "";
}
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int32_t) {
    return nullptr;
}
ECL_EXPORT int64_t GetMemoryDomainSize(int32_t) {
    return 0;
}
ECL_EXPORT int GetMemoryDomainWritable(int32_t) {
    return 0;
}

ECL_EXPORT int32_t GetBusCount(void) {
    return chimera_vita3k::bus_ready() ? 1 : 0;
}
ECL_EXPORT const char *GetBusName(int32_t) {
    return "Memory";
}
ECL_EXPORT int64_t GetBusSize(int32_t) {
    return static_cast<int64_t>(chimera_vita3k::BUS_SIZE);
}
ECL_EXPORT int32_t GetBusWritable(int32_t) {
    return 1;
}
ECL_EXPORT int32_t PeekBus(int32_t bus, int32_t addr) {
    return bus == 0 ? chimera_vita3k::bus_peek(static_cast<uint32_t>(addr)) : 0;
}
ECL_EXPORT void PokeBus(int32_t bus, int32_t addr, int32_t value) {
    if (bus == 0)
        chimera_vita3k::bus_poke(static_cast<uint32_t>(addr), static_cast<uint8_t>(value));
}
// A run of the bus at once (up to 64 KiB, one call): what PeekBus answers byte
// by byte. The buffer is invisible - what a tool read is no part of the
// machine.
namespace {
ECL_INVISIBLE uint8_t g_bus_run[65536];
}
ECL_EXPORT const uint8_t *ReadBus(int32_t bus, int64_t addr, int32_t len) {
    if (len < 0)
        len = 0;
    if (len > static_cast<int32_t>(sizeof g_bus_run))
        len = static_cast<int32_t>(sizeof g_bus_run);
    if (bus != 0 || addr < 0)
        std::memset(g_bus_run, 0, static_cast<size_t>(len));
    else
        chimera_vita3k::bus_read(static_cast<uint64_t>(addr), g_bus_run, static_cast<size_t>(len));
    return g_bus_run;
}

ECL_EXPORT uint64_t GetFrameCount(void) {
    return chimera_vita3k::frames();
}
ECL_EXPORT uint64_t GetExitedAt(void) {
    return chimera_vita3k::exited_at();
}
ECL_EXPORT uint64_t GetMachineTimeNs(void) {
    return chimera_vita3k::time_ns();
}
ECL_EXPORT uint64_t GetSwitchCount(void) {
    return chimera_vita3k::switches();
}
ECL_EXPORT uint64_t GetYieldCount(void) {
    return chimera_vita3k::yields();
}

// Every file in the machine's filesystem with its size, one a line: what a
// person debugging a boot wants first.
ECL_EXPORT const char *DebugListFiles(void) {
    static std::string listing;
    listing.clear();
    for (const auto &rel : chimera::memfs::list(chimera::memfs::ROOT)) {
        std::vector<uint8_t> data;
        const std::string path = std::string(chimera::memfs::ROOT) + "/" + rel;
        chimera::memfs::get(path, data);
        listing += path + " " + std::to_string(data.size()) + "\n";
    }
    return listing.c_str();
}

// The machine's log, for a person to read.
ECL_EXPORT int64_t GetLogSize(void) {
    g_log.clear();
    chimera_vita3k::log(g_log);
    return static_cast<int64_t>(g_log.size());
}
ECL_EXPORT const uint8_t *GetLogBuffer(void) {
    return g_log.data();
}

} // extern "C"
