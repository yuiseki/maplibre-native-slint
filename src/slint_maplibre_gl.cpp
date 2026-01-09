#include "slint_maplibre_gl.hpp"

#include <cmath>
#include <iostream>
#include <mbgl/gfx/backend_scope.hpp>
#include <mbgl/gfx/renderable.hpp>
#include <mbgl/gl/context.hpp>
#include <mbgl/map/map_options.hpp>
#include <mbgl/renderer/renderer.hpp>
#include <mbgl/renderer/renderer_observer.hpp>
#include <mbgl/renderer/update_parameters.hpp>
#include <mbgl/storage/resource_options.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/util/logging.hpp>
#include <mbgl/util/projection.hpp>
#include <thread>

// Redefine Size to avoid conflict with MacTypes.h if included indirectly
#define Size MacSize
#include <CoreFoundation/CoreFoundation.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#undef Size

#define CHECK_GL_ERROR(msg)                                                \
    {                                                                      \
        GLenum err = glGetError();                                         \
        if (err != GL_NO_ERROR)                                            \
            std::cerr << "[GL Error] " << msg << ": " << err << std::endl; \
    }

// --- SlintGLRenderable and Helpers ---

class SlintGLRenderableResource final : public mbgl::gl::RenderableResource {
public:
    void bind() override {
        // We assume the caller (SlintGLFrontend) has bound the correct FBO
    }
};

class SlintGLRenderable final : public mbgl::gfx::Renderable {
public:
    SlintGLRenderable(mbgl::Size size_, SlintGLBackend& backend_)
        : mbgl::gfx::Renderable(size_,
                                std::make_unique<SlintGLRenderableResource>()) {
    }

    // We make destructor public here so SlintGLBackend can own it in unique_ptr
    ~SlintGLRenderable() override = default;
};

// --- SlintGLBackend implementation ---

SlintGLBackend::SlintGLBackend()
    : mbgl::gl::RendererBackend(mbgl::gfx::ContextMode::Shared) {
}

SlintGLBackend::~SlintGLBackend() = default;

void SlintGLBackend::updateAssumedState() {
    if (context) {
        static_cast<mbgl::gl::Context&>(*context).setDirtyState();
    }
}

mbgl::gl::ProcAddress SlintGLBackend::getExtensionFunctionPointer(
    const char* name) {
    static CFBundleRef framework =
        CFBundleGetBundleWithIdentifier(CFSTR("com.apple.opengl"));
    if (!framework) {
        return nullptr;
    }
    CFStringRef symbol = CFStringCreateWithCString(kCFAllocatorDefault, name,
                                                   kCFStringEncodingUTF8);
    void* addr = CFBundleGetFunctionPointerForName(framework, symbol);
    CFRelease(symbol);
    return reinterpret_cast<mbgl::gl::ProcAddress>(addr);
}

mbgl::gfx::Renderable& SlintGLBackend::getDefaultRenderable() {
    if (!renderable) {
        renderable = std::make_unique<SlintGLRenderable>(size, *this);
    }
    return *renderable;
}

void SlintGLBackend::setSize(mbgl::Size size_) {
    size = size_;
    if (renderable) {
        renderable = std::make_unique<SlintGLRenderable>(size, *this);
    }
}

void SlintGLBackend::activate() {
    if (m_context) {
        m_oldContext = CGLGetCurrentContext();
        if (m_context != m_oldContext) {
            CGLSetCurrentContext(static_cast<CGLContextObj>(m_context));
        }
    }
}

void SlintGLBackend::deactivate() {
    if (m_oldContext && m_context != m_oldContext) {
        CGLSetCurrentContext(static_cast<CGLContextObj>(m_oldContext));
    }
    m_oldContext = nullptr;
}

// --- SlintGLFrontend implementation ---

SlintGLFrontend::SlintGLFrontend(mbgl::Size size, float pixelRatio)
    : backend(), m_size(size) {
    backend.setSize(size);
    renderer = std::make_unique<mbgl::Renderer>(backend, pixelRatio);
}

SlintGLFrontend::~SlintGLFrontend() {
    if (m_texture != 0)
        glDeleteTextures(1, &m_texture);
    if (m_depth != 0)
        glDeleteRenderbuffers(1, &m_depth);
    if (m_fbo != 0)
        glDeleteFramebuffers(1, &m_fbo);
    if (m_vao != 0)
        glDeleteVertexArrays(1, &m_vao);
}

void SlintGLFrontend::reset() {
    if (renderer)
        renderer.reset();
}

void SlintGLFrontend::setObserver(mbgl::RendererObserver& observer) {
    if (renderer)
        renderer->setObserver(&observer);
}

void SlintGLFrontend::update(std::shared_ptr<mbgl::UpdateParameters> params) {
    updateParameters = std::move(params);
}

const mbgl::TaggedScheduler& SlintGLFrontend::getThreadPool() const {
    return const_cast<SlintGLBackend&>(backend).getThreadPool();
}

uint32_t SlintGLFrontend::renderToTexture() {
    // SAVE/SET CONTEXT early so SlintMapLibreGL::has_captured_context() becomes
    // true
    void* currentContext = CGLGetCurrentContext();
    if (currentContext) {
        backend.setContext(currentContext);
    }

    if (!updateParameters || !renderer)
        return 0;

    // Ensure we have our FBO and Texture and they match current size
    if (m_texture == 0) {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_size.width, m_size.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        CHECK_GL_ERROR("texture creation");

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_texture, 0);

        glGenRenderbuffers(1, &m_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              m_size.width, m_size.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, m_depth);
        CHECK_GL_ERROR("FBO creation");

        glGenVertexArrays(1, &m_vao);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Framebuffer incomplete" << std::endl;
        }
        m_currentTextureSize = m_size;
    } else if (m_currentTextureSize != m_size) {
        // Resize existing texture/renderbuffer
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_size.width, m_size.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              m_size.width, m_size.height);

        m_currentTextureSize = m_size;
        CHECK_GL_ERROR("resizing texture/depth");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Perform rendering within the BackendScope
    {
        mbgl::gfx::BackendScope scope{backend};

        // IMPORTANT: Tell MapLibre that Slint might have changed GL state
        backend.updateAssumedState();

        // Render!
        renderer->render(updateParameters);
    }

    // EXHAUSTIVE CLEANUP for Slint compatibility
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Re-assert parameters for our texture
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glFinish();  // Total synchronization

    return m_texture;
}

SlintGLBackend& SlintGLFrontend::getBackend() {
    return backend;
}

void SlintGLFrontend::setSize(mbgl::Size size) {
    if (m_size == size)
        return;
    m_size = size;
    backend.setSize(m_size);
    // We don't delete here to avoid race conditions with Slint's UI thread.
    // Deletion and re-creation happens in renderToTexture.
}

// --- SlintMapLibreGL implementation ---

SlintMapLibreGL::SlintMapLibreGL()
    : run_loop(std::make_unique<mbgl::util::RunLoop>()) {
}

SlintMapLibreGL::~SlintMapLibreGL() = default;

void SlintMapLibreGL::initialize(int w, int h) {
    width = w;
    height = h;
    mbgl::Size size{static_cast<uint32_t>(w), static_cast<uint32_t>(h)};

    frontend = std::make_unique<SlintGLFrontend>(size, 1.0f);

    mbgl::ResourceOptions resourceOptions;
    resourceOptions.withCachePath(".mbgl-cache.db").withAssetPath(".");

    mbgl::MapOptions mapOptions;
    mapOptions.withMapMode(mbgl::MapMode::Continuous)
        .withSize(size)
        .withPixelRatio(1.0f);

    map = std::make_unique<mbgl::Map>(*frontend, *this, mapOptions,
                                      resourceOptions, mbgl::ClientOptions());

    // Default style
    map->getStyle().loadURL("https://demotiles.maplibre.org/style.json");
}

void SlintMapLibreGL::render_to_texture() {
    if (frontend) {
        m_textureId = frontend->renderToTexture();
        static bool logged = false;
        if (!logged && m_textureId != 0) {
            std::cout << "[Notifier] Rendered to texture: " << m_textureId
                      << " on Thread: " << std::this_thread::get_id()
                      << " Context: " << CGLGetCurrentContext() << std::endl;
            logged = true;
        }
    }
}

slint::Image SlintMapLibreGL::render_map() {
    if (width <= 0 || height <= 0 || m_textureId == 0) {
        return {};
    }

    static bool logged = false;
    if (!logged) {
        std::cout << "[UI] render_map using texture: " << m_textureId
                  << " on Thread: " << std::this_thread::get_id()
                  << " Context: " << CGLGetCurrentContext() << std::endl;
        logged = true;
    }

    return slint::Image::create_from_borrowed_gl_2d_rgba_texture(
        m_textureId,
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
        slint::Image::BorrowedOpenGLTextureOrigin::BottomLeft);
}

void SlintMapLibreGL::resize(int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    width = w;
    height = h;
    m_textureId = 0;  // Invalidate texture ID to avoid drawing deleted texture
    mbgl::Size size{static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    if (frontend) {
        frontend->setSize(size);
    }
    if (map) {
        map->setSize(size);
    }
}

bool SlintMapLibreGL::has_captured_context() {
    return frontend && frontend->getBackend().hasContext();
}

void SlintMapLibreGL::run_map_loop() {
    if (frontend && frontend->getBackend().hasContext()) {
        mbgl::gfx::BackendScope scope{frontend->getBackend()};
        run_loop->runOnce();
    }
}

bool SlintMapLibreGL::take_repaint_request() {
    return repaint_needed.exchange(false);
}

void SlintMapLibreGL::request_repaint() {
    repaint_needed.store(true);
}

bool SlintMapLibreGL::consume_forced_repaint() {
    int expected = forced_repaint_frames.load();
    if (expected > 0) {
        forced_repaint_frames.store(expected - 1);
        return true;
    }
    return false;
}

// MapObserver implementation
void SlintMapLibreGL::onWillStartLoadingMap() {
}
void SlintMapLibreGL::onDidFinishLoadingStyle() {
    style_loaded.store(true);
}
void SlintMapLibreGL::onDidBecomeIdle() {
}
void SlintMapLibreGL::onDidFailLoadingMap(mbgl::MapLoadError,
                                          const std::string& what) {
    std::cerr << "Map loading failed: " << what << std::endl;
}
void SlintMapLibreGL::onCameraDidChange(CameraChangeMode) {
    request_repaint();
}
void SlintMapLibreGL::onSourceChanged(mbgl::style::Source&) {
    request_repaint();
}
void SlintMapLibreGL::onDidFinishRenderingFrame(const RenderFrameStatus&) {
}

// Input Handlers
void SlintMapLibreGL::handle_mouse_press(float x, float y) {
    last_pos = {x, y};
}
void SlintMapLibreGL::handle_mouse_release(float, float) {
}
void SlintMapLibreGL::handle_mouse_move(float x, float y, bool pressed) {
    if (pressed) {
        double dx = x - last_pos.x;
        double dy = y - last_pos.y;
        map->moveBy({dx, dy});
        last_pos = {x, y};
    }
}
void SlintMapLibreGL::handle_double_click(float x, float y, bool shift) {
    if (shift) {
        map->scaleBy(0.5, mbgl::ScreenCoordinate{x, y});
    } else {
        map->scaleBy(2.0, mbgl::ScreenCoordinate{x, y});
    }
}
void SlintMapLibreGL::handle_wheel_zoom(float x, float y, float dy) {
    double factor = std::pow(2.0, dy / 100.0);
    map->scaleBy(factor, mbgl::ScreenCoordinate{x, y});
}

void SlintMapLibreGL::set_pitch(int pitch_value) {
    map->jumpTo(
        mbgl::CameraOptions().withPitch(static_cast<double>(pitch_value)));
}
void SlintMapLibreGL::set_bearing(float bearing_value) {
    map->jumpTo(
        mbgl::CameraOptions().withBearing(static_cast<double>(bearing_value)));
}
void SlintMapLibreGL::setStyleUrl(const std::string& url) {
    map->getStyle().loadURL(url);
}
void SlintMapLibreGL::fly_to(const std::string& location) {
    map->easeTo(
        mbgl::CameraOptions().withZoom(0.0),
        mbgl::AnimationOptions(mbgl::Duration(std::chrono::seconds(2))));
}
