#include <iostream>
#include <memory>

#include "map_window.h"
#include "slint_maplibre_headless.hpp"

int main(int argc, char** argv) {
    std::cout << "[main] Starting application" << std::endl;
    auto main_window = MapWindow::create();
    auto slint_map = std::make_shared<SlintMapLibre>();

    auto initialized = std::make_shared<bool>(false);

    // Render: read frame from MapLibre and push to MMapAdapter
    auto render_function = [=]() {
        auto image = slint_map->render_map();
        main_window->global<MMapAdapter>().set_frame(image);

        // Update reactive camera state
        if (auto* m = slint_map->get_map()) {
            const auto cam = m->getCameraOptions();
            if (cam.center) {
                main_window->global<MMapAdapter>().set_current_lat(
                    static_cast<float>(cam.center->latitude()));
                main_window->global<MMapAdapter>().set_current_lon(
                    static_cast<float>(cam.center->longitude()));
            }
            if (cam.zoom)
                main_window->global<MMapAdapter>().set_current_zoom(
                    static_cast<float>(*cam.zoom));
            if (cam.bearing)
                main_window->global<MMapAdapter>().set_current_bearing(
                    static_cast<float>(*cam.bearing));
            if (cam.pitch)
                main_window->global<MMapAdapter>().set_current_pitch(
                    static_cast<float>(*cam.pitch));
        }
    };

    slint_map->setRenderCallback(render_function);

    // Render loop tick
    main_window->global<MMapAdapter>().on_tick([=]() {
        slint_map->run_map_loop();
        if (slint_map->take_repaint_request() ||
            slint_map->consume_forced_repaint()) {
            render_function();
        }
    });

    // User interactions
    main_window->global<MMapAdapter>().on_mouse_pressed(
        [=](float x, float y) { slint_map->handle_mouse_press(x, y); });

    main_window->global<MMapAdapter>().on_mouse_released(
        [=](float x, float y) { slint_map->handle_mouse_release(x, y); });

    main_window->global<MMapAdapter>().on_mouse_moved(
        [=](float x, float y) {
            slint_map->handle_mouse_move(x, y, true);
        });

    main_window->global<MMapAdapter>().on_double_clicked(
        [=](float x, float y, bool shift) {
            slint_map->handle_double_click(x, y, shift);
        });

    main_window->global<MMapAdapter>().on_wheel_zoomed(
        [=](float x, float y, float dy) {
            slint_map->handle_wheel_zoom(x, y, dy);
        });

    // Commands
    main_window->global<MMapAdapter>().on_request_style_change(
        [=](const slint::SharedString& url) {
            slint_map->setStyleUrl(std::string(url.data(), url.size()));
        });

    main_window->global<MMapAdapter>().on_request_fly_to(
        [=](float lat, float lon, float zoom) {
            slint_map->fly_to(static_cast<double>(lat),
                              static_cast<double>(lon),
                              static_cast<double>(zoom));
        });

    main_window->global<MMapAdapter>().on_request_pitch_change(
        [=](float pitch) {
            slint_map->set_pitch(static_cast<int>(pitch / 60.0f * 100.0f));
        });

    main_window->global<MMapAdapter>().on_request_bearing_change(
        [=](float bearing) {
            slint_map->set_bearing(bearing / 360.0f * 100.0f);
        });

    // Initialize/resize when map area size changes
    main_window->on_map_size_changed([=]() {
        const auto s = main_window->get_map_size();
        const int w = static_cast<int>(s.width);
        const int h = static_cast<int>(s.height);
        if (w > 0 && h > 0) {
            if (!*initialized) {
                slint_map->initialize(w, h);
                *initialized = true;
            } else {
                slint_map->resize(w, h);
            }
        }
    });

    std::cout << "[main] Entering UI event loop" << std::endl;
    main_window->run();
    return 0;
}
