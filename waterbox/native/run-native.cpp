// The native reference: installs a .vpk into a fresh work directory, boots it
// under Vita3K with no window, and reports what it presented.
//
// The machine runs on its own scheduler and clock (vsched): this thread is
// its thread 0, and frame f ends at exactly f/60 s of machine time, so two
// runs of the same app are the same run.
//
// usage: vita3k-run-native <app.vpk> --work <dir> [--frames N] [--timeout S]
//                          [--digest-every N] [--screenshot F=PATH]...
//                          [--cpu-mhz N] [--rtc-start UNIX_SECONDS]

#include "archive.h"
#include "headless_frame.h"
#include "interface.h"

#include <app/functions.h>
#include <app/session_controller.h>
#include <config/functions.h>
#include <config/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <modules/module_parent.h>
#include <util/fs.h>
#include <util/log.h>

#include <chimera/vsched.h>

#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static constexpr int SCREEN_W = 960, SCREEN_H = 544;

static uint64_t fnv1a(const std::vector<uint8_t> &bytes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

// uncompressed 32-bit TGA, top row first
static bool write_tga(const std::string &path, const std::vector<uint8_t> &bgra, int w, int h) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    uint8_t header[18] = {};
    header[2] = 2;
    header[12] = w & 0xff;
    header[13] = w >> 8;
    header[14] = h & 0xff;
    header[15] = h >> 8;
    header[16] = 32;
    header[17] = 0x28; // top-left origin, 8 alpha bits
    std::fwrite(header, 1, sizeof header, f);
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    return std::fclose(f) == 0;
}

static fs::path exe_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return fs::current_path();
    buf[n] = '\0';
    return fs::path(buf).parent_path();
}

// A work directory this program made is marked, and only a marked one is
// emptied: --work is never trusted to point somewhere disposable.
static bool fresh_work_dir(const fs::path &work) {
    const fs::path mark = work / ".vita3k-run-native";
    if (fs::exists(work)) {
        if (!fs::exists(mark)) {
            std::fprintf(stderr, "run-native: %s exists and is not a work directory of ours\n", work.c_str());
            return false;
        }
        for (const auto &entry : fs::directory_iterator(work))
            if (entry.path() != mark)
                fs::remove_all(entry.path());
        return true;
    }
    fs::create_directories(work);
    FILE *f = std::fopen(mark.c_str(), "w");
    if (f)
        std::fclose(f);
    return true;
}

int main(int argc, char **argv) {
    std::string vpk, work_arg;
    uint64_t frames = 300, digest_every = 0;
    int timeout_s = 120;
    uint64_t cpu_mhz = 0, rtc_start = 0;
    std::map<uint64_t, std::string> screenshots;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--work" && i + 1 < argc)
            work_arg = argv[++i];
        else if (a == "--frames" && i + 1 < argc)
            frames = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--timeout" && i + 1 < argc)
            timeout_s = std::atoi(argv[++i]);
        else if (a == "--cpu-mhz" && i + 1 < argc)
            cpu_mhz = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--rtc-start" && i + 1 < argc)
            rtc_start = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--digest-every" && i + 1 < argc)
            digest_every = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--screenshot" && i + 1 < argc) {
            const std::string spec = argv[++i];
            const auto eq = spec.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "run-native: --screenshot wants F=PATH\n");
                return 2;
            }
            screenshots[std::strtoull(spec.substr(0, eq).c_str(), nullptr, 10)] = spec.substr(eq + 1);
        } else if (vpk.empty() && a[0] != '-')
            vpk = a;
        else {
            std::fprintf(stderr, "run-native: unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (vpk.empty() || work_arg.empty()) {
        std::fprintf(stderr, "usage: vita3k-run-native <app.vpk> --work <dir> [--frames N] [--timeout S] [--digest-every N] [--screenshot F=PATH]...\n");
        return 2;
    }

    // The machine's clock starts here, on this thread, before anything of the
    // emulator exists; every thread it makes is scheduled from then on.
    vsched_init();
    if (cpu_mhz)
        vsched_set_cpu_hz(cpu_mhz * 1000000ull);
    if (rtc_start)
        vsched_set_calendar_start(rtc_start);
    // a timezone from the host would show in the dates the machine formats
    setenv("TZ", "UTC0", 1);
    tzset();
    // a machine that stops running is stopped by the host, not waited on
    alarm(static_cast<unsigned>(timeout_s));

    const fs::path work = fs::absolute(work_arg);
    if (!fresh_work_dir(work))
        return 2;

    // Everything the emulator keeps lives in the work directory; the static
    // assets (data/, shaders-builtin/) sit beside this program, as they do
    // beside upstream's.
    Root root_paths;
    root_paths.set_static_assets_path(exe_dir() / "");
    root_paths.set_vita_fs_path(work / "fs" / "");
    root_paths.set_log_path(work / "");
    root_paths.set_config_path(work / "");
    root_paths.set_shared_path(work / "");
    root_paths.set_cache_path(work / "cache" / "");
    root_paths.set_patch_path(work / "patch" / "");
    // the emulator lists these without making them first
    fs::create_directories(root_paths.get_vita_fs_path());
    fs::create_directories(root_paths.get_cache_path());
    fs::create_directories(root_paths.get_patch_path());

    // the log goes to the work directory (vita3k.log), stdout is ours
    if (logging::init(root_paths, false) != Success)
        return 3;

    // The program's own option parser with fixed answers: the OpenGL backend,
    // and the config file is never rewritten.
    std::vector<std::string> args = { "vita3k-run-native", "-w", "-B", "OpenGL" };
    std::vector<char *> args_c;
    for (auto &s : args)
        args_c.push_back(s.data());
    args_c.push_back(nullptr);
    Config cfg{};
    EmuEnvState emuenv;
    if (config::init_config(cfg, static_cast<int>(args.size()), args_c.data(), root_paths, false) != Success) {
        std::fprintf(stderr, "run-native: the config could not be made\n");
        return 3;
    }

    // Nothing the host's state decides may reach the picture: no shader
    // notices (they depend on what is cached), no compiling on worker threads,
    // no shader cache on disk, no swap interval. The log keeps warnings only.
    cfg.v_sync = false;
    cfg.show_compile_shaders = false;
    cfg.async_pipeline_compilation = false;
    cfg.shader_cache = false;
    cfg.log_level = 3;

    if (!app::init(emuenv, cfg, root_paths)) {
        std::fprintf(stderr, "run-native: the emulated environment could not be made\n");
        return 3;
    }
    init_libraries(emuenv);
    app::init_apps_list(emuenv);
    app::load_users(emuenv);

    std::string title_id;
    for (const auto &content : install_archive(emuenv, fs::absolute(vpk)))
        if (content.category == "gd" && content.state)
            title_id = content.title_id;
    if (title_id.empty()) {
        std::fprintf(stderr, "run-native: %s installed no application\n", vpk.c_str());
        return 4;
    }
    app::init_apps_list(emuenv);

    HeadlessFrame frame(SCREEN_W, SCREEN_H);
    if (!frame.ok()) {
        std::fprintf(stderr, "run-native: %s\n", frame.why().c_str());
        return 5;
    }

    app::AppSessionController session(emuenv);
    AppLaunchRequest launch;
    launch.app_path = title_id;
    if (!session.begin_launch(launch) || !session.initialize_renderer(frame) || !session.initialize_runtime()
        || !session.load_and_run()) {
        std::fprintf(stderr, "run-native: %s did not start\n", title_id.c_str());
        return 6;
    }

    // Frame f ends 2 us after vblank f: the vblank thread runs at f/60 s
    // exactly and the render thread presents 1 us later, so the newest
    // picture then is the one vblank f shows.
    uint64_t seen = 0, exited = 0;
    for (uint64_t f = 1; f <= frames; f++) {
        vsched_sleep_until(VSCHED_START_NS + f * 1000000000ull / 60 + 2000);
        seen = f;
        // An app that exits asks the frontend to relaunch it, or nothing:
        // the machine stops where it stands, keeping the last picture.
        if (emuenv.take_app_launch_request()) {
            exited = f;
            break;
        }
        const auto picture = frame.picture();
        if (digest_every != 0 && f % digest_every == 0)
            std::printf("frame %" PRIu64 " time_ns=%" PRIu64 " video=%016" PRIx64 "\n", f, vsched_now_ns(), fnv1a(picture));
        const auto shot = screenshots.find(f);
        if (shot != screenshots.end() && !write_tga(shot->second, picture, SCREEN_W, SCREEN_H))
            std::fprintf(stderr, "run-native: could not write %s\n", shot->second.c_str());
    }

    const auto picture = frame.picture();
    std::printf("title=%s frames=%" PRIu64 " exited=%" PRIu64 " time_ns=%" PRIu64 " switches=%" PRIu64 " video=%016" PRIx64 " %dx%d\n", title_id.c_str(), seen,
        exited, vsched_now_ns(), vsched_switch_count(), picture.empty() ? 0 : fnv1a(picture), SCREEN_W, SCREEN_H);
    std::fflush(stdout);

    // Leave without tearing the machine down: guest threads are detached host
    // threads, and the process ending is the one stop they all obey.
    std::_Exit(seen >= frames || exited ? 0 : 7);
}
