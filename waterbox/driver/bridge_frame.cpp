// A FrameHost over the GPU bridge: see bridge_frame.h.
// SPDX-License-Identifier: MIT
#include "bridge_frame.h"

#include "gl-bridge.h"
#include "vk-bridge.h"

#include <cstring>

namespace {

// Our few GL entry points, through the bridge's own lookup: the renderer
// loads glad through get_proc_address, and nothing here may depend on when.
typedef void (*PFN_GenFramebuffers)(int, unsigned int *);
typedef void (*PFN_BindFramebuffer)(unsigned int, unsigned int);
typedef void (*PFN_GenRenderbuffers)(int, unsigned int *);
typedef void (*PFN_BindRenderbuffer)(unsigned int, unsigned int);
typedef void (*PFN_RenderbufferStorage)(unsigned int, unsigned int, int, int);
typedef void (*PFN_FramebufferRenderbuffer)(unsigned int, unsigned int, unsigned int, unsigned int);
typedef void (*PFN_ReadPixels)(int, int, int, int, unsigned int, unsigned int, void *);
typedef void (*PFN_GetIntegerv)(unsigned int, int *);
typedef void (*PFN_PixelStorei)(unsigned int, int);
typedef void (*PFN_DeleteFramebuffers)(int, const unsigned int *);
typedef void (*PFN_DeleteRenderbuffers)(int, const unsigned int *);

constexpr unsigned int GL_FRAMEBUFFER_ = 0x8D40, GL_READ_FRAMEBUFFER_ = 0x8CA8,
                       GL_READ_FRAMEBUFFER_BINDING_ = 0x8CAA, GL_RENDERBUFFER_ = 0x8D41,
                       GL_RGBA8_ = 0x8058, GL_COLOR_ATTACHMENT0_ = 0x8CE0, GL_BGRA_ = 0x80E1,
                       GL_UNSIGNED_BYTE_ = 0x1401, GL_PACK_ALIGNMENT_ = 0x0D05;

template <typename F>
F gl(const char *name) {
    return reinterpret_cast<F>(chimera_gl_lookup(name));
}

} // namespace

extern "C" void chimera_vulkan_quiet(bool quiet);

namespace chimera_vita3k {

BridgeFrame::BridgeFrame(int width_, int height_)
    : width(width_)
    , height(height_) {
}

bool BridgeFrame::resize(int width_, int height_) {
    if (fbo != 0 || count != 0)
        return false;
    width = width_;
    height = height_;
    return true;
}

void *BridgeFrame::get_proc_address(const char *name) const {
    return chimera_gl_lookup(name);
}

// The bridge's own vkGetInstanceProcAddr: every Vulkan function the renderer
// then asks for by name is one of the bridge's wrappers, or null.
void *BridgeFrame::vulkan_get_instance_proc_addr() const {
    return vulkan ? chimera_vk_lookup("vkGetInstanceProcAddr") : nullptr;
}

// The Vulkan renderer's present: the picture as it read it back, top row
// first already.
void BridgeFrame::present_pixels(const uint8_t *bgra, int width_, int height_) {
    if (!readback || !bgra || width_ != width || height_ != height)
        return;
    latest.assign(bgra, bgra + static_cast<size_t>(width) * height * 4);
    // opaque, as the OpenGL renderer's picture is: the same picture is then
    // the same bytes whichever renderer drew it
    for (size_t i = 3; i < latest.size(); i += 4)
        latest[i] = 0xFF;
    count++;
}

// Made on first use, on whichever thread holds the context then.
unsigned int BridgeFrame::default_fbo() const {
    if (fbo == 0) {
        gl<PFN_GenRenderbuffers>("glGenRenderbuffers")(1, &color_rb);
        gl<PFN_BindRenderbuffer>("glBindRenderbuffer")(GL_RENDERBUFFER_, color_rb);
        gl<PFN_RenderbufferStorage>("glRenderbufferStorage")(GL_RENDERBUFFER_, GL_RGBA8_, width, height);
        gl<PFN_GenFramebuffers>("glGenFramebuffers")(1, &fbo);
        gl<PFN_BindFramebuffer>("glBindFramebuffer")(GL_FRAMEBUFFER_, fbo);
        gl<PFN_FramebufferRenderbuffer>("glFramebufferRenderbuffer")(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_RENDERBUFFER_, color_rb);
    }
    return fbo;
}

bool BridgeFrame::make_current() {
    if (bind_current)
        bind_current();
    return true;
}

void BridgeFrame::done_current() {
    if (release_current)
        release_current();
}

void BridgeFrame::prepare_for_render_thread() {
    done_current();
}

// the host says which context the calls land on: it moves when a state is
// loaded (a fresh one in another process, a new id in the same one)
uint64_t BridgeFrame::context_id() const {
    return vulkan ? chimera_vk_context_id() : chimera_gl_context_id();
}

// The renderer dropped everything it remembered: the framebuffer goes too,
// in the same breath, before any new name is made (renderer.cpp,
// chimera_rebuild), and default_fbo makes it again.
void BridgeFrame::lost_context() {
    if (vulkan)
        return;
    if (fbo)
        gl<PFN_DeleteFramebuffers>("glDeleteFramebuffers")(1, &fbo);
    if (color_rb)
        gl<PFN_DeleteRenderbuffers>("glDeleteRenderbuffers")(1, &color_rb);
    fbo = 0;
    color_rb = 0;
}

// Vulkan after a load. true: what the renderer remembers is a context that
// no longer exists, and until false is said its calls go nowhere (wbx-entry,
// chimera_vulkan_quiet). false: it has let go of everything; the copies of
// mapped memory that nothing will unmap now are given back, and the calls
// cross again.
void BridgeFrame::vulkan_context_lost(bool lost) {
    if (!vulkan)
        return;
    if (lost) {
        chimera_vulkan_quiet(true);
    } else {
        chimera_vk_forget_mappings();
        chimera_vulkan_quiet(false);
    }
}

void BridgeFrame::swap_buffers() {
    if (!readback || vulkan)
        return;
    const size_t stride = static_cast<size_t>(width) * 4;
    rows.resize(stride * height);
    int previous = 0;
    gl<PFN_GetIntegerv>("glGetIntegerv")(GL_READ_FRAMEBUFFER_BINDING_, &previous);
    gl<PFN_BindFramebuffer>("glBindFramebuffer")(GL_READ_FRAMEBUFFER_, default_fbo());
    gl<PFN_PixelStorei>("glPixelStorei")(GL_PACK_ALIGNMENT_, 1);
    gl<PFN_ReadPixels>("glReadPixels")(0, 0, width, height, GL_BGRA_, GL_UNSIGNED_BYTE_, rows.data());
    gl<PFN_BindFramebuffer>("glBindFramebuffer")(GL_READ_FRAMEBUFFER_, static_cast<unsigned int>(previous));
    // GL's first row is the bottom one
    latest.resize(rows.size());
    for (int y = 0; y < height; y++)
        std::memcpy(&latest[static_cast<size_t>(y) * stride], &rows[static_cast<size_t>(height - 1 - y) * stride], stride);
    count++;
}

} // namespace chimera_vita3k
