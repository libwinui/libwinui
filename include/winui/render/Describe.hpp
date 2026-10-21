// SPDX-License-Identifier: MIT
#pragma once

#include "winui/render/Renderer.hpp"

#include <string>
#include <vector>

namespace ui
{

/// A stable text dump of a render-command stream: one line per command, fields in a fixed order,
/// floats at two decimals, colors as `#RRGGBBAA`. Golden-file tests compare against it, since it
/// doesn't depend on resolution, fonts or the operating system.
///
/// Image and SVG commands print their sizes, not their contents. ui::testing::describe() prints a
/// whole window.
///
/// Example:
/// @code
/// // A renderer of your own, an export backend say, can keep each call as a RenderCommand
/// // and log a frame in this format.
/// class ExportRenderer : public ui::Renderer
/// {
/// public:
///     void fill_rounded_rect(const ui::Rect& bounds, float radius, const ui::Color& color) override
///     {
///         frame_.push_back(ui::FillRoundedRect{bounds, radius, color});
///         // ... draw it ...
///     }
///
///     // After an element has rendered into it:
///     void log_frame() const
///     {
///         std::println("{}", ui::describe(frame_));
///         //   FillRoundedRect rect=(0.00,0.00,10.00,10.00) radius=2.00 color=`#FFFFFFFF`
///     }
///
/// private:
///     std::vector<ui::RenderCommand> frame_;
/// };
/// @endcode
[[nodiscard]] std::string describe(const std::vector<RenderCommand>& commands);

} // namespace ui
