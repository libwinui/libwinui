// SPDX-License-Identifier: MIT
#pragma once
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Window.hpp"

#include <memory>
#include <vector>

namespace ui::testing
{

/// Renders a laid-out Window to a Bitmap with the same renderer an on-screen window uses, with no
/// native window and no GPU. The pixel golden tests capture through it.
///
/// Draws into a software bitmap with grayscale text anti-aliasing, so output is stable run to run
/// and independent of the monitor. Lay the window out with measurer() first so text metrics match
/// what capture() paints. On a platform the library has no renderer for, ok() is false and every
/// capture is an empty bitmap. Not copyable.
///
/// Example:
/// @code
/// ui::testing::OffscreenCapture d2d;
/// REQUIRE(d2d.ok());
///
/// ui::Window window;
/// window.set_content(ui::Button{.text = "Hello"});
/// const ui::Size client{200, 60};
/// window.layout(client, d2d.measurer());
///
/// const ui::Bitmap image = d2d.capture(window, client, ui::Theme::light().colors().background);
/// REQUIRE(image.width() == 200);
/// @endcode
class OffscreenCapture
{
public:
    /// Sets up the renderer; check ok() afterwards.
    OffscreenCapture();
    /// Releases the renderer's resources.
    ~OffscreenCapture();
    /// Not copyable: owns renderer resources.
    OffscreenCapture(const OffscreenCapture&) = delete;
    /// Not copyable: owns renderer resources.
    OffscreenCapture& operator=(const OffscreenCapture&) = delete;

    /// True if the renderer was set up and capture() can draw.
    [[nodiscard]] bool ok() const;

    /// The text measurer the renderer uses. Lay the window out with it so text metrics match
    /// what capture() paints. Owned by this object; valid for its lifetime.
    [[nodiscard]] const TextMeasurer& measurer() const;

    /// Renders an already laid-out window at `client` pixels (1 DIP == 1 px) to an RGBA image,
    /// over `background`. Pass the theme's window background, since Fluent fills are
    /// translucent. Returns an empty image if the renderer is not ok().
    [[nodiscard]] Bitmap capture(const Window& window, Size client, const Color& background = colors::White);

    /// capture() the way a window repaints. Over what the last capture of the same size left,
    /// only the areas Window::take_damage() reports are cleared and drawn again, and the whole
    /// image the first time. A test of the partial repaint checks that the result matches a full
    /// capture().
    [[nodiscard]] Bitmap capture_changes(Window& window, Size client, const Color& background = colors::White);

private:
    template <typename Areas>
    Bitmap draw(const Window& window, Size client, const Color& background, Areas&& areas_for);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ui::testing
