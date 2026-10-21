// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
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

namespace detail
{
/// Which of the tip's fixed spots the card settled in; defined in TeachingTip.cpp.
enum class TeachingTipSpot : unsigned char;
} // namespace detail

/// Which edge of the card the hero content sits at: Auto puts it at the edge away from the tail.
enum class TeachingTipHeroPlacement
{
    Auto,   ///< The edge away from the tail; the top when there is no tail at the top.
    Top,    ///< Always across the top of the card.
    Bottom, ///< Always across the bottom of the card.
};

class TeachingTipSurface; // defined in TeachingTip.cpp

/// A card that points at a control (or sits against the window) to teach something about it.
///
/// The element itself takes no room where it is placed in the tree. While open, its card floats
/// above the window, placed against target() by placement() and placement_alignment(). It falls
/// back to the other sides when there is no room. The card holds an optional icon, the title and
/// subtitle, optional content, optional hero content, and up to two buttons. The tip owns its
/// content and hero content. The target is a non-owning pointer that must outlive the tip or be
/// cleared first. The target usually exists only once the tree is built, so set it right before
/// opening, or give the spec a `.target` handle, which links in either build order.
///
/// Example:
/// @code
/// ui::Control<ui::Button> show;
/// ui::Control<ui::TeachingTip> tip;
///
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::Button{.text = "Show TeachingTip", .on_click = [&] { tip->set_open(true); }, .control = show},
///         ui::TeachingTip{.title                 = "Save automatically",
///                         .subtitle              = "Your work is saved to the cloud as you go.",
///                         .action_button_text = "Got it",
///                         .close_button_text  = "Close",
///                         .target                = show,
///                         .placement             = ui::PopupPlacement::Below,
///                         .on_closed             = [](ui::PopupCloseReason) { /* remember it */ },
///                         .control               = tip,
///                         .content               = ui::Text{.text     = "Turn it off in Settings.",
///                                                           .wrapping = ui::TextWrapping::WrapWholeWords}},
///     },
/// });
///
/// // Or imperatively, next to the target in the same window:
/// auto  stack  = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* target = stack->add(std::make_unique<ui::ButtonControl>("Show TeachingTip"));
/// auto* t      = stack->add(std::make_unique<ui::TeachingTipControl>());
/// t->set_title("This is the title");
/// t->set_subtitle("And this is the subtitle");
/// t->set_light_dismiss(true);
/// target->on_click([t, target] {
///     t->set_target(target);
///     t->set_open(true);
/// });
/// @endcode
class TeachingTipControl final : public Element
{
public:
    /// A closed, untargeted tip with no text, no content and no buttons (only the corner "X"). Not
    /// hit-test visible: it draws nothing where it sits, so a press there goes to what is under it.
    TeachingTipControl();
    ~TeachingTipControl() override;

    // ---- text and content ----
    /// Sets the title.
    void set_title(std::string title);
    /// The semibold first line of the card, wrapping at whole words; hidden when empty. Screen
    /// readers also use it as the name when none is set.
    [[nodiscard]] const std::string& title() const
    {
        return title_;
    }
    /// Sets the subtitle.
    void set_subtitle(std::string subtitle);
    /// The line under the title, wrapping at whole words; hidden when empty.
    [[nodiscard]] const std::string& subtitle() const
    {
        return subtitle_;
    }
    /// Sets the icon, in its own column left of the titles. A glyph shows at its own size, which
    /// is 20 DIPs for a Symbol. Anything else fills a 20 DIP square.
    void set_icon(IconSource icon);
    /// The icon left of the titles; empty (the default) drops the icon column.
    [[nodiscard]] const IconSource& icon() const
    {
        return icon_;
    }

    /// Takes ownership of the content under the titles and returns a non-owning observer.
    /// Replaces and destroys any previous content; nullptr clears it.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "TeachingTip content must be an Element");
        T* observer = content.get();
        adopt(content_, std::move(content));
        return observer;
    }
    /// The content under the titles, owned by the tip; nullptr when none.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

    /// Takes ownership of the hero content, typically an image, drawn edge to edge across the
    /// card's top or bottom, and returns a non-owning observer. Replaces any previous hero.
    template <typename T>
    T* set_hero_content(std::unique_ptr<T> hero)
    {
        static_assert(std::is_base_of_v<Element, T>, "TeachingTip hero content must be an Element");
        T* observer = hero.get();
        adopt(hero_, std::move(hero));
        return observer;
    }
    /// The hero content, owned by the tip; nullptr when none.
    [[nodiscard]] Element* hero_content() const
    {
        return hero_.get();
    }
    /// Sets which edge the hero sits at.
    void set_hero_content_placement(TeachingTipHeroPlacement placement);
    /// Which edge the hero sits at; Auto (the default) puts the hero away from the tail.
    [[nodiscard]] TeachingTipHeroPlacement hero_content_placement() const
    {
        return hero_placement_;
    }

    /// Sets the action button's text; empty hides the button. The same goes for the close
    /// button. The action button only raises on_action_button_click; the close button also
    /// closes the tip.
    void set_action_button_text(std::string text);
    /// The action button's text; empty (the default) hides it.
    [[nodiscard]] const std::string& action_button_text() const
    {
        return action_text_;
    }
    /// Sets the close button's text; a non-empty one also hides the corner "X".
    void set_close_button_text(std::string text);
    /// The close button's text; empty (the default) hides it.
    [[nodiscard]] const std::string& close_button_text() const
    {
        return close_text_;
    }
    /// The action button, owned by the tip (for styling); shown only when
    /// action_button_text() is non-empty.
    [[nodiscard]] ButtonControl* action_button() const
    {
        return action_button_.get();
    }
    /// The close button, owned by the tip; shown only when close_button_text() is non-empty.
    [[nodiscard]] ButtonControl* close_button() const
    {
        return close_button_.get();
    }
    /// The corner "X". It shows only when there is no close button text and light dismiss is off
    /// (alternate_close_visible()).
    [[nodiscard]] ButtonControl* alternate_close_button() const
    {
        return alternate_close_.get();
    }
    /// Whether the corner "X" is on screen: no close button text and light dismiss off.
    [[nodiscard]] bool alternate_close_visible() const
    {
        return close_text_.empty() && !light_dismiss_;
    }

    /// A disabled tip grays out its action, close and corner buttons, which then take
    /// no input. Its content keeps its own state, and light dismiss and Escape still close it.
    [[nodiscard]] bool enabled() const
    {
        return close_button_->is_enabled();
    }

    // ---- placement ----
    /// The element the tail points at; nullptr for an untargeted tip. Non-owning: a target that
    /// is destroyed while the tip points at it leaves the tip untargeted. Setting it makes the
    /// next layout re-decide the placement.
    void set_target(Element* target);
    /// The current target (non-owning); nullptr (the default) for an untargeted tip.
    [[nodiscard]] Element* target() const
    {
        return target_.get();
    }
    /// Sets the side tried first; nullopt (the default) tries above the target first, and puts an
    /// untargeted tip at the bottom of the window. The next layout re-decides the placement.
    void set_placement(std::optional<PopupPlacement> placement);
    /// The side tried first, before the other sides; nullopt by default. For an untargeted tip,
    /// the window edge the card sits against.
    [[nodiscard]] std::optional<PopupPlacement> placement() const
    {
        return placement_;
    }
    /// Sets how the card lines up with the tail; the next layout re-decides the placement.
    void set_placement_alignment(PopupPlacementAlignment alignment);
    /// How the card lines up with the tail on its side; Center by default. Leading runs the card
    /// right (or down) from the tail, Trailing left (or up). For an untargeted tip, which end of
    /// the window edge the card sits at.
    [[nodiscard]] PopupPlacementAlignment placement_alignment() const
    {
        return alignment_;
    }
    /// Sets the placement margin; takes effect at the next layout.
    void set_placement_margin(Thickness margin);
    /// The placement margin, in DIPs; zero by default. For a targeted tip, it is the gap between
    /// the target and the card. Only the side the card is on counts, such as `top` for a card
    /// above the target or `left` for one to its left. For an untargeted tip, it is the gap from
    /// the window's edges, added to the fixed edge inset. A card centered on an axis moves by
    /// `left - right` or `top - bottom`.
    [[nodiscard]] Thickness placement_margin() const
    {
        return placement_margin_;
    }
    /// Sets when the tail is drawn.
    void set_tail_visibility(AutoVisibility visibility);
    /// When the tail is drawn; Auto (the default) draws the tail only for a targeted tip.
    [[nodiscard]] AutoVisibility tail_visibility() const
    {
        return tail_visibility_;
    }
    /// The side the open tip settled on, Below for an untargeted one. Only meaningful after a
    /// layout pass with the tip open. When no side fits, the card is centered over the target and
    /// this reports Above.
    [[nodiscard]] PopupPlacement effective_placement() const;
    /// How the open tip lines up with its tail on that side.
    [[nodiscard]] PopupPlacementAlignment effective_placement_alignment() const;
    /// Whether the tail is drawn for the current placement.
    [[nodiscard]] bool tail_shown() const;

    /// Turns light dismiss on or off; false by default. With it on, a click outside or Escape
    /// closes the tip with LightDismiss, and the card fills with in-app acrylic instead of its
    /// solid background. The tip also takes keyboard focus while open, and the corner "X" is hidden.
    void set_light_dismiss(bool enabled);
    /// Whether light dismiss is on; see set_light_dismiss().
    [[nodiscard]] bool light_dismiss() const
    {
        return light_dismiss_;
    }

    // ---- open / close ----
    /// Whether the tip is open; false by default. Stays true when an on_closing() handler cancels.
    [[nodiscard]] bool is_open() const
    {
        return open_;
    }
    /// Opens or closes the tip. Opening fires on_opened() at once and places the card at the next
    /// layout. A tip that fits nowhere then closes with Programmatic. Closing this way reports
    /// Programmatic, and on_closing() can cancel it. Setting the current state does nothing.
    void set_open(bool open);

    /// Fires when the action button is clicked; the tip stays open.
    void on_action_button_click(Callback<void()> handler)
    {
        on_action_button_click_ = std::move(handler);
    }
    /// Fires when the close button or the corner "X" is clicked, before the tip closes with
    /// CloseButton.
    void on_close_button_click(Callback<void()> handler)
    {
        on_close_button_click_ = std::move(handler);
    }
    /// Fires with the reason before an open tip closes; return ui::Cancel to keep the tip open.
    void on_closing(Callback<Decision(const PopupClosingArgs&)> handler)
    {
        on_closing_ = std::move(handler);
    }
    /// Fires with the reason once the tip has closed (not when canceled).
    void on_closed(Callback<void(PopupCloseReason)> handler)
    {
        on_closed_ = std::move(handler);
    }
    /// Fires from set_open(true) as the tip opens. There is no opening animation to wait for.
    void on_opened(Callback<void()> handler)
    {
        on_opened_ = std::move(handler);
    }

    // ---- automation ----
    /// Always AutomationRole::Pane. Screen readers reach the floating card the way they reach any
    /// popup's content.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Pane;
    }
    /// The title, when no accessibility name is set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return title_;
    }

protected:
    // ---- Element: nothing in place ----
    /// Always Size{0, 0}: the tip takes no room where it sits in the tree.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Nothing to arrange in place; the card is placed by the popup.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws nothing in place. The card draws above the window.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Also clears the content, hero and buttons, which are not ordinary children.
    void clear_dirty_override(DirtyFlags flags) override;
    /// Forwards the tick to the card's children (button animations) while open.
    void tick_override(Duration delta) override;


private:
    // Opens or closes the popup to match open_, and updates light dismiss.
    void sync_popup();
    // Decides the placement (against target() or the window) once per open and arranges the
    // card in the `client` area; a card that fits nowhere closes the tip with Programmatic.
    bool place_card(Element& content, Size client, const MeasureContext& context);

    friend class TeachingTipSurface;

    void adopt(std::unique_ptr<Element>& slot, std::unique_ptr<Element> element);
    void close(PopupCloseReason reason);
    void update_buttons();

    // The first placement that fits; nullopt when the card fits nowhere.
    [[nodiscard]] std::optional<detail::TeachingTipSpot> fit_targeted(Size tip, Rect target, Size client) const;
    // The spot the open card settled in (Top before the first placement).
    [[nodiscard]] detail::TeachingTipSpot spot() const;
    // Where the hero goes for a placement (Auto: away from the tail).
    [[nodiscard]] bool hero_at_bottom() const;

    std::string                    title_;
    std::string                    subtitle_;
    IconSource                     icon_;
    std::unique_ptr<Element>       content_;
    std::unique_ptr<Element>       hero_;
    std::string                    action_text_;
    std::string                    close_text_;
    std::unique_ptr<ButtonControl> action_button_;
    std::unique_ptr<ButtonControl> close_button_;
    std::unique_ptr<ButtonControl> alternate_close_;

    ElementWatch                  target_;
    std::optional<PopupPlacement> placement_{};
    PopupPlacementAlignment       alignment_        = PopupPlacementAlignment::Center;
    TeachingTipHeroPlacement      hero_placement_   = TeachingTipHeroPlacement::Auto;
    AutoVisibility                tail_visibility_  = AutoVisibility::Auto;
    Thickness                     placement_margin_ = {};
    bool                          light_dismiss_    = false;
    bool                          open_             = false;
    bool                          closing_          = false; // re-entrancy guard for close()

    // Kept while open.
    std::optional<detail::TeachingTipSpot> effective_;
    bool                                   targeted_ = false;

    std::unique_ptr<TeachingTipSurface> surface_; // built on the first open

    Callback<void()>                            on_action_button_click_;
    Callback<void()>                            on_close_button_click_;
    Callback<Decision(const PopupClosingArgs&)> on_closing_;
    Callback<void(PopupCloseReason)>            on_closed_;
    Callback<void()>                            on_opened_;

protected:
    void enabled_changed() override;
};

/// A card pointing at a control, or at the window, to teach something. Builds a
/// ui::TeachingTipControl, which takes no room in layout. Set its target with `.target`, or
/// through `.control` once the target exists. Without a target it sits against the window's edge.
///
/// Example:
/// @code
/// ui::Control<ui::Button> save_button;
/// ui::Control<ui::TeachingTip> tip;
///
/// window.set_content(ui::VerticalStack{ .content = {
///     ui::Button{ .text = "Save", .control = save_button },
///     ui::TeachingTip{ .title                = "Saving automatically",
///                      .subtitle             = "We save your changes as you go.",
///                      .close_button_text = "Got it",
///                      .placement            = ui::PopupPlacement::Below,
///                      .control              = tip },
/// }});
/// tip->set_target(save_button);
/// tip->set_open(true);
/// @endcode
struct TeachingTip
{
    /// The control this spec builds; what `ui::Control<TeachingTip>` points at.
    using control_type = TeachingTipControl;

    // ---- properties ----
    /// The semibold first line.
    Binding<std::string> title{};
    /// The line under the title.
    Binding<std::string> subtitle{};
    /// Whether the tip is open. `ui::bind(prop)` is two-way. Closing the tip writes false into the
    /// property, and a false that `on_closing` cancels is put back to true.
    TwoWayField<bool> is_open{false};
    /// The icon left of the title: a Symbol, a glyph, a path or a picture; empty draws none.
    Binding<IconSource> icon{};
    /// The accent button's caption; empty shows no action button.
    Binding<std::string> action_button_text{};
    /// A text close button; empty (with light dismiss off) shows a corner "X" instead.
    Binding<std::string> close_button_text{};
    /// Whether an outside click or Escape closes it.
    Binding<bool> light_dismiss{false};
    /// The element the tail points at, by its handle (`.target = save` with a
    /// `ui::Control<ui::Button>`), built before or after the tip. Unset for an untargeted tip.
    ElementLink target{};
    /// The side of the target tried first; the tip moves to another side when there is no room.
    /// Unset tries above first, and puts an untargeted tip at the bottom of the window.
    Binding<std::optional<PopupPlacement>> placement{};
    /// How the card lines up with the tail: Leading runs it right (or down) from the tail,
    /// Trailing left (or up).
    Binding<PopupPlacementAlignment> placement_alignment{PopupPlacementAlignment::Center};
    /// The gap in DIPs to the target on the side the card is on, or for an
    /// untargeted tip the gap from the window's edges (ui::TeachingTipControl::placement_margin()).
    Binding<Thickness> placement_margin{};
    /// Whether `.hero_content` goes above or below the text.
    Binding<TeachingTipHeroPlacement> hero_content_placement{TeachingTipHeroPlacement::Auto};
    /// When the tail pointing at the target is drawn.
    Binding<AutoVisibility> tail_visibility{AutoVisibility::Auto};

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
    Binding<bool>                     enabled{true}; ///< False grays out the tip's buttons.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Fires when the action button is clicked. Does not close the tip by itself.
    Action on_action_button_click{};
    /// Fires when the close button is clicked, before the tip closes.
    Action on_close_button_click{};
    /// The tip has opened. A two-way `is_open` has been written by then.
    Action on_opened{};
    /// Gets why the tip is about to close; return ui::Cancel to keep it open.
    Callback<Decision(const PopupClosingArgs&)> on_closing{};
    /// Raised however it closed, which is where a bound `is_open` learns about it.
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
    /// Out: the handle that receives the built control, through which to set_target().
    ControlOut<TeachingTip> control{};

    // ---- children ----
    /// Extra content under the subtitle.
    Spec content{};
    /// An edge-to-edge picture at the top or bottom of the card.
    Spec hero_content{};

    /// Consumes the spec and returns the built ui::TeachingTipControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
