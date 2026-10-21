// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/input/Button.hpp"
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

/// A chrome row with an icon, title and subtitle, optional back and pane-toggle (hamburger)
/// buttons, and three content slots (left header, content, right header).
///
/// The bar itself takes no focus. The back and pane-toggle buttons (subtle Buttons) and whatever
/// sits in the slots are each their own Tab stop. Use it either as a row at
/// the top of a page, or as the window's caption in a window with
/// WindowStyle::extend_content_into_title_bar. In a window, the bar follows it: it keeps the right
/// header clear of the space Window::caption_insets() reports for the caption buttons, and dims
/// while Window::active() is false. hit_region() then tells the host which parts drag the
/// window. set_caption_insets() and set_active() override what the window says, for a bar drawn
/// somewhere other than its own window's caption. The bar has no minimize, maximize or close
/// buttons, since the window keeps its own. It doesn't set the window's title from title(), so
/// an app sets both.
///
/// Example:
/// @code
/// ui::Property<bool>                can_go_back{false};
/// ui::Control<ui::NavigationFrame>  frame;
/// ui::Control<ui::NavigationShell>  nav;
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::TitleBar{
///         .title                      = "Contoso Notes",
///         .subtitle                   = "Preview",
///         .icon                       = ui::IconGlyph{0xE70B},
///         .back_button_visible        = can_go_back,
///         .pane_toggle_button_visible = true,
///         .content_alignment          = ui::Alignment::Stretch,
///         .on_back_requested          = [&] { frame->go_back(); },
///         .on_pane_toggle_requested   = [&] { nav->set_pane_open(!nav->is_pane_open()); },
///         .content                    = ui::TextBox{.placeholder_text = "Search", .max_width = 360.0f},
///     },
///     // ... the page below the bar ...
/// }});
/// // Run the window with ui::WindowOptions{.style = {.extend_content_into_title_bar = true}} to make
/// // the bar the caption; without it the bar is an ordinary row under the standard title bar.
/// @endcode
class TitleBarControl final : public Element
{
public:
    /// A bar showing `title` and, beside it in the secondary text color, `subtitle`. Both
    /// buttons start hidden, the slots empty.
    explicit TitleBarControl(std::string title = "", std::string subtitle = "");

    // ---- text ----
    /// The primary caption text. Empty draws none.
    [[nodiscard]] const std::string& title() const
    {
        return title_;
    }
    /// Replaces the title and updates the layout. Does not change the window's title.
    void set_title(std::string title)
    {
        title_ = std::move(title);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Dimmed secondary text after the title. Empty draws none.
    [[nodiscard]] const std::string& subtitle() const
    {
        return subtitle_;
    }
    /// Replaces the subtitle and updates the layout.
    void set_subtitle(std::string subtitle)
    {
        subtitle_ = std::move(subtitle);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- icon ----
    /// The icon left of the title: a glyph at 16 DIPs in the title text's color, or a path or
    /// picture fitted into the same 16x16 box. Empty (the default) shows none.
    [[nodiscard]] const IconSource& icon() const
    {
        return icon_;
    }
    /// Shows `icon`; an empty source removes it and the title moves left into the freed space.
    /// Updates the layout.
    void set_icon(IconSource icon)
    {
        icon_ = std::move(icon);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- content alignment ----
    /// Where the content sits in the space between the title and the right header. Center, the
    /// default, keeps the content at its own width, centered. Stretch hands it the whole space,
    /// which a search box with a max width wants.
    [[nodiscard]] Alignment content_alignment() const
    {
        return content_alignment_;
    }
    /// Changes the content slot's alignment and updates the layout.
    void set_content_alignment(Alignment alignment)
    {
        content_alignment_ = alignment;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- back button ----
    /// Whether the back button is shown; false by default. A hidden button takes no space, is not a
    /// Tab stop and is not hit-tested.
    [[nodiscard]] bool is_back_button_visible() const
    {
        return back_button_visible_;
    }
    /// Shows or hides the back button and updates the layout.
    void set_back_button_visible(bool visible)
    {
        back_button_visible_ = visible;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Whether the back button is enabled; true by default. A disabled back button stays visible but
    /// grayed, cannot be clicked, and drags the window when the bar is the caption.
    [[nodiscard]] bool is_back_button_enabled() const
    {
        return back_enabled_;
    }
    /// Enables or disables the back button (ButtonControl::set_enabled()); a disabled bar keeps
    /// it disabled either way.
    void set_back_button_enabled(bool enabled)
    {
        back_enabled_ = enabled;
        back_button_->set_enabled(back_enabled_ && is_enabled());
    }

    /// Fired on a completed click (or Enter/Space) of the back button.
    /// It cannot fire while the button is hidden, since a hidden button is not hit-tested.
    /// Replaces any previous handler.
    void on_back_requested(Callback<void()> handler)
    {
        on_back_requested_ = std::move(handler);
    }

    // ---- pane-toggle (hamburger) button ----
    /// Whether the pane-toggle button is shown; false by default. Typically paired with a
    /// NavigationShell whose own pane button is hidden.
    [[nodiscard]] bool is_pane_toggle_button_visible() const
    {
        return pane_toggle_visible_;
    }
    /// Shows or hides the pane-toggle button and updates the layout.
    void set_pane_toggle_button_visible(bool visible)
    {
        pane_toggle_visible_ = visible;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Fired when the pane-toggle button is clicked. The bar keeps
    /// no pane state of its own; the handler opens or closes the pane. Replaces any previous
    /// handler.
    void on_pane_toggle_requested(Callback<void()> handler)
    {
        on_pane_toggle_requested_ = std::move(handler);
    }

    // ---- header / content slots ----
    /// The left header: the slot after the buttons and before the icon. Each slot setter
    /// takes ownership of an arbitrary Element, replaces (and destroys) what was there, and hands
    /// back a non-owning observer, the same shape as ExpanderControl::set_content().
    template <typename T>
    T* set_left_header_content(std::unique_ptr<T> element)
    {
        static_assert(std::is_base_of_v<Element, T>, "TitleBar left header must be an Element");
        T* observer = element.get();
        set_parent_of(*observer, this);
        left_header_ = std::move(element);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The left header, or null when the slot is empty. Owned by the bar.
    [[nodiscard]] Element* left_header_content() const
    {
        return left_header_.get();
    }

    /// The content: the middle slot, between the title and the right header, placed per
    /// content_alignment(), typically a search box.
    template <typename T>
    T* set_content(std::unique_ptr<T> element)
    {
        static_assert(std::is_base_of_v<Element, T>, "TitleBar content must be an Element");
        T* observer = element.get();
        set_parent_of(*observer, this);
        content_ = std::move(element);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The middle slot's element, or null. Owned by the bar.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    /// The right header: the slot at the far end, kept clear of the caption buttons by
    /// caption_insets(), typically a PersonPicture or a settings button.
    template <typename T>
    T* set_right_header_content(std::unique_ptr<T> element)
    {
        static_assert(std::is_base_of_v<Element, T>, "TitleBar right header must be an Element");
        T* observer = element.get();
        set_parent_of(*observer, this);
        right_header_ = std::move(element);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The right header, or null. Owned by the bar.
    [[nodiscard]] Element* right_header_content() const
    {
        return right_header_.get();
    }

    // ---- as the window's caption ----
    /// The caption space the bar keeps free, in DIPs: the value set with set_caption_insets(),
    /// otherwise what the window the bar is in reports (Window::caption_insets()), all zero
    /// outside a window. All zero is a TitleBar inside the content. Anything else also keeps a
    /// 48 DIP strip free at the right for dragging the window.
    [[nodiscard]] const CaptionInsets& caption_insets() const
    {
        return caption_insets_;
    }
    /// The caption space to leave alone, in place of the window's. std::nullopt (the default)
    /// follows the window again. A change updates the layout.
    void set_caption_insets(std::optional<CaptionInsets> insets);
    /// Whether the bar draws as an active window's caption: the value set with set_active(),
    /// otherwise Window::active() of the window the bar is in, true outside a window.
    [[nodiscard]] bool active() const
    {
        return active_;
    }
    /// false dims the bar (to half opacity) the way a deactivated window's caption is dimmed,
    /// whatever the window says; std::nullopt (the default) follows the window again. Redraws
    /// only on a change.
    void set_active(std::optional<bool> active);
    /// Caption over the bar's own background. The visible back button (while enabled), the pane
    /// toggle and both headers take the press; in the content, the enabled focusable controls do
    /// and everything else drags, subject to Element::title_bar_drag_region().
    [[nodiscard]] HitRegion hit_region(Point p) const override;

    // ---- children ----
    /// Lists the back and pane-toggle buttons while they show, then the left header, the content
    /// and the right header when present. The icon is decoration, not a child: no tab stop, no
    /// popups, no automation node.
    void for_each_child(ChildVisitor visit) const override;

    // ---- automation ----
    /// UI Automation's Header control type.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Header;
    }
    /// The title, when no explicit accessible name is set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return title_;
    }

protected:
    // ---- layout ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----

    // ---- render ----
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- window ----
    void attached() override;
    void detached() override;

private:
    void render_parts(Renderer& renderer, const Theme& theme) const;
    // Works out caption_insets_ and active_ from the values set and the window's.
    void update_from_window();

    std::string title_;
    std::string subtitle_;
    IconSource  icon_{};
    Alignment   content_alignment_ = Alignment::Center;

    bool back_button_visible_ = false;
    bool pane_toggle_visible_ = false;
    bool back_enabled_        = true;

    CaptionInsets                caption_insets_{};   // in effect
    bool                         active_ = true;      // in effect
    std::optional<CaptionInsets> caption_insets_set_; // set_caption_insets(); nullopt follows the window
    std::optional<bool>          active_set_;         // set_active(); nullopt follows the window
    const Window*                followed_ = nullptr; // the window followed while attached, or null
    Subscription                 window_insets_;      // the window's caption_insets() while attached
    Subscription                 window_active_;      // the window's active() while attached

    std::unique_ptr<ButtonControl> back_button_;
    std::unique_ptr<ButtonControl> pane_toggle_button_;

    std::unique_ptr<Element> left_header_;
    std::unique_ptr<Element> content_;
    std::unique_ptr<Element> right_header_;

    Callback<void()> on_back_requested_;
    Callback<void()> on_pane_toggle_requested_;

    // Set by measure_override() for arrange_override() and render().
    mutable float row_height_     = 0.0f;
    mutable float left_header_w_  = 0.0f;
    mutable float title_w_        = 0.0f;
    mutable float subtitle_w_     = 0.0f;
    mutable float right_header_w_ = 0.0f;
    mutable float content_w_      = 0.0f;
    // Desired heights of the three slots, which sit centered in the row at that height.
    mutable float left_header_h_  = 0.0f;
    mutable float right_header_h_ = 0.0f;
    mutable float content_h_      = 0.0f;

    Rect icon_rect_{};
    Rect title_rect_{};
    Rect subtitle_rect_{};

protected:
    void enabled_changed() override
    {
        back_button_->set_enabled(back_enabled_ && is_enabled());
        pane_toggle_button_->set_enabled(is_enabled());
    }
};

/// A horizontal chrome row: icon, title, subtitle, an optional back/pane-toggle button, and
/// three arbitrary content slots. Builds a ui::TitleBarControl.
///
/// Example:
/// @code
/// using ui::operator""_fr;
/// ui::Control<ui::NavigationFrame> frame;
///
/// window.set_content(ui::Grid{
///     .rows    = {ui::AutoSize, 1_fr},
///     .content = {
///         ui::Cell{.row     = 0,
///                  .content = ui::TitleBar{
///                      .title               = "Contoso Mail",
///                      .subtitle            = "Preview",
///                      .icon                = ui::symbol_glyph(ui::Symbol::Mail),
///                      .back_button_visible = true,
///                      .on_back_requested   = [&] { frame->go_back(); },
///                      .content             = ui::AutoSuggestBox{.placeholder_text = "Search", .width = 300.0f},
///                  }},
///         ui::Cell{.row = 1, .content = ui::NavigationFrame{.control = frame}},
///     },
/// });
/// @endcode
struct TitleBar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<TitleBar>` points at.
    using control_type = TitleBarControl;

    // ---- properties ----
    /// Title: the window's name, after the icon.
    Binding<std::string> title{};
    /// Subtitle: secondary text after the title, in the caption style.
    Binding<std::string> subtitle{};
    /// The icon left of the title: a Symbol, a glyph, a path or a picture; empty shows none.
    Binding<IconSource> icon{};
    /// Show the back button at the far left.
    Binding<bool> back_button_visible{false};
    /// False shows the back button grayed out.
    Binding<bool> back_button_enabled{true};
    /// Show the hamburger button (typically for a NavigationShell).
    Binding<bool> pane_toggle_button_visible{false};
    /// The caption space to keep clear of the window's caption buttons. Unset follows the window the
    /// bar is in (Window::caption_insets()); a value or a binding wins, for a bar that is not its
    /// own window's caption.
    Binding<std::optional<CaptionInsets>> caption_insets{};
    /// false dims the bar. Unset follows the window the bar is in (Window::active()); a value or
    /// a binding wins.
    Binding<std::optional<bool>> active{};
    /// Where the content sits between the title and the right header
    /// (ui::TitleBarControl::set_content_alignment()).
    Binding<Alignment> content_alignment{Alignment::Center};

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
    Binding<bool>                     enabled{true}; ///< False grays out the bar's buttons.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The back button was clicked.
    Callback<void()> on_back_requested{};
    /// The pane-toggle button was clicked.
    Callback<void()> on_pane_toggle_requested{};
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
    ControlOut<TitleBar> control{}; ///< Out: the handle that receives the built control.

    // ---- children (TitleBar's own left-header / content / right-header slot order) ----
    /// The left slot, next to the buttons and title.
    Spec left_header_content{};
    /// Content: the middle slot, placed per `content_alignment`.
    Spec content{};
    /// The right slot; kept clear of the caption buttons via `caption_insets`.
    Spec right_header_content{};

    /// Consumes the spec and returns the built ui::TitleBarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
