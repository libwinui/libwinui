// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace ui
{

class FlyoutBase; // ui/dialogs/Flyout.hpp; a Button only ever points at the flyout that owns it

/// Which of Theme's per-control token sets a Button draws with.
///
/// Example:
/// @code
/// auto save = std::make_unique<ui::ButtonControl>("Save");
/// save->set_variant(ui::ButtonVariant::Accent); // the one high-emphasis action in the view
/// @endcode
enum class ButtonVariant
{
    /// The default look: a subtle translucent fill with an elevation border.
    Neutral,
    /// The filled, high-emphasis look, in the accent color. Use at most one per view.
    Accent,
    /// Borderless, showing only a wash on hover/press: toolbar, command-bar and menu buttons.
    Subtle,
    /// A hyperlink button: Subtle's backgrounds under link-colored text, a Hand cursor,
    /// reported to UI Automation as a hyperlink, and opening ButtonControl::navigate_uri() when clicked.
    Hyperlink,
};

/// A push button, clicked by mouse (press and release inside), Enter/Space while focused,
/// or UI Automation's Invoke.
///
/// Shows either a plain text() label, whose color tracks hover/press/disabled automatically,
/// or arbitrary set_content() (an icon plus text, say). A click runs, in order: the attached
/// flyout() toggles open or closed, the protected on_activated() hook, the on_click() handler,
/// the command (set_command()), then navigate_uri() opens if set. The look comes from variant()
/// and the theme, with per-instance background / foreground overrides. Not final: ToggleButton
/// and DropdownButton derive from it through the protected hooks.
///
/// Example:
/// @code
/// ui::Property<int> clicks{0};
///
/// auto       stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto*      label = stack->add(std::make_unique<ui::TextControl>("Clicks: 0"));
/// ui::ButtonControl* go = stack->add(std::make_unique<ui::ButtonControl>("Click me"));
/// go->set_variant(ui::ButtonVariant::Accent);
/// go->on_click([label, n = 0]() mutable { label->set_text(std::format("Clicks: {}", ++n)); });
/// window.set_content(std::move(stack));
///
/// // Declaratively:
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
///         ui::Button{.text = "Click me", .variant = ui::ButtonVariant::Accent,
///                           .on_click = [&] { ++clicks; }},
///         ui::Button{.text = "Docs", .navigate_uri = "https://example.com/docs",
///                           .variant = ui::ButtonVariant::Hyperlink},
///     },
/// });
/// @endcode
class ButtonControl : public Element, protected InvokeProvider
{

    // ---- composition ----
    /// Fades the hover/press highlight in when the button becomes hovered or pressed and out
    /// when it stops, without redrawing the button every frame. Does nothing while that state
    /// is unchanged, so a layout change doesn't restart the fade.
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;

public:
    /// A button with an empty label.
    ButtonControl() = default;
    /// A button labeled `text` (UTF-8).
    explicit ButtonControl(std::string text);

    // ---- content ----
    /// The plain-text label (UTF-8), drawn centered in the current state's text color, so it
    /// tracks hover/press/disabled automatically. Also the automation name fallback. Ignored
    /// once content() is set.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Sets the label (UTF-8). The button is measured again and redrawn.
    void set_text(std::string text)
    {
        text_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Any Element as content in place of the text label, e.g. a StackControl of an icon and a
    /// TextControl. Takes ownership, destroys any previous content, and returns a non-owning
    /// observer, as StackControl::add() does. When set, content replaces text() for layout and
    /// rendering. The caller picks the content's colors, because the state's foreground color
    /// only applies to text(). `content` must not be null. The button is measured again and redrawn.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "Button content must be an Element");
        T* observer = content.get();
        set_parent_of(*observer, this);
        content_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The content set with set_content() as a non-owning observer, or nullptr for a text button.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    /// The token set the button draws with; ButtonVariant::Neutral by default.
    [[nodiscard]] ButtonVariant variant() const
    {
        return variant_;
    }
    /// Redraws only; every style has the same metrics.
    void set_variant(ButtonVariant variant)
    {
        variant_ = variant;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The padding around the text or content, DIPs; nullopt (the default) keeps the theme's
    /// button padding. The theme padding clips the icon of a small
    /// fixed-size icon button, such as a 40x40 title-bar back button, so pass Thickness{0} there.
    /// The button is measured again and redrawn.
    void set_padding(std::optional<Thickness> padding)
    {
        padding_override_ = padding;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }


    // ---- command ----
    /// Invoked on a completed click (mouse press and release inside, Enter/Space
    /// while focused, or UI Automation's Invoke) while enabled, before the command runs. The
    /// handler may destroy the button. Replaces any previous handler.
    void on_click(Callback<void()> handler)
    {
        on_click_ = std::move(handler);
    }
    /// A click opens `flyout` (and a second one closes it). Set for you
    /// when a Button becomes a Flyout's anchor; the Flyout owns the Button, so the pointer
    /// never outlives what it points at.
    void set_flyout(FlyoutBase* flyout)
    {
        flyout_ = flyout;
    }
    /// The flyout a click opens, or nullptr; owned by that flyout, not the button.
    [[nodiscard]] FlyoutBase* flyout() const
    {
        return flyout_;
    }
    /// Runs `command` with `parameter` on every click, after the click handler. The button is
    /// enabled only while the command is, and follows it as it changes; without a tooltip of its
    /// own it shows the command's description. Either may go first: a destroyed command leaves
    /// the button as if it had none. An empty ref clears it. A plain Button takes no label or icon
    /// from the command.
    void set_command(CommandRef command, std::string parameter = {});
    /// The command set with set_command(), or nullptr (none, or destroyed); not owned.
    [[nodiscard]] const Command* command() const
    {
        return command_.get();
    }
    /// The parameter passed to the command's execute.
    [[nodiscard]] const std::string& command_parameter() const
    {
        return command_parameter_;
    }
    /// Every click opens `uri` in its default handler, after the click handler and the
    /// command. Meant for ButtonVariant::Hyperlink, but works with any variant. Empty (the
    /// default) opens nothing.
    void set_navigate_uri(std::string uri)
    {
        navigate_uri_ = std::move(uri);
    }
    /// The URI a click opens, or empty.
    [[nodiscard]] const std::string& navigate_uri() const
    {
        return navigate_uri_;
    }

    // ---- layout ----
    /// True for a text button. A button with content() answers false, because it measures the
    /// content without listing it in for_each_child() (the two share one tab stop).
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return content_ == nullptr;
    }

    // ---- focus ----
    /// Whether the button holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// A disabled button is skipped in the tab order.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled(); // a disabled button is skipped in the tab order
    }

    // ---- colors ----
    /// The button's fill at rest. Hover, press and the disabled state still use the theme's
    /// fills. nullopt (the default) keeps the style's. Clears a theme-resource background. Redraws.
    void set_background(std::optional<Color> color)
    {
        background_override_ = color;
        background_resource_ = nullptr;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The button's fill at rest as a theme resource, re-read under every theme. Clears a fixed
    /// background. Redraws.
    void set_background(ThemeColor resource)
    {
        background_override_ = std::nullopt;
        background_resource_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The label color in the Normal state; nullopt (the default)
    /// keeps the style's. Clears a theme-resource foreground. Redraws.
    void set_foreground(std::optional<Color> color)
    {
        foreground_override_ = color;
        foreground_resource_ = nullptr;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The label color in the Normal state as a theme resource, re-read under every theme. It
    /// wins over a fixed foreground color. Redraws.
    void set_foreground(ThemeColor resource)
    {
        foreground_resource_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }

private:
    friend class DropdownButtonControl; // answers Enter as its button does

    // Enter clicks an enabled button, so it never reaches a dialog's default button past it.
    [[nodiscard]] bool takes_enter_key() const
    {
        return is_enabled();
    }
    bool handle_pointer_state(const PointerEvent& e); // whether it took the event

    std::optional<Color> background_override_{};
    ThemeColor           background_resource_{};
    std::optional<Color> foreground_override_{};
    ThemeColor           foreground_resource_{};

public:
    /// The command's shortcut, app-wide: clicks the button while it is enabled. Nothing
    /// without a command or with an empty key combination.
    bool handle_global_shortcut(const KeyEvent& e) override;
    /// The tooltip set on the button, else its command's description.
    [[nodiscard]] std::string tool_tip() const override;

    /// `variant`'s colors in `state`: the Neutral/Accent/Subtle/Hyperlink table every Button draws
    /// from, and every button-shaped part of another control (SplitButton's two halves).
    [[nodiscard]] static ButtonVisual style_visual(const Theme& theme, ButtonVariant variant, ButtonState state);

    // ---- derived visual state ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// The state the button currently draws: Disabled, Pressed, Hover or Normal, derived
    /// from the enabled flag and the pointer/key input.
    [[nodiscard]] ButtonState visual_state() const;
    /// Desired size = measured text (or content()'s own measure()) + padding, clamped to
    /// `available`; the padding is cached for arrange_override().
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places content() inside the padding measure_override() last used, centered.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Tracks hover and press; a release inside after a press inside is a click.
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Emits: fill_rounded_rect (background) + draw_text (or the content), plus the theme's
    /// border where it draws one. A focus ring stroke is added only while focused and enabled.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    /// Forwards the tick to content(), so animated content (an AnimatedIcon, a ProgressRing)
    /// keeps moving inside a button.
    void tick_override(Duration delta) override
    {
        if (content_)
        {
            content_->tick(delta);
        }
    }


public:
    /// True while pressed by the pointer (inside) or by a held Enter/Space.
    [[nodiscard]] bool is_pressed() const;

    /// The Hand cursor for ButtonVariant::Hyperlink; no opinion otherwise.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point) const override
    {
        return variant_ == ButtonVariant::Hyperlink ? std::optional<CursorShape>{CursorShape::Hand} : std::nullopt;
    }

    // ---- automation ----
    /// Hyperlink for ButtonVariant::Hyperlink, Button otherwise.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return variant_ == ButtonVariant::Hyperlink ? AutomationRole::Hyperlink : AutomationRole::Button;
    }
    /// text(), used when Accessibility::name is not set. Empty for a content button: name
    /// it through set_accessibility().
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text_; // empty when content() is set
    }

protected:
    /// Runs on every completed click (pointer, Enter/Space, UI Automation's Invoke) before the
    /// click handler. ToggleButton flips its state here.
    virtual void on_activated()
    {
    }
    /// The colors the current state draws with; `variant()`'s set by default.
    [[nodiscard]] virtual ButtonVisual current_visual(const Theme& theme) const;

    /// Performs a click as if the user completed one, when enabled: the flyout toggle,
    /// on_activated(), on_click, the command, then navigate_uri().
    void activate();

    /// Width kept free at the right of the content, inside the padding, for a trailing part the
    /// derived control draws itself: DropdownButton's chevron column. The content (or the label)
    /// is laid out and centered in what is left.
    void set_trailing_reserve(float width)
    {
        trailing_reserve_ = width;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The width set by set_trailing_reserve(), DIPs; 0 by default.
    [[nodiscard]] float trailing_reserve() const
    {
        return trailing_reserve_;
    }
    /// The padding measure_override() last used (padding override or the theme's).
    [[nodiscard]] const Thickness& padding() const
    {
        return content_padding_;
    }

private:
    [[nodiscard]] bool hot() const;                      // hovered or pressed, i.e. wants the highlight
    [[nodiscard]] bool washed(const Theme& theme) const; // shows the hover wash under `theme`

    std::string              text_;
    std::unique_ptr<Element> content_; // when set, replaces text_ for layout/render
    ButtonVariant            variant_ = ButtonVariant::Neutral;
    std::optional<Thickness> padding_override_{};      // see set_padding()
    float                    trailing_reserve_ = 0.0f; // see set_trailing_reserve()

    // Set by measure_override() for arrange_override(), which has no Theme.
    mutable Thickness content_padding_{};

    // input flags the visual state is derived from
    bool hovered_      = false;
    bool pointer_down_ = false; // left button pressed while inside
    bool key_down_     = false; // Enter/Space held while focused

    // last "hotness" reported to the compositor, so sync_composition() only animates on
    // an actual transition.
    bool composed_hot_ = false;

    Callback<void()> on_click_;
    FlyoutBase*      flyout_ = nullptr; // set by FlyoutBase::set_anchor(); the flyout owns this Button
    CommandRef       command_;          // set_command(); not owned
    std::string      command_parameter_;
    Subscription     command_changed_; // command_->observe()
    std::string      navigate_uri_;

    void command_changed();

protected:
    /// The ring follows the button's own corner radius.
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override
    {
        return theme.resolved_button_corner_radius();
    }
    void enabled_changed() override;
    /// Called by Window. Losing focus cancels an Enter/Space press in progress. Redraws.
    void focus_changed(bool focused) override;
    /// Enter or Space down presses the button, and up (while still focused) clicks it.
    bool key_override(const KeyEvent& e) override;

protected:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke = this;
    }

private:
    // A full click, as activate(), when enabled; false and nothing happens when disabled.
    bool invoke() override
    {
        if (!is_enabled())
        {
            return false;
        }
        activate();
        return true;
    }
};

/// A push button. Builds a ui::ButtonControl.
///
/// `.text` is the label; `.content` replaces it with any subtree (an icon beside text, say).
/// A click runs `.on_click`, then `.command`, then opens `.navigate_uri`. With a `.command` the
/// button is enabled only while both `.enabled` and the command's `enabled` allow it, and an
/// unset `.tool_tip` falls back to the command's description.
///
/// Example:
/// @code
/// ui::Property<int> clicks{0};
/// ui::Property<bool> dirty{false};
/// ui::Command       save{.enabled = dirty, .execute = [&] { save_document(); }};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
///         ui::Button{.text = "Save", .command = save}, // enablement comes with the command
///         ui::Button{.variant = ui::ButtonVariant::Accent,
///                    .content = ui::HorizontalStack{
///                        .spacing  = 8,
///                        .content  = {ui::Icon{.source = ui::glyphs::More}, ui::Text{.text = "More"}},
///                    }},
///         ui::Button{.text         = "Docs",
///                    .navigate_uri = "https://example.com/docs",
///                    .variant      = ui::ButtonVariant::Hyperlink},
///     },
/// });
/// @endcode
struct Button
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Button>` points at.
    using control_type = ButtonControl;

    // ---- properties ----
    /// The label, ignored while `.content` is set.
    Binding<std::string> text{};
    /// A URI (ui::ButtonControl::set_navigate_uri()), opened with its default
    /// handler on every click, after `.on_click` and `.command`.
    Binding<std::string> navigate_uri{};
    /// Neutral, Accent, Subtle or Hyperlink (ui::ButtonControl::set_variant()).
    Binding<ButtonVariant> variant{ButtonVariant::Neutral};
    /// The padding around the text or content; nullopt keeps the theme's button padding.
    Binding<std::optional<Thickness>> padding{};
    /// The fill at rest (ui::ButtonControl::set_background()): a fixed color or a theme resource,
    /// which follows theme switches. Unset keeps the style's.
    Binding<Paint> background{};
    /// The label color at rest: a fixed color or a theme resource, which follows theme
    /// switches. Unset keeps the style's.
    Binding<Paint> foreground{};

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
    /// Shown when the pointer rests on it. Unset: the command's description.
    Binding<std::string> tool_tip{};
    Binding<bool>        enabled{true};   ///< False grays it out and takes no input.
    Accessibility        accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// A shared, named action (ui/Command.hpp), run after `.on_click` with
    /// `.command_parameter`. The button is enabled only while both the command and `.enabled`
    /// allow it, and follows the command as it changes. Written as the command itself:
    /// `.command = save`.
    CommandRef command{};
    /// Handed to `.command` when it runs.
    std::string command_parameter{};
    /// A completed press and release inside, or Enter/Space while focused. Runs before
    /// `.command`.
    Action on_click{};
    /// A context menu was requested (Element::on_context_requested()). Returning nothing, or
    /// ui::Handled, means a menu or flyout was shown; ui::NotHandled lets the request go on.
    Callback<Handling(std::optional<Point>)> on_context_requested{};
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
    ControlOut<Button> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Arbitrary content instead of a plain label, such as an icon plus text. When set it
    /// replaces `.text` for layout and rendering entirely.
    Spec content{};

    /// Consumes the spec and returns the built ui::ButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
