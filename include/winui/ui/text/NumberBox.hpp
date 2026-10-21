// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/NumberFormat.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>

namespace ui
{

/// Where a ui::NumberBox shows its spin buttons.
enum class NumberBoxSpinButtonPlacementMode
{
    /// No spin buttons (the default); the keyboard and the wheel still step.
    Hidden,
    /// A small indicator in the box; while it has focus, an up/down button pair floats beside it.
    /// Themes without the floating pair show the Inline buttons instead.
    Compact,
    /// Up and down buttons side by side at the box's right edge; the box is at least 120 DIPs
    /// wide.
    Inline,
};

/// What a ui::NumberBox does with input that is not a number.
enum class NumberBoxValidationMode
{
    /// The default: text that does not parse is replaced by the value's text, and committed
    /// values are clamped to [min, max].
    InvalidInputOverwritten,
    /// No validation: out-of-range values stand and unparsable text is left as typed.
    Disabled,
};

class NumberBoxSpinPopup; // defined in NumberBox.cpp

/// A text box for a number, validated and formatted when committed. It can have spin buttons,
/// arithmetic input, stepping by keyboard and wheel, and a header.
///
/// value() is the number that counts. The user can type anything, and the text is committed on
/// Enter, on losing focus and before each step. Text that parses is clamped to [min, max] and
/// formatted again. Text that doesn't is replaced by the last value's text. An unset value (the
/// default) means no number, and the box is empty and shows the placeholder. The box is one focus
/// stop and owns its inner TextBox. In a spec tree it is built by `ui::NumberBox`.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// double quantity = 5.0;
///
/// auto box = std::make_unique<ui::NumberBoxControl>(/*value=*/5.0, /*min=*/0.0, /*max=*/100.0);
/// box->set_header("Quantity");
/// box->set_spin_button_placement(ui::NumberBoxSpinButtonPlacementMode::Inline);
/// box->set_step(1.0);
/// box->set_large_step(10.0);
/// box->set_accepts_expression(true); // "2 * 3" commits 6
/// box->set_number_formatter(ui::DecimalFormatter{.fraction_digits = 2});
/// box->on_value_changed([&](std::optional<double> v) { quantity = v.value_or(0.0); });
/// ui::NumberBoxControl* quantity_box = stack->add(std::move(box));
///
/// // Or declaratively:
/// ui::Property<std::optional<double>> price{9.5};
/// stack->add(ui::NumberBox{
///     .header                = "Price",
///     .placeholder_text      = "0.00",
///     .value                 = price,
///     .min                   = 0.0,
///     .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Compact,
/// }.build());
/// @endcode
class NumberBoxControl final : public Element, private RangeValueProvider
{
public:
    /// `value` unset is an empty box. The range is unbounded by default; `max` below `min` is
    /// raised to `min` (see set_range()).
    explicit NumberBoxControl(std::optional<double> value = std::nullopt,
                              double                min   = -std::numeric_limits<double>::infinity(),
                              double                max   = std::numeric_limits<double>::infinity());
    ~NumberBoxControl() override;

    NumberBoxControl(const NumberBoxControl&)            = delete;
    NumberBoxControl& operator=(const NumberBoxControl&) = delete;

    // ---- value ----
    /// Minimum; committed and stepped values are clamped to it. -infinity by default.
    [[nodiscard]] double(min)() const
    {
        return min_;
    }
    /// Maximum; committed and stepped values are clamped to it. +infinity by default.
    [[nodiscard]] double(max)() const
    {
        return max_;
    }
    /// Sets the range and clamps the current value into it, as SliderControl::set_range() does.
    /// Fires on_value_changed() when that changes the value. A `max` below `min` becomes `min`.
    void set_range(double min, double max);

    /// The committed number, unset for none. Not what is being typed until it commits.
    [[nodiscard]] std::optional<double> value() const
    {
        return value_;
    }
    /// Sets the value, clamped to [min, max] under InvalidInputOverwritten, and formats the text
    /// to match. Fires on_value_changed() only when the value changed. Unset or NaN means no
    /// number, and the box is empty.
    void set_value(std::optional<double> value);

    /// What the arrow keys and spin buttons step by; 1 by default.
    [[nodiscard]] double step() const
    {
        return step_;
    }
    /// Sets how far a spin button click or an Up or Down key press moves the value. Zero turns
    /// both off. Negative values count as 0.
    void set_step(double step)
    {
        step_ = (std::max)(0.0, step);
    }

    /// What PageUp/PageDown step by. 10 by default.
    [[nodiscard]] double large_step() const
    {
        return large_step_;
    }
    /// Sets what PageUp and PageDown step by. Negative values count as 0.
    void set_large_step(double step)
    {
        large_step_ = (std::max)(0.0, step);
    }

    /// Where the spin buttons show; Hidden by default.
    [[nodiscard]] NumberBoxSpinButtonPlacementMode spin_button_placement() const
    {
        return placement_;
    }
    /// Sets where the spin buttons show: Hidden, Compact or Inline.
    void set_spin_button_placement(NumberBoxSpinButtonPlacementMode placement);

    /// Whether committed text is evaluated as arithmetic ("1 + 2^2" is 5). False by default.
    [[nodiscard]] bool accepts_expression() const
    {
        return accepts_expression_;
    }
    /// Committed text is evaluated as arithmetic.
    void set_accepts_expression(bool accepts)
    {
        accepts_expression_ = accepts;
    }

    /// The formatter the value is shown and typed text parsed with. The default shows up to ten
    /// significant digits with the user's regional separators.
    [[nodiscard]] const DecimalFormatter& number_formatter() const
    {
        return formatter_;
    }
    /// Sets the formatter the value is shown and the text parsed with. Formats the text again at
    /// once.
    void set_number_formatter(DecimalFormatter formatter);

    /// What happens to input that does not parse; InvalidInputOverwritten by default.
    [[nodiscard]] NumberBoxValidationMode validation_mode() const
    {
        return validation_mode_;
    }
    /// Sets what happens to input that does not parse. Takes effect from the next commit or step,
    /// and the current value is not validated again.
    void set_validation_mode(NumberBoxValidationMode mode)
    {
        validation_mode_ = mode;
    }

    /// Whether stepping wraps around the range. False by default.
    [[nodiscard]] bool wrap_enabled() const
    {
        return wrap_enabled_;
    }
    /// When true, a step past max lands on min, and a step past min lands on max.
    void set_wrap_enabled(bool wrap)
    {
        wrap_enabled_ = wrap;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The inner TextBox's placeholder; empty by default.
    [[nodiscard]] const std::string& placeholder_text() const
    {
        return text_box_->placeholder_text();
    }
    /// The hint shown while the box is empty (the value unset).
    void set_placeholder_text(std::string text)
    {
        text_box_->set_placeholder_text(std::move(text));
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- text ----
    /// The text as typed, not validated. It can differ from `value` until it is committed, on
    /// Enter, on losing focus, or before a spin button or arrow key step. set_text() does not
    /// fire on_text_changed().
    [[nodiscard]] const std::string& text() const;
    void                             set_text(std::string text);

    /// The header text; empty by default.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// The label above the control. Empty (the default) draws none and takes no room.
    void set_header(std::string header)
    {
        if (header_.set_text(std::move(header)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// The secondary text under the box; empty (the default) draws none.
    [[nodiscard]] const std::string& description() const
    {
        return description_.text();
    }
    /// Sets the description. Empty draws none and takes no room.
    void set_description(std::string description)
    {
        if (description_.set_text(std::move(description)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// What is wrong with the value, shown in the description's place in the critical color,
    /// with the border in the same color; empty (the default) shows the description.
    [[nodiscard]] const std::string& error() const
    {
        return description_.error();
    }
    /// Sets the error. Empty shows the description again.
    void set_error(std::string error)
    {
        if (description_.set_error(std::move(error)))
        {
            text_box_->set_invalid_border(description_.has_error());
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// The description, or the error while one is set, is the help text a screen reader offers
    /// when accessibility().description is empty.
    [[nodiscard]] std::string automation_help_text_fallback() const override
    {
        return description_.shown();
    }
    /// Invalid while an error is set.
    [[nodiscard]] bool automation_invalid() const override
    {
        return description_.has_error();
    }
    /// The header names the control when accessibility().name does not.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.text();
    }


    /// Fired after every user edit of the text, such as typing, Backspace or Delete, with the
    /// text. on_value_changed() fires only once a parsed, clamped value changes.
    void on_text_changed(Callback<void(const std::string&)> handler);

    /// Fired whenever `value` changes, from a committed text edit, a spin button click or an
    /// arrow key step.
    void on_value_changed(Callback<void(std::optional<double>)> handler)
    {
        on_value_changed_ = std::move(handler);
    }

    // ---- focus ----
    /// Whether the box holds keyboard focus. The inner TextBox shows the caret then.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The inner text box's caret.
    [[nodiscard]] std::optional<Rect> caret_bounds() const override
    {
        return text_box_->caret_bounds();
    }
    /// A tab stop while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- derived visual state (spin buttons only, the TextBox draws its own) ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// The inline up button's state: Disabled at the top of the range (unless wrapping), with
    /// no value, or while the box is disabled; else Pressed/Hover/Normal from the pointer.
    [[nodiscard]] ButtonState up_visual_state() const;
    /// The inline down button's state, as up_visual_state() at the bottom of the range.
    [[nodiscard]] ButtonState down_visual_state() const;

    // ---- layout ----
    /// Desired width is the internal TextBox's own desired width plus the spinner column;
    /// desired height is the TextBox's own single-line height.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the header, the inner TextBox and the spin-button columns (or Compact's indicator).
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Spin-button presses step (and repeat while held); everything else goes to the TextBox.
    bool               pointer_override(const PointerEvent& e) override;
    [[nodiscard]] bool covers_children_at(Point p) const override;
    /// The wheel steps the value while the box has focus. It then doesn't also scroll whatever
    /// holds the box.
    bool wheel_override(const PointerEvent& e) override;
    // ---- render ----
    /// Draws the header, the inner TextBox, and the inline spin buttons or Compact's indicator.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Ticks the inner TextBox, then advances the spin-button press-and-hold repeat countdown
    /// while a button is held.
    void tick_override(Duration delta) override;


public:
    // ---- the internal TextBox ----
    /// Visits the internal TextBox.
    void for_each_child(ChildVisitor visit) const override
    {
        visit(*text_box_);
    }
    /// One tab stop: the internal TextBox is part of this control, not a stop of its own.
    void collect_focusables(std::vector<Element*>& out) override
    {
        if (focusable())
        {
            out.push_back(this);
        }
    }
    /// One automation node: the internal TextBox is not listed as a child.
    void automation_children(std::vector<Element*>&) const override
    {
    }
    /// Shows Cut, Copy, Paste and Select All, with no spelling suggestions.
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    // ---- automation ----
    /// The text field's cursor over the text field: an I-beam, or an arrow over its buttons.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override
    {
        return text_box_->bounds().contains(p) ? text_box_->cursor_at(p) : std::nullopt;
    }
    /// AutomationRole::SpinButton.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::SpinButton;
    }

private:
    // The inner TextBox's editing and clipboard keys (Element::key_override()).
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const;
    // Up/Down step the value and Left/Right reach the inner TextBox caret.
    // Claimed so Window's directional focus navigation leaves them alone
    // (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    // Enter commits the typed value (Element::key_override()).
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // Compact's spin-button popup: open in Compact placement while the box is enabled and has
    // focus. A click outside doesn't close it; it closes when focus leaves the box.
    void sync_popup();
    // This control alone: the internal TextBox is not live on its own.
    void collect_live(std::vector<Element*>& out) override
    {
        out.push_back(this);
    }
    friend class NumberBoxSpinPopup; // steps and presses through the same state as the inline buttons

    // Compact falls back to the inline buttons under a theme without the spin-button popup.
    [[nodiscard]] bool inline_spinners() const
    {
        return placement_ == NumberBoxSpinButtonPlacementMode::Inline ||
               (placement_ == NumberBoxSpinButtonPlacementMode::Compact && !spin_popup_);
    }
    [[nodiscard]] bool compact_popup() const
    {
        return placement_ == NumberBoxSpinButtonPlacementMode::Compact && spin_popup_;
    }
    // Inline under a theme that stacks the spin buttons inside the field (Aero's up-down).
    [[nodiscard]] bool stacked_spinners() const
    {
        return inline_spinners() && stacked_width_ > 0.0f;
    }
    [[nodiscard]] float spinner_width() const;   // 0 unless Inline side by side
    [[nodiscard]] float indicator_width() const; // Compact's indicator, else 0
    [[nodiscard]] Rect  spinner_bounds() const;  // both spin-button columns together
    [[nodiscard]] Rect  up_bounds() const;
    [[nodiscard]] Rect  down_bounds() const;
    // Nothing to step from no value; either way while wrapping or not validating.
    [[nodiscard]] bool can_step_up() const
    {
        return is_enabled() && value_ &&
               (wrap_enabled_ || validation_mode_ == NumberBoxValidationMode::Disabled || *value_ < max_);
    }
    [[nodiscard]] bool can_step_down() const
    {
        return is_enabled() && value_ &&
               (wrap_enabled_ || validation_mode_ == NumberBoxValidationMode::Disabled || *value_ > min_);
    }

    [[nodiscard]] ButtonState popup_state(bool up) const;
    void                      popup_pointer(const PointerEvent& e, const Rect& up, const Rect& down);

    void sync_text_from_value(); // text_box_->set_text(format of value_), unconditional
    void commit_text();          // parses text_box_->text(): valid -> set_value(); invalid -> sync_text_from_value()
    void step_value(double delta);

    std::unique_ptr<TextBoxControl>     text_box_;
    double                              min_ = -std::numeric_limits<double>::infinity();
    double                              max_ = std::numeric_limits<double>::infinity();
    std::optional<double>               value_;
    double                              step_           = 1.0;
    double                              large_step_     = 10.0;
    NumberBoxSpinButtonPlacementMode    placement_      = NumberBoxSpinButtonPlacementMode::Hidden;
    mutable bool                        spin_popup_     = true; // the theme's spin_button_popup, from measure
    mutable float                       stacked_width_  = 0.0f; // measure(): the theme's stacked spinner width
    mutable float                       stacked_border_ = 0.0f; // and the field border they sit inside
    std::unique_ptr<NumberBoxSpinPopup> popup_;                 // Compact's, built on first open
    ControlHeader                       header_{8.0f};          // 8 DIPs between the header and the field
    ControlDescription                  description_;
    bool                                accepts_expression_ = false;
    bool                                wrap_enabled_       = false;
    NumberBoxValidationMode             validation_mode_    = NumberBoxValidationMode::InvalidInputOverwritten;
    DecimalFormatter                    formatter_{};

    bool hovered_up_ = false, pressed_up_ = false;
    bool hovered_down_ = false, pressed_down_ = false;
    bool popup_hovered_up_ = false, popup_hovered_down_ = false; // Compact's popup buttons

    // Press-and-hold repeat countdown, advanced by tick_override() while a button is pressed.
    double repeat_elapsed_ms_    = 0.0;
    bool   repeat_first_pending_ = true; // first repeat after the initial (longer) delay

    // Cached by measure_override() for arrange_override() and render_override().
    mutable float header_height_ = 0.0f;

    Callback<void(std::optional<double>)> on_value_changed_;

protected:
    void enabled_changed() override;
    /// Focus reaches the inner TextBox; losing it commits the typed text.
    void focus_changed(bool focused) override;
    /// Up/Down step by step(), PageUp/PageDown by large_step(), Enter commits; the other keys
    /// edit the text.
    bool key_override(const KeyEvent& e) override;
    /// Typed characters go to the inner TextBox; they are parsed only when committed.
    bool text_override(const TextInputEvent& text_event) override;

protected:
    /// None: the field shows focus with its accent border.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.range_value = this;
    }
    // value() (NaN with no number).
    [[nodiscard]] double range_value() const override
    {
        return value_.value_or(std::numeric_limits<double>::quiet_NaN());
    }
    // min().
    [[nodiscard]] double range_minimum() const override
    {
        return min_;
    }
    // max().
    [[nodiscard]] double range_maximum() const override
    {
        return max_;
    }
    // step().
    [[nodiscard]] double range_small_change() const override
    {
        return step_;
    }
    // set_value() from an accessibility client; refused while the box is disabled.
    bool set_range_value(double value) override
    {
        if (!is_enabled())
        {
            return false;
        }
        set_value(value);
        return true;
    }
    [[nodiscard]] bool range_read_only() const override
    {
        return false;
    }
};

/// A numeric field, with spin buttons when asked for. Builds a ui::NumberBoxControl.
///
/// Typed text is committed on Enter or when focus leaves. It is parsed, or evaluated with
/// `.accepts_expression`, clamped to `[min, max]` and reported through `.on_value_changed`.
/// A bound value outside `[min, max]` is clamped too, and a two-way property gets the clamped
/// value written back. The arrow keys, the wheel and the spin buttons step by `.step`.
///
/// Example:
/// @code
/// ui::Property<std::optional<double>> quantity{1.0};
///
/// window.set_content(ui::NumberBox{
///     .header                = "Quantity",
///     .value                 = quantity,
///     .min                   = 0.0,
///     .max                   = 99.0,
///     .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
///     .accepts_expression    = true,
/// });
/// @endcode
struct NumberBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<NumberBox>` points at.
    using control_type = NumberBoxControl;

    // ---- properties ----
    /// The label above the control. Empty draws none.
    Binding<std::string> header{};
    /// Secondary text under the box, such as what to enter or why; empty draws none. Screen
    /// readers offer it as help text.
    Binding<std::string> description{};
    /// What is wrong with the value: non-empty shows it in the description's place in the critical
    /// color and draws the border in that color, and screen readers report the field invalid.
    /// The field shows exactly what it is given, so bind it to a check of the value and decide
    /// there when it shows (after the first submit, say).
    Binding<std::string> error{};
    /// The hint shown while the box is empty.
    Binding<std::string> placeholder_text{};
    /// Unset (the default) is no number: an empty box showing the placeholder.
    TwoWayField<std::optional<double>> value{};
    /// Minimum; committed values below it are clamped. Unbounded by default. A bound range that
    /// moves past the value clamps it, and a two-way `value` follows.
    Binding<double> min{-std::numeric_limits<double>::infinity()};
    /// Maximum; committed values above it are clamped. Unbounded by default. Below `min`, it is
    /// taken as `min`.
    Binding<double> max{std::numeric_limits<double>::infinity()};
    /// What the arrow keys and spin buttons step by; 0 disables stepping.
    Binding<double> step{1.0};
    /// What PageUp and PageDown step by.
    Binding<double> large_step{10.0};
    /// Where the spin buttons show; Hidden by default.
    Binding<NumberBoxSpinButtonPlacementMode> spin_button_placement{NumberBoxSpinButtonPlacementMode::Hidden};
    /// Typed arithmetic is evaluated.
    Binding<bool> accepts_expression{false};
    /// Formats the value and parses the text. Unset keeps the default: ten significant digits,
    /// no padding.
    Binding<std::optional<DecimalFormatter>> number_formatter{};
    /// What happens to input that does not parse. By default it is overwritten with the last
    /// valid value.
    Binding<NumberBoxValidationMode> validation_mode{NumberBoxValidationMode::InvalidInputOverwritten};
    /// When true, a step past `max` lands on `min`, and a step past `min` lands on `max`.
    Binding<bool> wrap_enabled{false};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Fires whenever the value changes, from a committed edit, a spin button or an arrow key step.
    /// A bound value doesn't run it.
    Callback<void(std::optional<double>)> on_value_changed{};
    /// Fires on every user edit with the raw, not yet parsed text.
    Callback<void(const std::string&)> on_text_changed{};
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
    ControlOut<NumberBox> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::NumberBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
