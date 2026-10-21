// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Inlines.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// What a TextControl shows when its text does not fit the arranged box.
enum class TextTrimming
{
    None,              ///< All of the text, clipped at the box (the default).
    CharacterEllipsis, ///< As much as fits, cut between characters, then an ellipsis.
    WordEllipsis,      ///< As much as fits, cut between words, then an ellipsis.
    Clip,              ///< All of the text, clipped at the box.
};

/// A non-interactive run of text, the label of every form.
///
/// A plain string draws in the style the theme's type ramp gives its role() (Body by default),
/// in the theme's window text color. set_text_style(), set_color() and set_foreground() override
/// them. The text can wrap (set_wrapping()), trim with an ellipsis (set_trimming()), align and
/// justify (set_text_alignment()), and carry formatted runs and links (set_inlines()). With
/// set_selection_enabled(), the user can select and copy it. Otherwise it takes no input or focus.
/// In a spec tree it is built by `ui::Text`.
///
/// Example:
/// @code
/// ui::Property<int> clicks{0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Text{.text = "Settings", .role = ui::TextRole::Title},
///         ui::Text{.text     = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); }),
///                  .wrapping = ui::TextWrapping::WrapWholeWords},
///         ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
///     },
/// });
///
/// // Or imperatively, with formatted runs and a link:
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto block = std::make_unique<ui::TextControl>();
/// block->set_inlines({
///     {.text = "Read the "},
///     {.text = "release notes", .on_click = [] { /* open them */ }},
///     ui::line_break(),
///     {.text = "before updating.", .weight = ui::FontWeight::Bold},
/// });
/// block->set_wrapping(ui::TextWrapping::WrapWholeWords);
/// block->set_selection_enabled(true);
/// ui::TextControl* observer = stack->add(std::move(block)); // the stack owns it now
/// @endcode
class TextControl final : public Element
{
public:
    /// An empty label.
    TextControl() = default;
    /// A label showing `text` (UTF-8) as one plain run.
    explicit TextControl(std::string text) : text_(std::move(text))
    {
    }

    /// The content as UTF-8; with inlines, their texts joined (line breaks as `\n`).
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Replaces the content with one plain run, and drops any inlines and the selection.
    void set_text(std::string text)
    {
        text_ = std::move(text);
        inlines_.clear();
        interaction_.select(0, 0);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The formatted runs; empty for plain text (the default, and after set_text()).
    [[nodiscard]] const std::vector<Inline>& inlines() const
    {
        return inlines_;
    }
    /// Replaces the content with formatted runs. text() becomes the runs' text joined. Clears the
    /// selection. An inline with `on_click` or `navigate_uri` is a link.
    void set_inlines(std::vector<Inline> inlines);

    /// How each line sits between the left and right edges; Left by default.
    [[nodiscard]] TextAlignment text_alignment() const
    {
        return alignment_;
    }
    /// Sets the line alignment.
    void set_text_alignment(TextAlignment alignment)
    {
        alignment_ = alignment;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Extra space between characters, in thousandths of an em; 0 by default.
    [[nodiscard]] int character_spacing() const
    {
        return character_spacing_;
    }
    /// Sets the extra space between characters, in thousandths of an em
    /// (TextStyle::character_spacing).
    void set_character_spacing(int spacing)
    {
        character_spacing_ = spacing;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the user can select and copy the text; false by default. While true the block is
    /// focusable.
    [[nodiscard]] bool selection_enabled() const
    {
        return selection_enabled_;
    }
    /// Turns text selection on or off. While on, the user selects by dragging, double-clicking a
    /// word or triple-clicking a paragraph. Ctrl+A, Ctrl+C and a context menu with Copy and Select
    /// All act on the selection, and a click focuses the block. Turning it off clears the selection.
    void set_selection_enabled(bool enabled);

    /// The fixed color behind selected text; nullopt (the default) when unset or a theme resource
    /// is set. Unset is the accent.
    [[nodiscard]] std::optional<Color> selection_highlight_color() const
    {
        return selection_color_.color();
    }
    /// The color behind selected text as a theme resource, or nullptr.
    [[nodiscard]] ThemeColor selection_highlight_resource() const
    {
        return selection_color_.resource();
    }
    /// Sets the fixed color behind selected text and clears a theme resource; nullopt goes back
    /// to the accent.
    void set_selection_highlight_color(std::optional<Color> color)
    {
        selection_color_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Sets the color behind selected text to a theme resource, re-read under every theme, and
    /// clears a fixed one.
    void set_selection_highlight_color(ThemeColor resource)
    {
        selection_color_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The selection as [start, end) byte offsets into text() (inlines joined); {0, 0} when none.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection() const
    {
        return interaction_.range();
    }
    /// Selects the byte range [start, end) of text(). The selection shows only while the block
    /// has focus and selection is enabled.
    void select(std::size_t start, std::size_t end)
    {
        interaction_.select(start, end);
        mark_dirty(DirtyFlags::Visual);
    }
    /// Selects the whole text, as Ctrl+A does.
    void select_all();
    /// The selected part of the laid-out text; empty when nothing is selected or before the
    /// first measure.
    [[nodiscard]] std::string selected_text() const
    {
        return interaction_.selected_text(layout_);
    }

    /// The explicit style set by set_text_style(). Meaningful only once set_text_style() was
    /// called. Until then the theme's type ramp entry for role() is used.
    [[nodiscard]] const TextStyle& text_style() const
    {
        return style_;
    }
    /// Fixes the font, size, weight and alignment. From then on it takes precedence over role()
    /// and the theme's type ramp.
    void set_text_style(TextStyle style)
    {
        style_          = std::move(style);
        style_explicit_ = true; // an explicit style always wins over the theme's role ramp
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The role this label plays in the theme's `type` ramp; Body by default. The style is looked
    /// up in the current theme, so a plain TextControl follows the theme's own fonts.
    /// set_text_style() always takes precedence.
    [[nodiscard]] TextRole role() const
    {
        return role_;
    }
    /// Sets the type ramp role.
    void set_role(TextRole role)
    {
        role_ = role;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the text breaks into lines; NoWrap by default.
    [[nodiscard]] TextWrapping wrapping() const
    {
        return wrapping_;
    }
    /// Sets the wrapping mode. Anything but NoWrap wins over the wrapping of a style from
    /// set_text_style(). NoWrap, the default, leaves that style's own `wrapping` in charge. Text
    /// wraps only at a bounded width, and a horizontal StackControl offers an unbounded one.
    void set_wrapping(TextWrapping wrapping)
    {
        wrapping_ = wrapping;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The fixed text color, if any. Unset (the default) uses the theme's window text color, so
    /// a label without set_color() stays readable under a dark theme.
    [[nodiscard]] std::optional<Color> color() const
    {
        return color_;
    }
    /// Fixes the text color and clears any set_foreground() resource. A fixed color does not
    /// follow a theme change.
    void set_color(Color color)
    {
        color_          = color;
        color_resource_ = nullptr;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Sets the text color to a theme resource, re-read whenever the text is drawn so it follows a
    /// theme change. Wins over set_color(); nullptr clears it.
    void set_foreground(ThemeColor resource)
    {
        color_resource_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }

    /// What shows when the text does not fit the arranged box: all of it clipped (None, Clip), or
    /// as much as fits followed by an ellipsis, cut between characters or between words. Only the
    /// plain-text form trims (not inlines, selectable or justified text).
    [[nodiscard]] TextTrimming trimming() const
    {
        return trimming_;
    }
    /// Sets the trimming mode.
    void set_trimming(TextTrimming trimming)
    {
        trimming_ = trimming;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Whether the last arrange cut the text short.
    [[nodiscard]] bool is_text_trimmed() const
    {
        return trimmed_;
    }
    /// Fires during arrange with the new is_text_trimmed() value whenever it changes. A typical
    /// use is a tooltip that shows the full text.
    void on_text_trimmed_changed(Callback<void(bool)> handler)
    {
        on_trimmed_changed_ = std::move(handler);
    }


    /// True: measuring reads only the text's own state, the measurer and the theme.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- selection and links ----
    /// Focusable only while enabled with text selection on.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && selection_enabled_;
    }
    /// Whether the block has keyboard focus (only ever true while focusable()).
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Offers Copy / Select All while selection is enabled; returns false (no menu) otherwise.
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    // ---- automation ----
    /// A hand over a link, an I-beam over selectable text.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override;
    /// Always AutomationRole::Text.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Text;
    }
    /// The text, when no accessibility name is set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text_;
    }
    /// One node per link inline. Invoking a node follows its link.
    void automation_children(std::vector<Element*>& out) const override;

protected:
    /// The text's size in DIPs in the resolved style, wrapped to `available.width` when wrapping.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Re-flows laid-out content to the arranged width and applies trimming (which may fire
    /// on_text_trimmed_changed()).
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// The text, or the laid-out runs with the selection highlight (while focused) and link
    /// underlines.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Selection drags and multi-clicks, and link clicks; ignored while disabled.
    bool pointer_override(const PointerEvent& e) override;


private:
    // `style_` verbatim once set_text_style() was called, otherwise the theme's `type` ramp entry for
    // `role_` with TextControl's own alignment on top; alignment is layout, not typography.
    [[nodiscard]] TextStyle resolved_style(const Theme& theme) const;
    [[nodiscard]] bool      uses_layout() const
    {
        return !inlines_.empty() || selection_enabled_ || alignment_ == TextAlignment::Justify;
    }
    void relayout(float width) const;

    std::string text_;
    // Left-aligned and vertically centered. Used verbatim only after set_text_style().
    TextStyle            style_{14.0f, TextAlign::Leading, TextAlign::Center, "Segoe UI"};
    bool                 style_explicit_ = false;
    TextRole             role_           = TextRole::Body;
    TextWrapping         wrapping_       = TextWrapping::NoWrap;
    std::optional<Color> color_{};
    ThemeColor           color_resource_{};

    // TextTrimming: the text as drawn once cut to the arranged box, and the style it was measured in.
    TextTrimming         trimming_ = TextTrimming::None;
    mutable bool         trimmed_  = false;
    mutable std::string  trimmed_text_;
    mutable TextStyle    measured_style_{};
    Callback<void(bool)> on_trimmed_changed_;
    void                 trim_to_bounds();

    std::vector<Inline> inlines_;
    TextAlignment       alignment_         = TextAlignment::Left;
    int                 character_spacing_ = 0;
    bool                selection_enabled_ = false;
    Paint               selection_color_{};
    TextInteraction     interaction_;

    // The laid-out content (uses_layout() only), rebuilt by measure and re-flowed to the arranged
    // width; the measurer and options are kept for that re-flow and for hit testing.
    mutable TextLayout                 layout_;
    mutable TextLayoutOptions          layout_options_{};
    mutable const TextMeasurer*        measurer_ = nullptr;
    mutable std::vector<const Inline*> links_;
    // One automation node per place in links_, made on first use and kept for the control's life.
    mutable std::vector<std::unique_ptr<Element>> link_automation_;
    void                                          place_link_automation() const;

protected:
    void enabled_changed() override
    {
        mark_dirty(DirtyFlags::Visual);
    }
    /// Tracks focus. The selection shows only while the block has focus.
    void focus_changed(bool /*focused*/) override
    {
        mark_dirty(DirtyFlags::Visual);
    }
    /// Ctrl+A (select all) and Ctrl+C (copy), only while enabled with selection on.
    bool key_override(const KeyEvent& e) override;

protected:
    /// None: selectable text shows focus through its selection.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }
};

/// A run of non-interactive text. Builds a ui::TextControl.
///
/// The text is a constant string, or a `bind()` so the label follows a Property. The font comes
/// from the theme's type ramp through `.role`, such as `TextRole::Caption` or
/// `TextRole::Subtitle`. `.text_style` sets a style for this label alone.
///
/// Example:
/// @code
/// ui::Property<int>    clicks{0};
/// ui::Control<ui::Text> status;
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Text{ .text = "Appearance", .role = ui::TextRole::Subtitle },
///         ui::Text{ .text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); }) },
///         ui::Text{ .text     = "A longer paragraph that wraps at the width the parent offers.",
///                   .wrapping = ui::TextWrapping::Wrap,
///                   .control  = status },
///     },
/// });
/// @endcode
struct Text
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Text>` points at.
    using control_type = TextControl;

    // ---- properties ----
    /// The displayed string (UTF-8). Ignored when `.inlines` is set.
    Binding<std::string> text{};

    /// Runs with their own formatting, line breaks and links (ui/text/Inlines.hpp). When set,
    /// they are the content and `.text` is ignored.
    Binding<std::vector<Inline>> inlines{};
    /// The user can select the text with the pointer or keyboard and copy it.
    Binding<bool> selection_enabled{false};
    /// The step of the theme's type ramp the text uses, such as Caption or Subtitle. It follows a
    /// theme change.
    Binding<TextRole> role{TextRole::Body};

    /// A style for this label alone. When set, the theme's type ramp no longer applies to this
    /// label.
    Binding<std::optional<TextStyle>> text_style{};


    /// Whether the text breaks into lines at the width the parent offers.
    Binding<TextWrapping> wrapping{TextWrapping::NoWrap};

    /// How each line sits between the left and right edges.
    Binding<TextAlignment> text_alignment{TextAlignment::Left};
    /// Extra space between characters, in thousandths of an em.
    Binding<int> character_spacing{0};
    /// The color behind selected text: a fixed color or a theme resource, which follows theme
    /// switches. Unset is the accent.
    Binding<Paint> selection_highlight_color{};
    /// The text color: a fixed color, or a theme resource re-read on a theme change. Unset
    /// uses the theme's text color.
    Binding<Paint> foreground{};
    /// What shows when the text does not fit: all of it clipped, or an ellipsis (plain text only).
    Binding<TextTrimming> trimming{TextTrimming::None};

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
    /// Fires with the new state whenever layout starts or stops cutting the text off. Only with
    /// `.trimming` set.
    Callback<void(bool)> on_text_trimmed_changed{};
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
    ControlOut<Text> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::TextControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
