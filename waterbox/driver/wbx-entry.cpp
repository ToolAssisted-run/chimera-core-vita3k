// The core's exports: what the frontend (and both runners of the gate) call.
// The same file in both flavours: natively they are called directly, in the
// sandbox through miniBox. The app is the file named by "rom.name", the
// settings are the JSON object in "settings", and save data to start from is
// the zip "savedata" when there is one - mounted files in the sandbox, files
// in the working directory natively.
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
#endif

namespace {

constexpr int SCREEN_W = 960, SCREEN_H = 544;

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

// The GPU bridge: the host's dispatcher, handed over before Init.
ECL_EXPORT void SetGpuBridge(uint64_t addr) {
    chimera_gl_install(reinterpret_cast<chimera_gl_bridge_fn>(static_cast<uintptr_t>(addr)));
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
    std::string name = read_file("rom.name");
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
        name.pop_back();
    if (name.empty()) {
        g_error = "no rom.name";
        return 0;
    }
    const std::string settings = read_file("settings");
    chimera_vita3k::Options options;
    options.cpu_mhz = json_number(settings, "cpu_mhz");
    options.rtc_start = json_number(settings, "rtc_start");
    options.no_surface_sync = json_number(settings, "no_surface_sync") != 0;
    if (FILE *f = fopen("savedata", "rb")) {
        fclose(f);
        options.savedata = "savedata";
    }
    return chimera_vita3k::boot(name, options, g_frame, g_error) ? 1 : 0;
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
    return SCREEN_W;
}
ECL_EXPORT int GetVideoHeight(void) {
    return SCREEN_H;
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
