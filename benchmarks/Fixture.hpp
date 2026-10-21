// SPDX-License-Identifier: MIT
// What every scenario lays out and draws through: a headless window, and either the deterministic
// measurer with a TestRenderer ("headless") or DirectWrite and the real D2DRenderer into an
// offscreen bitmap ("d2d").
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/OffscreenCapture.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Window.hpp"

#include <string>

namespace bench
{

enum class Backend
{
    Headless,
    D2D,
};

inline const char* name(Backend backend)
{
    return backend == Backend::Headless ? "headless" : "d2d";
}

// One per process: creating the Direct2D, DirectWrite and WIC factories is not what is measured.
ui::testing::OffscreenCapture& offscreen();

struct Fixture
{
    explicit Fixture(Backend backend, ui::Size client = {800.0f, 600.0f}) : backend(backend), client(client)
    {
    }

    void layout()
    {
        if (backend == Backend::Headless)
        {
            ui.layout(client);
        }
        else
        {
            window().layout(client, offscreen().measurer());
        }
    }

    // The way the window host paints: only what Window::take_damage() reports.
    void render()
    {
        if (backend == Backend::Headless)
        {
            const ui::Rect all{0.0f, 0.0f, client.width, client.height};
            for (const ui::Rect& area : window().take_damage())
            {
                ui::testing::TestRenderer renderer;
                if (area == all)
                {
                    window().render(renderer);
                }
                else
                {
                    window().render(renderer, area);
                }
            }
        }
        else
        {
            (void)offscreen().capture_changes(window(), client);
        }
    }

    void frame()
    {
        layout();
        render();
    }

    [[nodiscard]] ui::Window& window()
    {
        return ui.window();
    }

    Backend                 backend;
    ui::Size                client;
    ui::testing::HeadlessUI ui = ui::testing::create_headless();
};

} // namespace bench
