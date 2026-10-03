// A FrameHost with no window: an EGL context (surfaceless) drawing into a
// framebuffer object of the Vita's screen size, read back at every present.
#pragma once

#include <renderer/frame_host.h>

#include <EGL/egl.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class HeadlessFrame : public renderer::FrameHost {
public:
    HeadlessFrame(int width, int height);
    ~HeadlessFrame() override;

    // false, and why, when no GL 4.3 core context could be made
    bool ok() const { return error.empty(); }
    const std::string &why() const { return error; }

    renderer::DisplayHandle handle() const override { return {}; }
    int drawable_width() const override { return width; }
    int drawable_height() const override { return height; }
    std::vector<std::string> font_dirs() const override { return {}; }
    void *get_proc_address(const char *name) const override;
    unsigned int default_fbo() const override;
    bool make_current() override;
    void done_current() override;
    void swap_buffers() override;
    void prepare_for_render_thread() override;
    void destroy_render_context() override;

    // The presented pictures, BGRA top row first, counted from 1. Waits until
    // picture `n` exists or `timeout_ms` passes; returns how many exist.
    uint64_t wait_for(uint64_t n, int timeout_ms);
    // the picture `n` (only the newest is kept), empty when it is gone
    std::vector<uint8_t> picture() const;
    uint64_t presented() const;

private:
    int width, height;
    std::string error;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    mutable unsigned int fbo = 0, color_rb = 0;

    mutable std::mutex mutex;
    std::condition_variable cond;
    std::vector<uint8_t> latest;
    uint64_t count = 0;
};
