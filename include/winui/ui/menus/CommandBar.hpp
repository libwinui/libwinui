// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// What a closed CommandBar shows.
enum class AppBarClosedDisplayMode
{
    Compact, ///< the icons, 48 tall
    Minimal, ///< the More button only, 24 tall
    Hidden,  ///< nothing, 0 tall
};

/// A toolbar of app-bar buttons with an overflow menu behind a More ("...") button.
///
/// Primary commands (AppBarButton, AppBarToggleButton, AppBarSeparator, or any Element) sit in a
/// row against the bar's right edge, with optional content on the left. Secondary commands are in
/// the overflow menu. Opening the bar with the More button or set_open() shows the overflow menu
/// and the text of commands whose text goes at the Bottom. The open bar is drawn over whatever lies
/// below it, or above it when it opens upward, and never moves other elements. A click on a
/// command closes the bar, and so does a click outside or Escape unless is_sticky(). With dynamic
/// overflow on (the default), primary commands that do not fit move into the overflow and come
/// back when there is room. The shortcuts of secondary commands fire app-wide while the bar is in
/// the window.
///
/// Example:
/// @code
/// auto  bar     = std::make_unique<ui::CommandBarControl>();
/// auto* add     = bar->add_primary(std::make_unique<ui::AppBarButtonControl>("Add", ui::Symbol::Add));
/// bar->add_primary(std::make_unique<ui::AppBarSeparatorControl>());
/// bar->add_primary(std::make_unique<ui::AppBarToggleButtonControl>("Shuffle", ui::Symbol::Shuffle));
/// auto* settings = bar->add_secondary(std::make_unique<ui::AppBarButtonControl>("Settings", ui::Symbol::Setting));
/// bar->set_content(std::make_unique<ui::TextControl>("Now playing"));
/// bar->set_default_text_position(ui::CommandBarDefaultTextPosition::Right);
/// add->on_click([] { /* add an item */ });
/// settings->on_click([] { /* open settings */ });
/// window.set_content(std::move(bar));
///
/// // Declaratively:
/// window.set_content(ui::CommandBar{
///     .default_text_position = ui::CommandBarDefaultTextPosition::Right,
///     .content   = ui::Text{.text = "Now playing", .vertical_alignment = ui::Alignment::Center},
///     .primary   = {ui::AppBarButton{.text = "Add", .icon = ui::Symbol::Add},
///                   ui::AppBarSeparator{},
///                   ui::AppBarToggleButton{.text = "Shuffle", .icon = ui::Symbol::Shuffle}},
///     .secondary = {ui::AppBarButton{.text = "Settings", .icon = ui::Symbol::Setting}},
/// });
/// @endcode
class CommandBarControl final : public Element, public AppBarCommandHost
{
    /// Forwards composition sync to the content and every primary.
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;

public:
    /// An empty, closed, Compact bar with the More button on Auto.
    CommandBarControl();
    /// Destroys every command, the content and the overflow.
    ~CommandBarControl() override;

    // ---- commands ----
    /// Appends `command` to the row. Takes ownership and hands back a
    /// non-owning observer (the same convention as StackControl::add()). Updates the layout.
    template <typename T>
    T* add_primary(std::unique_ptr<T> command)
    {
        static_assert(std::is_base_of_v<Element, T>, "CommandBar commands must be Elements");
        T* observer = command.get();
        insert_primary_element(primary_.size(), std::move(command));
        return observer;
    }
    /// Appends `command` to the overflow menu. Takes ownership and
    /// hands back a non-owning observer. Updates the layout.
    template <typename T>
    T* add_secondary(std::unique_ptr<T> command)
    {
        static_assert(std::is_base_of_v<Element, T>, "CommandBar commands must be Elements");
        T* observer = command.get();
        insert_secondary_element(secondary_.size(), std::move(command));
        return observer;
    }
    /// Inserts a primary command before `index` (clamped to the end), taking ownership.
    /// `command` must not be null. Updates the layout.
    void insert_primary_element(std::size_t index, std::unique_ptr<Element> command);
    /// Inserts a secondary command before `index` (clamped to the end), taking ownership.
    /// `command` must not be null. Updates the layout.
    void insert_secondary_element(std::size_t index, std::unique_ptr<Element> command);
    /// Destroys the primary command at `index`; out of range is ignored. Updates the layout.
    void remove_primary(std::size_t index);
    /// Destroys the secondary command at `index`; out of range is ignored. Updates the layout.
    void remove_secondary(std::size_t index);

    /// The number of primary commands, including any moved into the overflow.
    [[nodiscard]] std::size_t primary_count() const
    {
        return primary_.size();
    }
    /// The number of secondary commands.
    [[nodiscard]] std::size_t secondary_count() const
    {
        return secondary_.size();
    }
    /// The primary command at `index` as a non-owning observer, or nullptr when out of range.
    [[nodiscard]] Element* primary(std::size_t index) const
    {
        return index < primary_.size() ? primary_[index].get() : nullptr;
    }
    /// The secondary command at `index` as a non-owning observer, or nullptr when out of range.
    [[nodiscard]] Element* secondary(std::size_t index) const
    {
        return index < secondary_.size() ? secondary_[index].get() : nullptr;
    }

    /// Shows `content` at the bar's left, replacing any previous content; null clears
    /// it. Takes ownership and hands back a non-owning observer. Hidden while closed in
    /// Minimal/Hidden.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "CommandBar content must be an Element");
        T* observer = content.get();
        set_content_element(std::move(content));
        return observer;
    }
    /// The content as a non-owning observer, or nullptr.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    // ---- properties ----
    /// Where every command puts its text. Bottom (the default) shows the text only
    /// while the bar is open, Right always beside the icon, Collapsed never.
    [[nodiscard]] CommandBarDefaultTextPosition default_text_position() const
    {
        return text_position_;
    }
    /// Sets where every command puts its text, and lays out every command again.
    void set_default_text_position(CommandBarDefaultTextPosition position);

    /// Whether the bar is lifted over its surroundings, with the text and the overflow showing.
    /// False by default.
    [[nodiscard]] bool is_open() const
    {
        return open_;
    }
    /// Opens or closes the bar. Fires on_opening()/on_closing() at once and on_opened()/
    /// on_closed() once the transition has run; no-op when unchanged.
    void set_open(bool open);


    /// What the closed bar shows; Compact by default.
    [[nodiscard]] AppBarClosedDisplayMode closed_display_mode() const
    {
        return closed_mode_;
    }
    /// Sets what the closed bar shows and updates the layout. The closed height is 48, 24 or
    /// 0 DIPs.
    void set_closed_display_mode(AppBarClosedDisplayMode mode)
    {
        closed_mode_ = mode;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The fill behind the bar, closed and open: a fixed color or a theme resource. Empty, the
    /// default, is transparent. In Compact mode the open bar draws its acrylic over this fill.
    void set_background(Paint color)
    {
        background_ = color;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Where the open bar is drawn: over the closed bar and whatever lies below it, or above it
    /// when it opens upward. It takes no room in the layout, and the bar's bounds() stay those of
    /// the closed bar.
    [[nodiscard]] Rect open_rect() const
    {
        return open_rect_;
    }
    /// Whether the open bar went up because there was not enough room below for it and its overflow.
    [[nodiscard]] bool opens_up() const
    {
        return opens_up_;
    }

    /// Whether light dismiss is off, so an outside click or Escape leaves the bar open.
    [[nodiscard]] bool is_sticky() const
    {
        return sticky_;
    }
    /// False by default. A click on a command still closes a sticky bar.
    void set_sticky(bool sticky)
    {
        sticky_ = sticky;
        sync_popup();
    }

    /// When the More button shows. Auto, the default, shows it only when there is something to
    /// reveal: overflow entries or hidden text. With Collapsed, the pointer cannot reach the
    /// secondary commands.
    [[nodiscard]] AutoVisibility overflow_button_visibility() const
    {
        return more_visibility_;
    }
    /// Updates the layout.
    void set_overflow_button_visibility(AutoVisibility visibility)
    {
        more_visibility_ = visibility;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether dynamic overflow is on; true by default. When the bar is narrower than its
    /// commands, primary commands move into the overflow and move back when there is room. The
    /// group with the lowest dynamic_overflow_order() moves first, then the rest from the right.
    [[nodiscard]] bool is_dynamic_overflow_enabled() const
    {
        return dynamic_overflow_;
    }
    /// Updates the layout.
    void set_dynamic_overflow_enabled(bool enabled)
    {
        dynamic_overflow_ = enabled;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the primary command at `index` has been moved into the overflow by dynamic overflow.
    [[nodiscard]] bool is_primary_in_overflow(std::size_t index) const
    {
        return index < moved_.size() && moved_[index];
    }
    /// Whether the More button shows, resolving overflow_button_visibility()'s Auto against the
    /// current commands, closed mode and text position.
    [[nodiscard]] bool more_button_visible() const;

    /// Fired as is_open() turns true, before the transition. Replaces any previous
    /// handler.
    void on_opening(Callback<void()> handler)
    {
        on_opening_ = std::move(handler);
    }
    /// Fired once the opening transition has run.
    void on_opened(Callback<void()> handler)
    {
        on_opened_ = std::move(handler);
    }
    /// Fired before is_open() turns false, with why: Programmatic for set_open(false),
    /// LightDismiss for a click outside or Escape, Invoked for a command click, CloseButton for
    /// the More button. Return ui::Cancel to keep the bar open.
    void on_closing(Callback<Decision(const PopupClosingArgs&)> handler)
    {
        on_closing_ = std::move(handler);
    }
    /// Fired once the closing transition has run, with the reason on_closing() saw. This is
    /// where a bound `is_open` learns about a light dismiss.
    void on_closed(Callback<void(PopupCloseReason)> handler)
    {
        on_closed_ = std::move(handler);
    }

    /// A command in the bar was clicked, so the bar closes.
    void command_invoked(CommandBarElement& command) override;
    /// The More button was clicked, or Down/Up pressed on it: toggles the bar, and an opening
    /// moves keyboard focus into the overflow.
    void toggle_from_more_button();
    /// Lists the shown row in drawing order (the content, the primaries left in the bar, the
    /// More button), which is what Tab and automation walk. A closed bar in Minimal or Hidden mode
    /// leaves out the content and the primaries, and Hidden leaves out the More button too.
    void for_each_child(ChildVisitor visit) const override;
    /// Passes native-control hosting on to the content only.
    void sync_native(NativeHost& host) override;
    /// The shortcuts of the overflow's commands, which fire whether or not the overflow is open.
    bool handle_global_shortcut(const KeyEvent& e) override;

    // ---- automation ----
    /// Always AutomationRole::ToolBar.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::ToolBar;
    }

protected:
    // ---- Element ----
    /// The closed bar's size, with a height of 48, 24 or 0 DIPs. Also decides which primary
    /// commands move into the overflow at the available width.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the content, the primaries left in the bar and the More button.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the closed bar; the open bar is drawn over the tree by the window's popup layer.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// The commands, while the bar is closed.
    void hit_test_children(Point p, std::vector<Element*>& out) const override;
    /// Clears `flags` on this node and every owned child, shown or not: the secondaries, the
    /// separator, the open-bar layer and the overflow too.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Ticks the content and every command, shown or not, and the More button, then runs the
    /// open/close transition.
    void tick_override(Duration delta) override;


private:
    // The open-bar layer and the bar itself (for the overflow), then the live elements in the
    // shown row (command flyouts), with the More button itself for its tooltip only.
    void collect_live(std::vector<Element*>& out) override;
    // Opens, updates or closes the open-bar layer and the overflow popup to match the phase.
    void sync_popup();
    // The overflow menu, built on first use and owned by the bar.
    Element& overflow_menu();
    // Under the open bar, or over it when the bar opens up; right-aligned with it, left-aligned
    // when that would leave the window.
    bool place_overflow(Element& content, Size client, const TextMeasurer& measurer, const Theme& theme);
    // set_content() without the typed observer.
    void set_content_element(std::unique_ptr<Element> content);
    friend class CommandBarOverflow;
    friend class CommandBarMoreButton;
    friend class CommandBarLift;

    std::vector<std::unique_ptr<Element>> primary_;
    std::vector<std::unique_ptr<Element>> secondary_;
    std::unique_ptr<Element>              content_;
    std::unique_ptr<Element>              more_;      // a CommandBarMoreButton
    std::unique_ptr<Element>              overflow_;  // a CommandBarOverflow, built on first open
    std::unique_ptr<Element>              separator_; // the one dynamic overflow puts before the secondaries
    std::unique_ptr<Element>              lift_;      // a CommandBarLift: the open bar, drawn over the tree

    CommandBarDefaultTextPosition text_position_    = CommandBarDefaultTextPosition::Bottom;
    AutoVisibility                more_visibility_  = AutoVisibility::Auto;
    bool                          open_             = false;
    bool                          sticky_           = false;
    bool                          dynamic_overflow_ = true;
    bool                          focus_overflow_   = false; // opened from the More button
    AppBarClosedDisplayMode       closed_mode_      = AppBarClosedDisplayMode::Compact;
    Paint                         background_{};

    // The overflow's slide: 250 ms down into view, 167 ms back up.
    enum class Phase
    {
        Closed,
        Opening,
        Open,
        Closing,
    };
    Phase  phase_    = Phase::Closed;
    double phase_ms_ = 0.0;

    mutable std::vector<bool> moved_;        // per primary: moved into the overflow
    mutable std::vector<Size> primary_size_; // per primary, as measured in the bar
    mutable Size              content_size_{};
    mutable float             content_width_ = 0.0f;
    mutable float             closed_height_ = 48.0f; // the layout height: Compact's, 24 or 0
    mutable float             open_height_   = 48.0f; // the open bar's
    Rect                      open_rect_{};           // where the lift put the open bar
    bool                      opens_up_        = false;
    bool                      direction_known_ = false; // decided once per opening

    Callback<void()>                            on_opening_;
    Callback<void()>                            on_opened_;
    Callback<Decision(const PopupClosingArgs&)> on_closing_;
    Callback<void(PopupCloseReason)>            on_closed_;
    PopupCloseReason                            close_reason_ = PopupCloseReason::Programmatic; // for on_closed_

    // Closes with `reason`: on_closing_ first, which may cancel.
    void close(PopupCloseReason reason);

    /// What the overflow shows, in order: the moved primaries, the separator (when there are
    /// both moved primaries and secondaries), the secondaries. Collapsed ones are left out.
    [[nodiscard]] std::vector<Element*> overflow_items() const;
    void                                update_looks(bool compact) const;
    [[nodiscard]] float                 overflow_slide() const; // 0 open, 1 fully tucked away

    /// Open, or still animating closed: the bar is drawn by the lift, over the tree.
    [[nodiscard]] bool lifted() const
    {
        return phase_ != Phase::Closed;
    }
    /// Whether the content and the primaries show and take input: always while lifted, closed
    /// only in Compact.
    [[nodiscard]] bool shows_commands() const
    {
        return lifted() || closed_mode_ == AppBarClosedDisplayMode::Compact;
    }
    [[nodiscard]] bool shows_more_button() const
    {
        return more_button_visible() && (lifted() || closed_mode_ != AppBarClosedDisplayMode::Hidden);
    }
    /// What shows, in drawing order: the content, the primaries left in the bar, the More button.
    [[nodiscard]] std::vector<Element*> row() const;
    /// The row in `rect`, whose height is the bar's (the closed one, or the open one while lifted).
    void arrange_row(const Rect& rect);
    /// The lift's placement: decides the direction (once per opening), puts the open bar there
    /// and arranges the row in it.
    Rect place_lift(Size client, const TextMeasurer& measurer, const Theme& theme);
    void render_row(Renderer& renderer, const Theme& theme, const Rect& rect) const;

protected:
    void enabled_changed() override;
};

/// A toolbar of app-bar buttons with an overflow menu. Builds a ui::CommandBarControl.
///
/// Primary commands sit in the bar, and secondary commands are in the overflow menu behind the
/// More ("...") button. With dynamic overflow on, primary commands that do not fit move into the
/// overflow too. Opening the bar with the More button shows the text and the overflow.
///
/// Example:
/// @code
/// ui::Property<std::string> status;
/// window.set_content(ui::CommandBar{
///     .default_text_position = ui::CommandBarDefaultTextPosition::Right,
///     .on_closed              = [&](ui::PopupCloseReason) { status = "closed"; },
///     .content                = ui::Text{ .text = "Now playing", .vertical_alignment = ui::Alignment::Center },
///     .primary   = { ui::AppBarButton{ .text = "Add", .icon = ui::Symbol::Add, .on_click = [&] { status = "new"; } },
///                    ui::AppBarSeparator{},
///                    ui::AppBarToggleButton{ .text = "Shuffle", .icon = ui::Symbol::Shuffle } },
///     .secondary = { ui::AppBarButton{ .text = "Settings", .icon = ui::Symbol::Setting } },
/// });
/// @endcode
struct CommandBar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<CommandBar>` points at.
    using control_type = CommandBarControl;

    // ---- properties ----
    /// Whether the bar is open, with the text and the overflow menu showing.
    TwoWayField<bool> is_open{false};
    /// No light dismiss: an outside click or Escape leaves the open bar open.
    Binding<bool> sticky{false};
    /// Text under the icons, to their right, or none.
    Binding<CommandBarDefaultTextPosition> default_text_position{CommandBarDefaultTextPosition::Bottom};
    /// Auto shows the More button only when there is something to reveal.
    Binding<AutoVisibility> overflow_button_visibility{AutoVisibility::Auto};
    /// Primary commands that do not fit move into the overflow menu.
    Binding<bool> dynamic_overflow_enabled{true};
    /// Compact (48 DIPs tall), Minimal (24 DIPs, the More button only) or Hidden (0).
    Binding<AppBarClosedDisplayMode> closed_display_mode{AppBarClosedDisplayMode::Compact};
    /// The fill behind the bar: a fixed color or a theme resource. Unset is transparent. In Compact
    /// mode the open bar draws its own acrylic.
    Binding<Paint> background{};

    // ---- appearance / layout ----
    Style                             style{};                                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};                               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};                               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};                            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};                           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};                            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};                           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{};                 ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{Alignment::Leading}; ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true}; ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays out the More button.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The bar is about to open.
    Action on_opening{};
    /// The bar has opened.
    Action on_opened{};
    /// The bar is about to close. Gets the reason: a light dismiss, a command click, the More
    /// button, or `is_open` set to false. Return ui::Cancel to keep it open.
    Callback<Decision(const PopupClosingArgs&)> on_closing{};
    /// The bar has closed, with the reason. This is where a bound `is_open` learns about it.
    Callback<void(PopupCloseReason)> on_closed{};
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
    ControlOut<CommandBar> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Shown at the bar's left.
    Spec content{};
    /// The commands in the bar: ui::AppBarButton, ui::AppBarToggleButton and ui::AppBarSeparator specs.
    SpecList primary{};
    /// The overflow menu's entries, as app-bar specs.
    SpecList secondary{};

    /// Consumes the spec and returns the built ui::CommandBarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
