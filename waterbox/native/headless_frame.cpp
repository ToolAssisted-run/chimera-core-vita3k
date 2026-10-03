#include "headless_frame.h"

#include <EGL/eglext.h>

#include <chrono>
#include <cstring>

// Our own few GL entry points, from EGL: the renderer loads glad through
// get_proc_address, and nothing here may depend on when it does.
typedef void (*PFN_GenFramebuffers)(int, unsigned int *);
typedef void (*PFN_BindFramebuffer)(unsigned int, unsigned int);
typedef void (*PFN_GenRenderbuffers)(int, unsigned int *);
typedef void (*PFN_BindRenderbuffer)(unsigned int, unsigned int);
typedef void (*PFN_RenderbufferStorage)(unsigned int, unsigned int, int, int);
typedef void (*PFN_FramebufferRenderbuffer)(unsigned int, unsigned int, unsigned int, unsigned int);
typedef void (*PFN_ReadPixels)(int, int, int, int, unsigned int, unsigned int, void *);
typedef void (*PFN_GetIntegerv)(unsigned int, int *);
typedef void (*PFN_PixelStorei)(unsigned int, int);

static constexpr unsigned int GL_FRAMEBUFFER_ = 0x8D40, GL_READ_FRAMEBUFFER_ = 0x8CA8,
                              GL_READ_FRAMEBUFFER_BINDING_ = 0x8CAA, GL_RENDERBUFFER_ = 0x8D41,
                              GL_RGBA8_ = 0x8058, GL_COLOR_ATTACHMENT0_ = 0x8CE0, GL_BGRA_ = 0x80E1,
                              GL_UNSIGNED_BYTE_ = 0x1401, GL_PACK_ALIGNMENT_ = 0x0D05;

template <typename F>
static F gl(const char *name) {
    return reinterpret_cast<F>(eglGetProcAddress(name));
}

HeadlessFrame::HeadlessFrame(int width_, int height_)
    : width(width_)
    , height(height_) {
    auto get_platform_display = gl<PFNEGLGETPLATFORMDISPLAYEXTPROC>("eglGetPlatformDisplayEXT");
    if (get_platform_display)
        display = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (display == EGL_NO_DISPLAY)
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        error = "no EGL display";
        return;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        error = "EGL has no desktop OpenGL";
        return;
    }
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint configs = 0;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &configs) || configs == 0) {
        error = "no EGL config for OpenGL";
        return;
    }
    const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        error = "no OpenGL 4.3 core context";
        return;
    }
    // surfaceless: the picture is our framebuffer object, never a surface
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        error = "the context could not be made current";
}

HeadlessFrame::~HeadlessFrame() {
    destroy_render_context();
}

void *HeadlessFrame::get_proc_address(const char *name) const {
    return reinterpret_cast<void *>(eglGetProcAddress(name));
}

// Made on first use, on whichever thread holds the context then.
unsigned int HeadlessFrame::default_fbo() const {
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

bool HeadlessFrame::make_current() {
    return context != EGL_NO_CONTEXT && eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

void HeadlessFrame::done_current() {
    if (display != EGL_NO_DISPLAY)
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

// The render thread takes the context over; the thread that made it gives it up.
void HeadlessFrame::prepare_for_render_thread() {
    done_current();
}

void HeadlessFrame::swap_buffers() {
    std::vector<uint8_t> rows(static_cast<size_t>(width) * height * 4);
    int previous = 0;
    gl<PFN_GetIntegerv>("glGetIntegerv")(GL_READ_FRAMEBUFFER_BINDING_, &previous);
    gl<PFN_BindFramebuffer>("glBindFramebuffer")(GL_READ_FRAMEBUFFER_, default_fbo());
    gl<PFN_PixelStorei>("glPixelStorei")(GL_PACK_ALIGNMENT_, 1);
    gl<PFN_ReadPixels>("glReadPixels")(0, 0, width, height, GL_BGRA_, GL_UNSIGNED_BYTE_, rows.data());
    gl<PFN_BindFramebuffer>("glBindFramebuffer")(GL_READ_FRAMEBUFFER_, static_cast<unsigned int>(previous));

    // GL's first row is the bottom one
    std::vector<uint8_t> picture(rows.size());
    const size_t stride = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; y++)
        std::memcpy(&picture[static_cast<size_t>(y) * stride], &rows[static_cast<size_t>(height - 1 - y) * stride], stride);

    {
        std::lock_guard<std::mutex> lock(mutex);
        latest = std::move(picture);
        count++;
    }
    cond.notify_all();
}

void HeadlessFrame::destroy_render_context() {
    if (display == EGL_NO_DISPLAY)
        return;
    done_current();
    if (context != EGL_NO_CONTEXT)
        eglDestroyContext(display, context);
    context = EGL_NO_CONTEXT;
}

uint64_t HeadlessFrame::wait_for(uint64_t n, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex);
    cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return count >= n; });
    return count;
}

std::vector<uint8_t> HeadlessFrame::picture() const {
    std::lock_guard<std::mutex> lock(mutex);
    return latest;
}

uint64_t HeadlessFrame::presented() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count;
}
