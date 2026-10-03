// A FrameHost over the GPU bridge: see bridge_frame.h.
// SPDX-License-Identifier: MIT
#include "bridge_frame.h"

#include "gl-bridge.h"

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

namespace chimera_vita3k {

BridgeFrame::BridgeFrame(int width_, int height_)
    : width(width_)
    , height(height_) {
}

void *BridgeFrame::get_proc_address(const char *name) const {
    return chimera_gl_lookup(name);
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
    return chimera_gl_context_id();
}

// The renderer dropped everything it remembered: the framebuffer goes too,
// in the same breath, before any new name is made (renderer.cpp,
// chimera_rebuild), and default_fbo makes it again.
void BridgeFrame::lost_context() {
    if (fbo)
        gl<PFN_DeleteFramebuffers>("glDeleteFramebuffers")(1, &fbo);
    if (color_rb)
        gl<PFN_DeleteRenderbuffers>("glDeleteRenderbuffers")(1, &color_rb);
    fbo = 0;
    color_rb = 0;
}

void BridgeFrame::swap_buffers() {
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
