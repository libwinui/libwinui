// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"

#include <string>
#include <unordered_map>

namespace ui
{

/// The content an AnimatedVisualPlayer plays: an animation with a fixed size and length whose
/// render_frame() draws any point of it through Renderer.
///
/// Implement it: draw your animation in render_frame() and hand it to an AnimatedVisualPlayer.
///
/// A source has a fixed natural size and total duration, and render_frame() draws the frame at
/// a progress point. AnimatedVisualPlayer and AnimatedIcon hold a non-owning
/// `const AnimatedVisualSource*`, so the caller keeps the source alive at least as long as any
/// player or icon points at it. render_frame() is const and depends only on `progress`, so one
/// instance can serve many players at once.
///
/// Example:
/// @code
/// class Pulse final : public ui::AnimatedVisualSource
/// {
/// public:
///     [[nodiscard]] ui::Size natural_size() const override { return {24.0f, 24.0f}; }
///     [[nodiscard]] ui::Duration duration() const override { return ui::Duration{900.0}; }
///     void render_frame(ui::Renderer& r, const ui::Rect& bounds, double progress) const override
///     {
///         const float radius = std::min(bounds.width, bounds.height) * 0.5f * static_cast<float>(progress);
///         if (radius > 0.0f)
///         {
///             r.fill_ellipse(bounds.center(), radius, radius, ui::Color::from_rgba(0, 120, 215));
///         }
///     }
/// };
///
/// static const Pulse pulse; // must outlive the player
/// window.set_content(ui::AnimatedVisualPlayer{.source = &pulse, .width = 96, .height = 96});
/// @endcode
class AnimatedVisualSource
{
public:
    virtual ~AnimatedVisualSource() = default;

    /// The unstretched size this source draws at, in DIPs. AnimatedVisualPlayer fits it into its
    /// bounds per its Stretch with fit_stretch(), as Image does for a bitmap.
    [[nodiscard]] virtual Size natural_size() const = 0;

    /// How long one full [0,1] progress sweep takes, in milliseconds like every other duration
    /// in the library (Theme::anim_normal, ToolTipControl::delay()). A source made from an
    /// animation file reads it from the file. A source with no length, such as a pose that never
    /// moves, can return 0, which AnimatedVisualPlayerControl::duration() handles.
    [[nodiscard]] virtual Duration duration() const = 0;

    /// Draws one frame at normalized `progress` (0 = animation start, 1 = animation end) into
    /// `bounds`. It is called from render(), so what it draws may depend only on `progress` and
    /// the source's own fixed state, and it changes nothing, like every Element::render().
    virtual void render_frame(Renderer& renderer, const Rect& bounds, double progress) const = 0;

    /// Whether this source can be shown at all. A source that depends on something the running
    /// system lacks returns false, and AnimatedVisualPlayer shows its fallback content instead.
    [[nodiscard]] virtual bool available() const
    {
        return true;
    }
};

/// AnimatedIcon's source interface: an AnimatedVisualSource with named markers, each mapping a
/// state name (such as "PointerOver") to the normalized progress point of that state's pose.
/// AnimatedIconControl::set_state() looks the requested state up here and animates toward its
/// progress. A state with no matching marker leaves the current pose alone. The icon's color
/// arrives as render_icon_frame()'s `foreground`.
///
/// Implement it: draw your icon in render_icon_frame() and hand it to an AnimatedIcon.
///
/// Example:
/// @code
/// class PulseIcon final : public ui::AnimatedIconSource
/// {
/// public:
///     PulseIcon()
///     {
///         markers_["Normal"]      = 0.0;
///         markers_["PointerOver"] = 0.5;
///         markers_["Pressed"]     = 1.0;
///     }
///     [[nodiscard]] ui::Size natural_size() const override { return {24.0f, 24.0f}; }
///     [[nodiscard]] ui::Duration duration() const override { return ui::Duration{900.0}; }
///     void render_frame(ui::Renderer& r, const ui::Rect& b, double progress) const override
///     {
///         render_icon_frame(r, b, progress, ui::colors::Black);
///     }
///     void render_icon_frame(ui::Renderer& r, const ui::Rect& b, double progress,
///                            const ui::Color& foreground) const override
///     {
///         const float radius = 12.0f * static_cast<float>(progress);
///         r.fill_ellipse(b.center(), radius, radius, foreground);
///     }
///     [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override
///     {
///         return markers_;
///     }
///
/// private:
///     std::unordered_map<std::string, double> markers_;
/// };
///
/// static const PulseIcon pulse;
/// auto icon = std::make_unique<ui::AnimatedIconControl>();
/// icon->set_source(&pulse);
/// icon->set_state("PointerOver"); // animates from 0 toward 0.5
/// @endcode
class AnimatedIconSource : public AnimatedVisualSource
{
public:
    /// State name to the normalized progress (0..1) its pose sits at. Transition segments such
    /// as `NormalToPointerOver_Start`/`_End` are markers too; AnimatedIcon prefers them.
    [[nodiscard]] virtual const std::unordered_map<std::string, double>& markers() const = 0;

    /// render_frame() in the icon's foreground color, which AnimatedIcon passes with every draw.
    /// The source doesn't store the color, because one source object is shared by every icon
    /// showing it. A source with no themable color keeps this default, which ignores `foreground`.
    virtual void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& /*foreground*/) const
    {
        render_frame(renderer, bounds, progress);
    }
};

} // namespace ui
