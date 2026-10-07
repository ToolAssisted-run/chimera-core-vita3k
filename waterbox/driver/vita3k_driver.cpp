// The core: see vita3k_driver.h.
// SPDX-License-Identifier: MIT
#include "vita3k_driver.h"

#include "archive.h"
#include "assets.h"
#include "interface.h"
#include "memfs.h"
#include "savedata.h"

#include <app/functions.h>
#include <app/session_controller.h>
#include <audio/impl/chimera_audio.h>
#include <config/functions.h>
#include <config/state.h>
#include <ctrl/ctrl.h>
#include <emuenv/state.h>
#include <mem/functions.h>
#include <modules/module_parent.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
#include <motion/functions.h>
#include <touch/functions.h>
#include <util/fs.h>
#include <util/log.h>

#include <chimera/input.h>
#include <chimera/vsched.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>

namespace chimera_vita3k {

namespace {

constexpr const char *TREE = chimera::memfs::ROOT;

constexpr size_t AUDIO_CAP = 2048; // pairs; a frame is 800

struct Machine {
    EmuEnvState emuenv;
    std::unique_ptr<app::AppSessionController> session;
    uint64_t frames = 0;
    uint64_t exited = 0;
    int16_t audio[AUDIO_CAP * 2] = {};
    int audio_pairs = 0;
    std::vector<savedata::File> saves; // the last snapshot
};

Machine *g_machine;
BridgeFrame *g_frame_host;

// the buttons' bits as the pad reports them, in the declared order; L and R
// are L1/R1 to the 2/Ext2 reads, as on the hardware
constexpr uint32_t PAD[12] = { SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT, SCE_CTRL_CROSS,
    SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE, SCE_CTRL_L, SCE_CTRL_R, SCE_CTRL_START, SCE_CTRL_SELECT };
constexpr uint32_t PAD_EXT[12] = { SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT, SCE_CTRL_CROSS,
    SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE, SCE_CTRL_L1, SCE_CTRL_R1, SCE_CTRL_START, SCE_CTRL_SELECT };
constexpr int FRONT_TOUCH = 12, REAR_TOUCH = 13;

uint8_t stick(int32_t v) {
    return static_cast<uint8_t>(std::clamp(128 + v, 0, 255));
}

// 0..65535 across a panel, onto `size` points from `first`
uint16_t panel(int32_t v, uint32_t first, uint32_t size) {
    return static_cast<uint16_t>(first + static_cast<uint32_t>(std::clamp(v, 0, 65535)) * size / 65536);
}

int64_t machine_seconds() {
    return static_cast<int64_t>(vsched_calendar_us() / 1000000ull);
}

} // namespace

// A PlayStation Store package, by what it is and not by what it is called:
// "\x7fPKG" at the start, "\x7fext" where its extended header is.
static bool is_package(const std::string &path) {
    PkgHeader header{};
    PkgExtHeader ext{};
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    const bool read = fread(&header, sizeof header, 1, f) == 1 && fread(&ext, sizeof ext, 1, f) == 1;
    fclose(f);
    return read && std::memcmp(&header.magic, "\x7fPKG", 4) == 0 && std::memcmp(&ext.magic, "\x7f" "ext", 4) == 0;
}

// The game as a package (.pkg), the form the PlayStation Store sends it in:
// Vita3K unpacks it into the machine and decrypts it with its licence, which
// the project brings as the game's work.bin. Vita3K looks for a package's
// licence in the machine, at ux0:license/<TITLE ID>/<content id>.rif, so that
// is where the project's file is put - the name is the package's own content
// id, whatever the file was called. Only a game is the Game slot's: a package
// that installs anything else (add-on content, a patch, a theme) says so.
static bool install_package(EmuEnvState &emuenv, const std::string &app, const std::string &tree, const std::string &host_name,
    const std::string &licence, std::string &title_id, std::string &error) {
    if (licence.empty()) {
        error = host_name + " is a package, and a package is encrypted: put the game's licence (its work.bin) in the Licence slot";
        return false;
    }
    PkgHeader header{};
    if (FILE *f = fopen(app.c_str(), "rb")) {
        const bool read = fread(&header, sizeof header, 1, f) == 1;
        fclose(f);
        if (!read)
            header = {};
    }
    const std::string content_id(header.content_id, strnlen(header.content_id, sizeof header.content_id));
    if (content_id.size() < 16) {
        error = host_name + " names no content: it is not a package Vita3K can install";
        return false;
    }
    // a file of the machine's own, not a graft of the project's: the install
    // writes the licence there again when it is done
    std::vector<uint8_t> bytes;
    if (FILE *f = fopen(licence.c_str(), "rb")) {
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && bytes.size() < (1u << 20))
            bytes.insert(bytes.end(), buf, buf + n);
        fclose(f);
    }
    const std::string rif = tree + "/fs/ux0/license/" + content_id.substr(7, 9) + "/" + content_id + ".rif";
    if (bytes.empty() || !chimera::memfs::mkdirs(rif.substr(0, rif.rfind('/'))) || !chimera::memfs::put(rif, bytes)) {
        error = "cannot read the licence " + licence;
        return false;
    }
    // The conversion prints the licence, as text, on stdout. That is the one
    // thing here nobody is to read: it goes nowhere, not even to the log.
    std::ostringstream unread;
    std::streambuf *was = std::cout.rdbuf(unread.rdbuf());
    std::string zrif = find_pkg_zrif(app, emuenv.vita_fs_path);
    std::cout.rdbuf(was);
    if (zrif.empty() || !validate_zrif(zrif)) {
        error = fs::path(licence).filename().string() + " is not a licence: a work.bin is the 512 bytes the console keeps for the game";
        return false;
    }
    std::ostringstream said;
    was = std::cout.rdbuf(said.rdbuf());
    const bool installed = install_pkg(app, emuenv, zrif, [](float) {});
    std::cout.rdbuf(was);
    if (!said.str().empty())
        LOG_DEBUG("the install said:\n{}", said.str());
    if (!installed) {
        error = host_name + " did not install: the package is damaged, or " + fs::path(licence).filename().string() + " is another game's licence";
        return false;
    }
    if (emuenv.app_info.app_category != "gd") {
        error = host_name + " is not a game on its own: it installs " + emuenv.app_info.app_title
            + " (category " + emuenv.app_info.app_category + "), which belongs to a game";
        return false;
    }
    title_id = emuenv.app_info.app_title_id;
    return true;
}

bool boot(const std::string &host_name, const Options &options, BridgeFrame &frame, std::string &error) {
    g_frame_host = &frame;
    // The machine's clock starts here, on this thread, before anything of
    // the emulator exists; every thread it makes is scheduled from then on.
    vsched_init();
    if (options.cpu_mhz)
        vsched_set_cpu_hz(options.cpu_mhz * 1000000ull);
    if (options.rtc_start)
        vsched_set_calendar_start(options.rtc_start);
    if (options.free_hle_calls)
        vsched_set_hle_call_instructions(0);
    // a timezone from the host would show in the dates the machine formats
    setenv("TZ", "UTC0", 1);
    tzset();

    // Everything the emulator keeps lives in the machine's filesystem: the
    // Vita's partitions, config, cache, log. The static assets (built into
    // the core) and the app are grafted in read-only.
    chimera::memfs::set_clock(machine_seconds);
    const std::string tree = TREE;
    for (size_t i = 0; i < asset_count; i++)
        chimera::memfs::graft_static(tree + "/assets/" + assets[i].path, assets[i].data, assets[i].size);
    const std::string app = tree + "/rom/" + fs::path(host_name).filename().string();
    if (!chimera::memfs::graft(app, host_name)) {
        error = "cannot read " + host_name;
        return false;
    }
    for (const char *d : { "/fs", "/cache", "/log", "/config", "/patch" })
        chimera::memfs::mkdirs(tree + d);

    Root root_paths;
    root_paths.set_static_assets_path(tree + "/assets/");
    root_paths.set_vita_fs_path(tree + "/fs/");
    root_paths.set_log_path(tree + "/log/");
    root_paths.set_config_path(tree + "/config/");
    root_paths.set_shared_path(tree + "/config/");
    root_paths.set_cache_path(tree + "/cache/");
    root_paths.set_patch_path(tree + "/patch/");

    if (logging::init(root_paths, false) != Success) {
        error = "the log could not be made";
        return false;
    }

    // The program's own option parser with fixed answers: the OpenGL
    // backend, and the config file is never rewritten.
    std::vector<std::string> args = { "vita3k", "-w", "-B", "OpenGL" };
    std::vector<char *> args_c;
    for (auto &s : args)
        args_c.push_back(s.data());
    args_c.push_back(nullptr);
    Config cfg{};
    g_machine = new Machine;
    EmuEnvState &emuenv = g_machine->emuenv;
    if (config::init_config(cfg, static_cast<int>(args.size()), args_c.data(), root_paths, false) != Success) {
        error = "the config could not be made";
        return false;
    }

    // Nothing the host's state decides may reach the picture: no shader
    // notices (they depend on what is cached), no compiling on worker threads,
    // no shader cache, no swap interval. The log keeps warnings only. Surface
    // sync writes every finished scene back to the machine's memory, as the
    // Vita's GPU does: a state then holds the pictures, and the renderer,
    // rebuilt after a load, finds them there.
    cfg.v_sync = false;
    cfg.show_compile_shaders = false;
    cfg.async_pipeline_compilation = false;
    cfg.shader_cache = false;
    cfg.log_level = 3;
    cfg.disable_surface_sync = options.no_surface_sync;
    cfg.resolution_multiplier = static_cast<float>(options.resolution_scale);
    // the system parameters games read (sceAppUtilSystemParamGetInt)
    cfg.sys_lang = options.language;
    cfg.sys_button = options.enter_button;

    if (!app::init(emuenv, cfg, root_paths)) {
        error = "the emulated environment could not be made";
        return false;
    }
    init_libraries(emuenv);
    app::init_apps_list(emuenv);
    app::load_users(emuenv);

    // the system software the project brings, installed as Vita3K installs
    // it (os0, vs0, sa0, pd0 into the Vita's filesystem): before the app, and
    // before the machine is sealed, so a state carries none of it
    for (const std::string &pup : options.firmware) {
        // each file installs its own partition, and the two are easy to
        // swap by name: the system software fills vs0, the fonts sa0 (the
        // folders themselves exist from the start)
        const bool fonts = pup.find("PSP2UPDAT") != std::string::npos;
        const char *partition = fonts ? "sa0" : "vs0";
        const std::string where = tree + "/fs/" + partition;
        const size_t had = chimera::memfs::list(where).size();
        const std::string version = install_pup(emuenv.vita_fs_path, pup);
        if (version.empty()) {
            error = "cannot install the system software " + pup;
            return false;
        }
        if (chimera::memfs::list(where).size() <= had) {
            error = pup + " installed nothing in " + partition + ": it is not the " + (fonts ? "font package" : "system software")
                + (fonts ? " (is it the system software, PSVUPDAT.PUP?)" : " (is it the font package, PSP2UPDAT.PUP?)");
            return false;
        }
        LOG_INFO("installed {}: system software {}", pup, version);
    }

    // what the install prints (Vita3K's PFS decryption talks on stdout) goes
    // to the machine's log, not to the frontend's output
    std::string title_id;
    if (is_package(app)) {
        if (!install_package(emuenv, app, tree, host_name, options.licence, title_id, error))
            return false;
    } else {
        std::ostringstream said;
        std::streambuf *was = std::cout.rdbuf(said.rdbuf());
        for (const auto &content : install_archive(emuenv, app))
            if (content.category == "gd" && content.state)
                title_id = content.title_id;
        std::cout.rdbuf(was);
        if (!said.str().empty())
            LOG_DEBUG("the install said:\n{}", said.str());
    }
    if (title_id.empty()) {
        error = host_name + " installed no application";
        return false;
    }

    // the save data the project brings, in place before the app starts (and
    // before the machine is sealed, so a state carries only what it changes)
    if (!options.savedata.empty()) {
        std::vector<uint8_t> zip;
        if (FILE *f = fopen(options.savedata.c_str(), "rb")) {
            uint8_t buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0)
                zip.insert(zip.end(), buf, buf + n);
            fclose(f);
        }
        if (zip.empty()) {
            error = "cannot read the save data " + options.savedata;
            return false;
        }
        // the user it belongs to: made and logged in now, as the launch
        // would a moment later
        if (!app::ensure_current_user(emuenv)) {
            error = "no user to give the save data to";
            return false;
        }
        if (!savedata::seed(zip, tree + "/fs/ux0", emuenv.cfg.user_id, error))
            return false;
    }
    app::init_apps_list(emuenv);

    g_machine->session = std::make_unique<app::AppSessionController>(emuenv);
    auto &session = *g_machine->session;
    AppLaunchRequest launch;
    launch.app_path = title_id;
    if (!session.begin_launch(launch) || !session.initialize_renderer(frame) || !session.initialize_runtime()
        || !session.load_and_run()) {
        error = title_id + " did not start";
        return false;
    }
    return true;
}

void set_rendering(bool on) {
    if (g_frame_host)
        g_frame_host->readback = on;
}

void set_input(uint64_t buttons, const int32_t axes[AXES]) {
    auto &in = chimera::frame_input();
    in.buttons = in.buttons_ext = 0;
    for (int i = 0; i < 12; i++)
        if (buttons & (1ull << i)) {
            in.buttons |= PAD[i];
            in.buttons_ext |= PAD_EXT[i];
        }
    in.lx = stick(axes[0]);
    in.ly = stick(-axes[1]);
    in.rx = stick(axes[2]);
    in.ry = stick(-axes[3]);
    // the front panel is 1920x1088 points over the screen; the rear one
    // 1920 across and 108..889 down
    in.touch_down[0] = (buttons >> FRONT_TOUCH) & 1;
    in.touch_x[0] = panel(axes[4], 0, 1920);
    in.touch_y[0] = panel(axes[5], 0, 1088);
    in.touch_down[1] = (buttons >> REAR_TOUCH) & 1;
    in.touch_x[1] = panel(axes[6], 0, 1920);
    in.touch_y[1] = panel(axes[7], 108, 782);
    for (int i = 0; i < 3; i++) {
        in.accel[i] = axes[8 + i] / 1000.0f;
        in.gyro[i] = axes[11 + i] / 3600.0f; // tenths of a degree to revolutions
    }
}

void frame() {
    Machine &m = *g_machine;
    m.audio_pairs = 0;
    if (m.exited)
        return;
    // the panels and sensors, sampled as the frame starts
    chimera::frame_input().read = false;
    touch_vsync_update(m.emuenv);
    chimera_motion_update(m.emuenv.motion);
    refresh_motion(m.emuenv.motion, m.emuenv.ctrl);

    const uint64_t f = m.frames + 1;
    const uint64_t end = VSCHED_START_NS + f * 1000000000ull / 60 + 2000;
    vsched_sleep_until(end);
    m.frames = f;
    if (m.emuenv.audio.adapter)
        m.audio_pairs = static_cast<int>(static_cast<ChimeraAudioAdapter &>(*m.emuenv.audio.adapter).take(end, m.audio, AUDIO_CAP));
    // An app that exits asks the frontend to relaunch it, or nothing: the
    // machine stops where it stands, keeping the last picture.
    if (m.emuenv.take_app_launch_request())
        m.exited = f;
}

const int16_t *audio(int &pairs) {
    pairs = g_machine ? g_machine->audio_pairs : 0;
    return g_machine ? g_machine->audio : nullptr;
}

bool input_was_read() {
    return chimera::frame_input().read;
}

size_t savedata_snapshot() {
    if (!g_machine)
        return 0;
    g_machine->saves = savedata::snapshot(std::string(TREE) + "/fs/ux0", g_machine->emuenv.cfg.user_id);
    return g_machine->saves.size();
}

const std::string &savedata_name(size_t i) {
    return g_machine->saves[i].name;
}

const std::vector<uint8_t> &savedata_bytes(size_t i) {
    return g_machine->saves[i].data;
}

uint64_t frames() {
    return g_machine ? g_machine->frames : 0;
}

uint64_t exited_at() {
    return g_machine ? g_machine->exited : 0;
}

uint64_t time_ns() {
    return vsched_now_ns();
}

uint64_t switches() {
    return vsched_switch_count();
}

uint64_t yields() {
    return vsched_yield_count();
}

bool bus_ready() {
    return g_machine != nullptr && g_machine->emuenv.mem.memory;
}

uint8_t bus_peek(uint32_t addr) {
    if (!bus_ready() || !is_valid_addr(g_machine->emuenv.mem, addr))
        return 0;
    return g_machine->emuenv.mem.memory.get()[addr];
}

void bus_poke(uint32_t addr, uint8_t value) {
    if (bus_ready() && is_valid_addr(g_machine->emuenv.mem, addr))
        g_machine->emuenv.mem.memory.get()[addr] = value;
}

void bus_read(uint64_t addr, uint8_t *out, size_t len) {
    constexpr uint64_t PAGE = 4096;
    while (len) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(len, PAGE - addr % PAGE));
        if (addr < BUS_SIZE && bus_ready() && is_valid_addr(g_machine->emuenv.mem, static_cast<Address>(addr)))
            std::memcpy(out, g_machine->emuenv.mem.memory.get() + addr, n);
        else
            std::memset(out, 0, n);
        out += n;
        addr += n;
        len -= n;
    }
}

bool log(std::vector<uint8_t> &out) {
    // the log is a buffered stream: what it holds is written out first
    fflush(nullptr);
    return chimera::memfs::get(std::string(TREE) + "/log/vita3k.log", out);
}

} // namespace chimera_vita3k
