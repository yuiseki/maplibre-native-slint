#include <iostream>
#include <memory>
#include <vector>

// macOS IOSurface support (Must be included with Size hack to avoid conflict
// with Slint)
#define Size MacSize
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLIOSurface.h>
#undef Size

#include "map_window.h"

// Switch to the new GL backend
#include "slint_maplibre_gl.hpp"
// #include "slint_maplibre_headless.hpp"
#include <mbgl/util/exception.hpp>

int main(int argc, char** argv) {
    try {
        std::cout << "[main] Starting application (Direct Context Zero-Copy)"
                  << std::endl;
        auto main_window = MapWindow::create();

        // Use the GL-based implementation
        auto slint_map_libre = std::make_shared<SlintMapLibreGL>();

        // Register rendering notifier for Zero-Copy
        main_window->window().set_rendering_notifier(
            [slint_map_libre](slint::RenderingState state,
                              slint::GraphicsAPI api) {
                if (state == slint::RenderingState::BeforeRendering &&
                    api == slint::GraphicsAPI::NativeOpenGL) {
                    slint_map_libre->render_to_texture();
                }
            });

        // Trigger an initial redraw to capture the context as early as possible
        slint::invoke_from_event_loop(
            [main_window]() { main_window->window().request_redraw(); });

        auto initialized = std::make_shared<bool>(false);
        auto style_initiated = std::make_shared<bool>(false);
        static bool redraw_pending = false;

        // Render callback (periodic from Slint loop via Timer in .slint)
        main_window->global<MapAdapter>().on_tick_map_loop([=]() {
            // Ensure we have a context and start style load if not yet done
            if (!*style_initiated && slint_map_libre->has_captured_context()) {
                if (!*initialized) {
                    slint_map_libre->initialize(776, 259);
                    *initialized = true;
                }
                slint_map_libre->setStyleUrl(
                    "https://demotiles.maplibre.org/style.json");
                *style_initiated = true;
                // Request first redraw to get things moving
                slint::invoke_from_event_loop([main_window]() {
                    main_window->window().request_redraw();
                });
            }

            // Background tasks only if we have a context
            if (!slint_map_libre->take_repaint_request()) {
                slint_map_libre->run_map_loop();
            }

            if (slint_map_libre->take_repaint_request() && !redraw_pending) {
                redraw_pending = true;
                slint::invoke_from_event_loop([main_window]() {
                    main_window->window().request_redraw();
                    redraw_pending = false;
                });
            }
        });

        main_window->global<MapAdapter>().on_style_changed(
            [=](slint::SharedString url) {
                slint_map_libre->setStyleUrl(std::string(url));
            });

        // Connect mouse events
        main_window->global<MapAdapter>().on_mouse_press([=](float x, float y) {
            slint_map_libre->handle_mouse_press(x, y);
        });

        main_window->global<MapAdapter>().on_mouse_release(
            [=](float x, float y) {
                slint_map_libre->handle_mouse_release(x, y);
            });

        main_window->global<MapAdapter>().on_mouse_move(
            [=](float x, float y, bool pressed) {
                slint_map_libre->handle_mouse_move(x, y, pressed);
            });

        // Double click zoom with Shift for zoom-out
        main_window->global<MapAdapter>().on_double_click_with_shift(
            [=](float x, float y, bool shift) {
                slint_map_libre->handle_double_click(x, y, shift);
            });

        // Wheel zoom
        main_window->global<MapAdapter>().on_wheel_zoom(
            [=](float x, float y, float dy) {
                slint_map_libre->handle_wheel_zoom(x, y, dy);
            });

        // Pitch and bearing controls
        main_window->global<MapAdapter>().on_pitch_changed(
            [=](int pitch_value) { slint_map_libre->set_pitch(pitch_value); });

        main_window->global<MapAdapter>().on_bearing_changed(
            [=](float bearing_value) {
                slint_map_libre->set_bearing(bearing_value);
            });

        main_window->global<MapAdapter>().on_fly_to(
            [=](slint::SharedString location) {
                slint_map_libre->fly_to(std::string(location));
            });

        // Handle Resizing
        main_window->on_map_size_changed([=]() {
            try {
                const auto s = main_window->get_map_size();
                const int w = static_cast<int>(s.width);
                const int h = static_cast<int>(s.height);
                if (w > 0 && h > 0) {
                    std::cout << "Map Area Size Changed: " << w << "x" << h
                              << std::endl;
                    if (!*initialized) {
                        slint_map_libre->initialize(w, h);
                        *initialized = true;
                    } else {
                        slint_map_libre->resize(w, h);
                    }
                    main_window->global<MapAdapter>().set_map_texture(
                        slint_map_libre->render_map());
                }
            } catch (const std::exception& e) {
                std::cerr << "[ERROR in resize] " << e.what() << std::endl;
            }
        });

        std::cout << "[main] Entering UI event loop" << std::endl;
        main_window->run();

    } catch (const mbgl::util::MisuseException& e) {
        std::cerr << "MAPLIBRE MISUSE ERROR: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
    return 0;
}
