// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <chrono>

#include <cmath>

namespace ui::detail
{

/// What `elapsed_ms` starts at before a list has ever opened: long over.
inline constexpr double ListSlideIdle = 1.0e9;

[[nodiscard]] inline bool list_slide_running(const Theme::PopupMotion& motion, double elapsed_ms)
{
    const double length = motion.open.count();
    return motion.list == ThemePopupAnimation::Slide && length > 0.0 && elapsed_ms < length;
}

/// Advances the slide; true while it is still running, so the owner keeps invalidating.
inline bool tick_list_slide(const Theme::PopupMotion& motion, double& elapsed_ms, double delta_ms)
{
    if (!list_slide_running(motion, elapsed_ms))
    {
        return false;
    }
    elapsed_ms += delta_ms;
    return true;
}

/// Pushes the clip and offset for the list at `list`, which hangs below its control, or above it
/// when `from_below`. Returns whether anything was pushed, i.e. whether to pop_list_slide().
inline bool push_list_slide(Renderer& renderer, const Theme& theme, double elapsed_ms, const Rect& list, bool from_below)
{
    if (!list_slide_running(theme.popup_motion, elapsed_ms))
    {
        return false;
    }
    // Linear, like AnimateWindow's AW_SLIDE that the era's combo box opens with.
    const double length   = theme.popup_motion.open.count();
    const float  progress = static_cast<float>(elapsed_ms / length);
    const float  offset   = (1.0f - progress) * list.height * (from_below ? 1.0f : -1.0f);
    renderer.push_clip(list);
    renderer.push_transform(Matrix::translation(0.0f, offset));
    return true;
}

inline void pop_list_slide(Renderer& renderer)
{
    renderer.pop_transform();
    renderer.pop_clip();
}

// A drop-down list's width: its control's, `anchor_width`, within the `available` width the window
// offers, which reaches to the window's edge.
[[nodiscard]] inline float list_width(float anchor_width, float available)
{
    return std::isfinite(available) ? std::min(anchor_width, available) : anchor_width;
}

// The accent pill left of the selected row: 3x16 DIP, 1 DIP in from the edge.
inline void draw_selection_pill(Renderer& renderer, const Rect& row, const Color& fill)
{
    constexpr float Width  = 3.0f;
    constexpr float Height = 16.0f;
    constexpr float Radius = 1.5f;
    constexpr float Margin = 1.0f;
    const Rect      pill{row.left() + Margin, row.top() + (row.height - Height) * 0.5f, Width, Height};
    renderer.fill_rounded_rect(pill, Radius, fill);
}

} // namespace ui::detail
