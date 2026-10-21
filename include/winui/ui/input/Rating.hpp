// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

/// A row of stars the user rates with a click, a drag or the keyboard.
///
/// `value()` is unset until something is rated, then a number in [1, `max_rating()`].
/// Interaction always commits a whole number, and a click or drag commits only on release (the
/// stars preview the pointer meanwhile; dragging off the left edge and releasing clears).
/// Clicking the star that already is the rating clears it while `is_clear_enabled()`. A
/// fractional value set by the app is drawn as a clipped partial star, and a
/// `placeholder_value()` (say, the average of other users' ratings) is shown while the user
/// has not rated. `set_read_only()` stops user input but keeps the full-strength look
/// and the tab stop. `ui::Rating` is the declarative form.
///
/// Example:
/// @code
/// ui::Property<std::optional<double>> stars;
///
/// window.set_content(ui::Rating{
///     .value             = stars,
///     .placeholder_value = 3.5,            // the community average, until the user rates
///     .caption           = "312 ratings",
///     .horizontal_alignment           = ui::Alignment::Leading,
///     // stars is unset again when cleared
/// });
///
/// // Imperatively, a read-only average:
/// auto average = std::make_unique<ui::RatingControl>(/*max_rating=*/5, /*value=*/4.5);
/// average->set_read_only(true);
/// average->set_caption("4.5 average");
/// @endcode
class RatingControl final : public Element, private RangeValueProvider
{
public:
    /// A row of `max_rating` stars (at least 1) holding `value`, coerced like `set_value()`.
    explicit RatingControl(std::size_t max_rating = 5, std::optional<double> value = std::nullopt);

    // ---- rating ----
    /// The number of stars; 5 by default.
    [[nodiscard]] std::size_t max_rating() const
    {
        return static_cast<std::size_t>(max_rating_);
    }
    /// Clamped to >= 1. Re-clamps value()/placeholder_value() into the new range, firing
    /// on_value_changed() only if that moves value().
    void set_max_rating(std::size_t max_rating);

    /// Unset until rated, otherwise a value in [1, max_rating()] (interaction always lands
    /// on a whole number within that range; a fraction is reachable only via set_value()).
    [[nodiscard]] std::optional<double> value() const
    {
        return value_;
    }
    /// Sets or clears the rating. A value up to 1 rounds up to 1.0, anything above
    /// max_rating() clamps down to it. Does nothing, and fires no callback, if the result is the
    /// current value.
    void set_value(std::optional<double> value);

    /// Unset by default. A grayed preview, such as an average rating, shown only while value()
    /// is unset and the pointer is not over the control.
    [[nodiscard]] std::optional<double> placeholder_value() const
    {
        return placeholder_value_;
    }
    /// Clamped to [0, max_rating()], keeping 0 and fractions below 1 as they are, since a
    /// placeholder is a display hint. No-op if unchanged; never fires on_value_changed.
    void set_placeholder_value(std::optional<double> value);

    /// Whether clicking the current rating's star clears it; true by default.
    [[nodiscard]] bool is_clear_enabled() const
    {
        return is_clear_enabled_;
    }
    /// Whether clicking the star already representing the current rating clears it
    /// (default: true).
    void set_clear_enabled(bool enabled)
    {
        is_clear_enabled_ = enabled;
    }

    /// Where a keyboard step lands from an unset value; 1 by default.
    [[nodiscard]] int initial_set_value() const
    {
        return initial_set_value_;
    }
    /// The rating a keyboard/gamepad step lands on when stepping from an unset value()
    /// (default: 1). Clamped to >= 1.
    void set_initial_set_value(int value);

    /// The label above the stars; empty by default.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// A label above the stars, which also names the control for screen readers when
    /// accessibility().name is empty. Empty (the default) takes no room.
    void set_header(std::string header);

    /// The label right of the stars; empty by default.
    [[nodiscard]] const std::string& caption() const
    {
        return caption_;
    }
    /// A decorative label shown to the right of the stars. No interaction reads it. Empty (the
    /// default) draws no caption and reserves no width for one.
    void set_caption(std::string caption);


    /// Whether user input is ignored; false by default. Independent of `enabled()`.
    [[nodiscard]] bool read_only() const
    {
        return read_only_;
    }
    /// Ignores pointer and keyboard input while keeping the enabled look and the tab stop, so
    /// a screen reader still reaches the rating. set_value() still works.
    /// Turning it on drops any hover or drag preview in progress.
    void set_read_only(bool read_only);

    /// Fired whenever value() changes: a click/drag release, a keyboard step, or
    /// set_value() alike, including clearing it (unset).
    void on_value_changed(Callback<void(std::optional<double>)> handler)
    {
        on_value_changed_ = std::move(handler);
    }

    // ---- focus ----
    /// Whether the control has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// A tab stop while enabled, read-only or not.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    /// Whether the pointer is over the control, which shows the hover preview.
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }

    // ---- automation ----
    /// Custom, with a range value, since UI Automation has no rating role. An unset rating
    /// reports 0, because a range value cannot be unset.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Custom;
    }
    /// The header names the control when accessibility().name does not.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.text();
    }

protected:
    // ---- layout ----
    /// A fixed-size row: max_rating() star-sized cells plus (max_rating() - 1) item-spacing
    /// gaps between them, plus caption()'s measured width and a 4 DIP gap when non-empty.
    /// value() and hover never change it. The header, when set, sits above the row.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Takes the header off the top, leaving bounds() as the row of stars.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Hover and drag update the preview; a left-button release commits the star under the
    /// pointer (clamped, so releasing past the left edge clears). Ignored while read-only.
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Draws the outline stars, the filled (possibly partial) stars for the preview, value or
    /// placeholder, the caption and the focus ring.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // Up/Right increase and Down/Left decrease, regardless of orientation.
    // Claimed so Window's directional focus navigation leaves them alone
    // (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    [[nodiscard]] float content_width() const;
    [[nodiscard]] Rect  star_bounds(int index) const;
    // The ceil of the pointer's fraction along the stars, unclamped on purpose, so a position past
    // either edge reaches set_rating_to() to clamp there (dragging off the left edge clears).
    [[nodiscard]] double position_to_star_count(Point p) const;

    // Set or step the rating. Only a pointer pick may clear by re-picking
    // the max rating, so a held "increase" key at the max holds steady.
    void set_rating_to(double new_rating, bool originated_from_mouse);
    void change_rating_by(double delta, bool originated_from_mouse);

    int                   max_rating_ = 5;
    std::optional<double> value_;
    std::optional<double> placeholder_value_;
    bool                  is_clear_enabled_  = true;
    int                   initial_set_value_ = 1;
    bool                  read_only_         = false;
    std::string           caption_;
    ControlHeader         header_{8.0f}; // 8 DIPs between the header and the stars

    bool hovered_      = false; // whole-control hover, gates whether a preview is live
    bool pointer_down_ = false;
    // Unset: no hover or drag preview, so render() shows value() or placeholder_value().
    std::optional<double> preview_count_;

    Callback<void(std::optional<double>)> on_value_changed_;

protected:
    void enabled_changed() override;
    /// Marks the control for redraw (the focus ring).
    void focus_changed(bool focused) override;
    /// Up/Right add a star and Down/Left remove one (from an unset value, the first step lands on
    /// `initial_set_value()`); Home goes to 0 (clearing, or 1 when clearing is disabled) and End to
    /// `max_rating()`. Ignored while read-only.
    bool key_override(const KeyEvent& e) override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.range_value = this;
    }
    [[nodiscard]] double range_value() const override
    {
        return value_.value_or(0.0);
    }
    [[nodiscard]] double range_minimum() const override
    {
        return 0.0;
    }
    [[nodiscard]] double range_maximum() const override
    {
        return static_cast<double>(max_rating_);
    }
    [[nodiscard]] double range_small_change() const override
    {
        return 1.0;
    }
    // Rates as a keyboard step would; false while disabled or read-only.
    bool set_range_value(double value) override
    {
        if (!is_enabled() || read_only_)
        {
            return false;
        }
        set_rating_to(value, /*originated_from_mouse=*/false);
        return true;
    }
    // Reports read_only() alone, whatever enabled() is.
    [[nodiscard]] bool range_read_only() const override
    {
        return read_only_;
    }
};

/// A star rating. Builds a ui::RatingControl.
///
/// Example:
/// @code
/// ui::Property<std::optional<double>> stars{3.0};
///
/// window.set_content(ui::Rating{
///     .value                = stars,
///     .caption              = "312 ratings",
///     .horizontal_alignment = ui::Alignment::Leading,
/// });
/// @endcode
struct Rating
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Rating>` points at.
    using control_type = RatingControl;

    // ---- properties ----
    /// The label above the stars, which also names the control for screen readers. Empty draws
    /// none.
    Binding<std::string> header{};
    /// The rating; unset until rated.
    TwoWayField<std::optional<double>> value{};
    /// The number of stars.
    Binding<std::size_t> max_rating{5};
    /// A grayed hint shown while `value` is unset; never raises a change.
    Binding<std::optional<double>> placeholder_value{};
    /// Clicking the star that is the current rating clears it.
    Binding<bool> clear_enabled{true};
    /// Where a keyboard step lands from an unset value (at least 1).
    Binding<int> initial_set_value{1};
    /// The decorative label right of the stars ("312 ratings"). Empty draws none.
    Binding<std::string> caption{};
    /// Ignores input but keeps the enabled look and the tab stop
    /// (ui::RatingControl::set_read_only).
    Binding<bool> read_only{false};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The new rating (unset when cleared), from input. A bound value doesn't run it.
    Callback<void(std::optional<double>)> on_value_changed{};
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Rating> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::RatingControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
