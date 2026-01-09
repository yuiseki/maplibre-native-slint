#pragma once

#include <memory>
#include <mutex>
#include <vector>

// Slint
#include <slint.h>

// MapLibre
#include <mbgl/gfx/headless_frontend.hpp>
#include <mbgl/gfx/renderer_backend.hpp>
#include <mbgl/gl/renderable_resource.hpp>
#include <mbgl/gl/renderer_backend.hpp>
#include <mbgl/map/map.hpp>
#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/size.hpp>

// OpenGL Headers
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>

namespace mbgl {
class Renderer;
class TaggedScheduler;
}  // namespace mbgl

class SlintGLRenderable;

/**
 * Custom GL Backend that uses Slint's current OpenGL context.
 */
class SlintGLBackend final : public mbgl::gl::RendererBackend {
public:
    SlintGLBackend();
    ~SlintGLBackend() override;

    // gl::RendererBackend implementation
    void updateAssumedState() override;
    mbgl::gl::ProcAddress getExtensionFunctionPointer(const char*) override;

    // gfx::RendererBackend implementation
    mbgl::gfx::Renderable& getDefaultRenderable() override;

    void setSize(mbgl::Size);
    void setContext(void* cglContext) {
        m_context = cglContext;
    }

    void activate() override;
    void deactivate() override;

    mbgl::Size getSize() const {
        return size;
    }
    bool hasContext() const {
        return m_context != nullptr;
    }

private:
    mbgl::Size size;
    std::unique_ptr<SlintGLRenderable> renderable;
    void* m_context = nullptr;
    void* m_oldContext = nullptr;
};

/**
 * Custom Frontend that drives rendering manually.
 */
class SlintGLFrontend : public mbgl::RendererFrontend {
public:
    SlintGLFrontend(mbgl::Size size, float pixelRatio);
    ~SlintGLFrontend() override;

    // mbgl::RendererFrontend implementation
    void reset() override;
    void setObserver(mbgl::RendererObserver&) override;
    void update(std::shared_ptr<mbgl::UpdateParameters>) override;
    const mbgl::TaggedScheduler& getThreadPool() const override;

    // Returns the texture ID that MapLibre rendered to
    uint32_t renderToTexture();

    void setSize(mbgl::Size size);
    SlintGLBackend& getBackend();

private:
    SlintGLBackend backend;
    std::unique_ptr<mbgl::Renderer> renderer;
    std::shared_ptr<mbgl::UpdateParameters> updateParameters;
    mbgl::Size m_size;
    mbgl::Size m_currentTextureSize{0, 0};
    uint32_t m_texture = 0;
    uint32_t m_fbo = 0;
    uint32_t m_depth = 0;
    uint32_t m_vao = 0;
};

/**
 * Main Controller for the GL-based MapLibre integration.
 */
class SlintMapLibreGL : public mbgl::MapObserver {
public:
    SlintMapLibreGL();
    ~SlintMapLibreGL();

    void initialize(int width, int height);
    slint::Image render_map();
    void render_to_texture();  // Called from rendering notifier
    void resize(int width, int height);

    // Input handling
    void handle_mouse_press(float x, float y);
    void handle_mouse_release(float x, float y);
    void handle_mouse_move(float x, float y, bool pressed);
    void handle_double_click(float x, float y, bool shift);
    void handle_wheel_zoom(float x, float y, float dy);

    // Map control
    void set_pitch(int pitch_value);
    void set_bearing(float bearing_value);
    void setStyleUrl(const std::string& url);
    void fly_to(const std::string& location);

    // Animation & Loop
    void run_map_loop();
    bool take_repaint_request();
    void request_repaint();
    bool consume_forced_repaint();
    bool has_captured_context();

    // MBGL Observer implementation
    void onWillStartLoadingMap() override;
    void onDidFinishLoadingStyle() override;
    void onDidBecomeIdle() override;
    void onDidFailLoadingMap(mbgl::MapLoadError error,
                             const std::string& what) override;
    void onCameraDidChange(CameraChangeMode) override;
    void onSourceChanged(mbgl::style::Source&) override;
    void onDidFinishRenderingFrame(const RenderFrameStatus&) override;

private:
    std::unique_ptr<mbgl::util::RunLoop> run_loop;
    std::unique_ptr<SlintGLFrontend> frontend;
    std::unique_ptr<mbgl::Map> map;

    int width = 0;
    int height = 0;

    // State
    std::atomic<bool> style_loaded{false};
    std::atomic<bool> repaint_needed{false};
    std::atomic<int> forced_repaint_frames{0};

    mbgl::Point<double> last_pos;

    uint32_t m_textureId = 0;

    // Animation Helper
    void tick_animation();
    struct CustomAnim {
        bool active = false;
        mbgl::LatLng start_center;
        mbgl::LatLng target_center;
        double start_zoom = 0;
        double target_zoom = 0;
        double mid_zoom = 0;
        std::chrono::steady_clock::time_point start_time;
        int duration_ms = 0;
    } custom_anim;
};
