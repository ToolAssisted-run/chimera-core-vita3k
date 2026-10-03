// The core: see vita3k_driver.h.
// SPDX-License-Identifier: MIT
#include "vita3k_driver.h"

#include "archive.h"
#include "assets.h"
#include "interface.h"
#include "memfs.h"

#include <app/functions.h>
#include <app/session_controller.h>
#include <config/functions.h>
#include <config/state.h>
#include <emuenv/state.h>
#include <modules/module_parent.h>
#include <util/fs.h>
#include <util/log.h>

#include <chimera/vsched.h>

#include <cstdio>
#include <memory>

namespace chimera_vita3k {

namespace {

constexpr const char *TREE = chimera::memfs::ROOT;

struct Machine {
    EmuEnvState emuenv;
    std::unique_ptr<app::AppSessionController> session;
    uint64_t frames = 0;
    uint64_t exited = 0;
};

Machine *g_machine;

int64_t machine_seconds() {
    return static_cast<int64_t>(vsched_calendar_us() / 1000000ull);
}

} // namespace

bool boot(const std::string &host_name, const Options &options, BridgeFrame &frame, std::string &error) {
    // The machine's clock starts here, on this thread, before anything of
    // the emulator exists; every thread it makes is scheduled from then on.
    vsched_init();
    if (options.cpu_mhz)
        vsched_set_cpu_hz(options.cpu_mhz * 1000000ull);
    if (options.rtc_start)
        vsched_set_calendar_start(options.rtc_start);
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
    // no shader cache, no swap interval. The log keeps warnings only.
    cfg.v_sync = false;
    cfg.show_compile_shaders = false;
    cfg.async_pipeline_compilation = false;
    cfg.shader_cache = false;
    cfg.log_level = 3;

    if (!app::init(emuenv, cfg, root_paths)) {
        error = "the emulated environment could not be made";
        return false;
    }
    init_libraries(emuenv);
    app::init_apps_list(emuenv);
    app::load_users(emuenv);

    std::string title_id;
    for (const auto &content : install_archive(emuenv, app))
        if (content.category == "gd" && content.state)
            title_id = content.title_id;
    if (title_id.empty()) {
        error = host_name + " installed no application";
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

void frame() {
    Machine &m = *g_machine;
    if (m.exited)
        return;
    const uint64_t f = m.frames + 1;
    vsched_sleep_until(VSCHED_START_NS + f * 1000000000ull / 60 + 2000);
    m.frames = f;
    // An app that exits asks the frontend to relaunch it, or nothing: the
    // machine stops where it stands, keeping the last picture.
    if (m.emuenv.take_app_launch_request())
        m.exited = f;
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

bool log(std::vector<uint8_t> &out) {
    // the log is a buffered stream: what it holds is written out first
    fflush(nullptr);
    return chimera::memfs::get(std::string(TREE) + "/log/vita3k.log", out);
}

} // namespace chimera_vita3k
