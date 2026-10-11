// A FrameHost over the GPU bridge, the same in both flavours: every GL entry
// point the renderer loads is a generated wrapper that hands the call to the
// host (miniBox source/gl), in the sandbox through its one callback and
// natively straight into the same dispatcher. The default framebuffer is a
// framebuffer object of the Vita's screen size, read back at every present.
// SPDX-License-Identifier: MIT
#pragma once

#include <renderer/frame_host.h>

#include <cstdint>
#include <vector>

namespace chimera_vita3k {

class BridgeFrame : public renderer::FrameHost {
public:
    BridgeFrame(int width, int height);

    // Another size, before anything was drawn: the Internal Resolution
    // setting is read after this object exists. False once the framebuffer
    // has been made.
    bool resize(int width, int height);

    // Natively the context is a real EGL context held by one thread at a
    // time, and the render thread takes it over from the thread that made
    // the renderer; in the sandbox every guest thread is one host thread and
    // these stay null.
    void (*bind_current)() = nullptr;
    void (*release_current)() = nullptr;

    // false: a present is not read back and the picture stands (turbo)
    bool readback = true;

    // true: the renderer is Vulkan's, through the Vulkan bridge. The picture
    // then arrives by present_pixels and nothing of OpenGL is touched.
    bool vulkan = false;

    renderer::DisplayHandle handle() const override { return {}; }
    int drawable_width() const override { return width; }
    int drawable_height() const override { return height; }
    std::vector<std::string> font_dirs() const override { return {}; }
    void *get_proc_address(const char *name) const override;
    void *vulkan_get_instance_proc_addr() const override;
    void present_pixels(const uint8_t *bgra, int width, int height) override;
    unsigned int default_fbo() const override;
    bool make_current() override;
    void done_current() override;
    void swap_buffers() override;
    void prepare_for_render_thread() override;
    uint64_t context_id() const override;
    void lost_context() override;
    void vulkan_context_lost(bool lost) override;

    // the newest presented picture, BGRA top row first; empty before the
    // first present
    const std::vector<uint8_t> &picture() const { return latest; }
    uint64_t presented() const { return count; }

private:
    int width, height;
    mutable unsigned int fbo = 0, color_rb = 0;
    std::vector<uint8_t> latest;
    std::vector<uint8_t> rows;
    uint64_t count = 0;
};

} // namespace chimera_vita3k
