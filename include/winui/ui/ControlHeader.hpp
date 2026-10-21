// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/LayoutContext.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace ui
{

class Element;

/// The `Header` label an input control (Slider, ComboBox, ToggleSwitch, TextBox, NumberBox, ...)
/// carries above itself. It draws wrapping text in the primary text color, or in the disabled
/// color while the control is disabled, takes no room while empty, and leaves a per-control gap
/// below it.
///
/// It is not an Element. The control owns one as a plain member and calls it from its own
/// measure, arrange and render, so the element tree stays the same. arrange() takes the header
/// off the top of the control's rect and leaves the rest as the control's bounds, so hit tests,
/// focus rings and popup anchors still cover only the control itself.
///
/// Example:
/// @code
/// class LabeledThing final : public ui::Element
/// {
/// public:
///     void set_header(std::string text)
///     {
///         if (header_.set_text(std::move(text)))
///         {
///             mark_dirty(ui::DirtyFlags::Layout | ui::DirtyFlags::Visual);
///         }
///     }
///
/// protected:
///     ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
///     {
///         const ui::Size header = header_.measure(available.width, context);
///         const ui::Size room   = ui::ControlHeader::below(available, header);
///         const ui::Size body{120.0f, std::min(room.height, 32.0f)};
///         return ui::ControlHeader::above(body, header);
///     }
///     void arrange_override(const ui::Rect&, const ui::ArrangeContext&) override
///     {
///         header_.arrange(*this); // bounds() is now the body's rect
///     }
///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
///     {
///         const ui::Theme& theme = context.theme;
///         header_.render(renderer, theme, /*enabled=*/true);
///         renderer.fill_rounded_rect(bounds(), 4.0f, theme.colors().background); // the body
///     }
///
/// private:
///     ui::ControlHeader header_{4.0f}; // gap below the header, DIPs
/// };
/// @endcode
class ControlHeader
{
public:
    /// `gap` is the space in DIPs between the header and the control below it (4 for a Slider).
    /// It applies only while the header has text.
    explicit ControlHeader(float gap) : gap_(gap)
    {
    }

    /// The header text; empty (the default) collapses the header entirely.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Returns whether the text changed, so the caller knows to mark itself dirty for layout and
    /// visuals, since the header's height depends on its text.
    bool set_text(std::string text)
    {
        if (text == text_)
        {
            return false;
        }
        text_ = std::move(text);
        return true;
    }
    /// True while there is no text. The header then takes no room and draws nothing.
    [[nodiscard]] bool empty() const
    {
        return text_.empty();
    }

    /// The room the header takes above the control, in DIPs: its width, and its height plus the
    /// gap. Zero when empty. It wraps at `available_width` when that is bounded (> 0). arrange()
    /// uses the height from the last call.
    [[nodiscard]] Size measure(float available_width, const MeasureContext& context) const;

    /// `available` with the header's height taken out, for measuring the control below it. An
    /// unbounded height stays unbounded.
    [[nodiscard]] static Size below(Size available, Size header)
    {
        return Size{available.width, (std::max)(0.0f, available.height - header.height)};
    }
    /// The control's own desired size with the header stacked on top of it: the wider of the
    /// two, and the heights summed.
    [[nodiscard]] static Size above(Size control, Size header)
    {
        return Size{(std::max)(control.width, header.width), control.height + header.height};
    }

    /// Takes the header (as last measured) off the top of `control`'s bounds(), which are left as
    /// the control's own rect. Call it from the control's arrange_override(). A no-op while empty.
    void arrange(Element& control);

    /// Draws the header where arrange() put it, in the theme's disabled look while the control is
    /// disabled.
    void render(Renderer& renderer, const Theme& theme, bool enabled) const;

private:
    friend class ControlDescription; // shares the label style

    [[nodiscard]] static TextStyle style(const Theme& theme);

    std::string   text_;
    float         gap_;
    mutable float height_ = 0.0f; // text height from the last measure(), gap excluded
    Rect          rect_{};
};

/// The `description` an input control (TextBox, PasswordBox, NumberBox, AutoSuggestBox,
/// RichEditBox, ComboBox) carries under itself. It draws wrapping text in the secondary text color
/// and takes no room while empty. While the control's `error` is set, the error shows in its place
/// in the critical color.
///
/// It works like ControlHeader, below the control instead of above it. measure() it along with
/// the header, take its height out of the room the control measures in, and arrange() it off the
/// bottom of the bounds.
///
/// Example:
/// @code
/// class DescribedThing : public ui::Element
/// {
///     ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
///     {
///         const ui::Size header      = header_.measure(available.width, context);
///         const ui::Size description = description_.measure(available.width, context);
///         const ui::Size under_head  = ui::ControlHeader::below(available, header);
///         const ui::Size room        = ui::ControlHeader::below(under_head, description);
///         const ui::Size body{120.0f, std::min(room.height, 32.0f)};
///         return ui::ControlHeader::above(ui::ControlHeader::above(body, header), description);
///     }
///     void arrange_override(const ui::Rect&, const ui::ArrangeContext&) override
///     {
///         header_.arrange(*this);
///         description_.arrange(*this); // bounds() is now the body's rect
///     }
///
///     ui::ControlHeader      header_{4.0f};
///     ui::ControlDescription description_;
/// };
/// @endcode
class ControlDescription
{
public:
    /// The description text; empty (the default) collapses it entirely.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Returns whether the text changed, so the caller knows to mark itself dirty for layout and
    /// visuals.
    bool set_text(std::string text)
    {
        if (text == text_)
        {
            return false;
        }
        text_ = std::move(text);
        return true;
    }
    /// The error text; empty (the default) shows the description.
    [[nodiscard]] const std::string& error() const
    {
        return error_;
    }
    /// Returns whether the error changed, so the caller knows to mark itself dirty.
    bool set_error(std::string error)
    {
        if (error == error_)
        {
            return false;
        }
        error_ = std::move(error);
        return true;
    }
    /// True while an error is set.
    [[nodiscard]] bool has_error() const
    {
        return !error_.empty();
    }
    /// What shows under the control: the error while one is set, else the description.
    [[nodiscard]] const std::string& shown() const
    {
        return error_.empty() ? text_ : error_;
    }
    /// True while there is nothing to show: it takes no room and draws nothing.
    [[nodiscard]] bool empty() const
    {
        return shown().empty();
    }

    /// The room the description takes under the control, in DIPs. Zero when empty. It wraps at
    /// `available_width` when that is bounded (> 0).
    [[nodiscard]] Size measure(float available_width, const MeasureContext& context) const;
    /// Takes the description (as last measured) off the bottom of `control`'s bounds(), which are
    /// left as the control's own rect. Call it from the control's arrange_override(). A no-op
    /// while empty.
    void arrange(Element& control);
    /// Draws the description where arrange() put it, in the secondary text color (the disabled
    /// color while the control is disabled), or the error in the critical color.
    void render(Renderer& renderer, const Theme& theme, bool enabled) const;

private:
    std::string   text_;
    std::string   error_;
    mutable float height_ = 0.0f;
    Rect          rect_{};
};

} // namespace ui
