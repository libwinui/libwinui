// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace ui
{

/// Which pane wins when the available space fits neither Wide nor Tall.
enum class TwoPanePriority
{
    Pane1, ///< Show pane 1 alone (the default).
    Pane2, ///< Show pane 2 alone.
};

/// Which axis, if either, the two panes are split along, as mode() reports it. The order of the
/// panes on that axis comes from TwoPaneWideModeConfiguration or TwoPaneTallModeConfiguration, so
/// mode() does not tell LeftRight from RightLeft.
enum class TwoPaneMode
{
    SinglePane, ///< Only the pane_priority() pane is shown.
    Wide,       ///< Both panes, side by side.
    Tall,       ///< Both panes, one above the other.
};

/// How the panes are arranged once the width qualifies for Wide.
enum class TwoPaneWideModeConfiguration
{
    SinglePane, ///< Never split horizontally; fall through to the Tall test.
    LeftRight,  ///< Pane 1 on the left (the default).
    RightLeft,  ///< Pane 1 on the right.
};

/// How the panes are arranged once the height qualifies for Tall.
enum class TwoPaneTallModeConfiguration
{
    SinglePane, ///< Never stack; show a single pane instead.
    TopBottom,  ///< Pane 1 on top (the default).
    BottomTop,  ///< Pane 1 at the bottom.
};

/// Two content panes that sit side by side when there is width, stacked when there is height, and
/// collapse to one pane when there is neither.
///
/// Each measure pass picks the mode from the available size. It is Wide when the width exceeds
/// min_wide_mode_width(), else Tall when the height exceeds min_tall_mode_height(), else a single
/// pane chosen by pane_priority(). The two panes share the split axis by pane1_length() and
/// pane2_length(), like two Grid tracks. The pane not shown takes no part in layout, rendering,
/// focus or accessibility. There is no mode-changed event, so read mode() after layout. Panes are
/// not wrapped in a Scroll, so put one in a pane that needs to scroll. Spanning two screens is not
/// supported.
///
/// Example:
/// @code
/// auto  view = std::make_unique<ui::TwoPaneControl>();
/// auto* list = view->set_pane1(std::make_unique<ui::ListViewControl>());
/// view->set_pane2(std::make_unique<ui::TextControl>("Select an item"));
/// view->set_pane1_length(ui::GridLength::dip(320.0f));
/// view->set_min_wide_mode_width(500.0f);
/// view->set_tall_mode_configuration(ui::TwoPaneTallModeConfiguration::SinglePane);
/// window.set_content(std::move(view));
///
/// // Or declaratively:
/// window.set_content(ui::TwoPane{
///     .pane1_length        = ui::GridLength::dip(320.0f),
///     .min_wide_mode_width = 500.0f,
///     .pane1               = ui::Text{.text = "Master"},
///     .pane2               = ui::Text{.text = "Detail"},
/// });
/// @endcode
class TwoPaneControl final : public Element
{
public:
    /// An empty view: no panes, pane 1 Auto, pane 2 one fraction, both thresholds 641 DIPs.
    TwoPaneControl() = default;

    // ---- content ----
    /// Takes ownership of pane 1 and returns a non-owning observer, like
    /// ExpanderControl::set_content(). Destroys any previous pane 1. Null clears it. Lays the view
    /// out again.
    template <typename T>
    T* set_pane1(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "TwoPane panes must be Elements");
        T* observer = content.get();
        if (observer) // null clears
        {
            set_parent_of(*observer, this);
        }
        pane1_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// Takes ownership of pane 2 and returns a non-owning observer; see set_pane1().
    template <typename T>
    T* set_pane2(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "TwoPane panes must be Elements");
        T* observer = content.get();
        if (observer) // null clears
        {
            set_parent_of(*observer, this);
        }
        pane2_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// Pane 1, or null before set_pane1(). Owned by the view.
    [[nodiscard]] Element* pane1() const
    {
        return pane1_.get();
    }
    /// Pane 2, or null before set_pane2(). Owned by the view.
    [[nodiscard]] Element* pane2() const
    {
        return pane2_.get();
    }

    // ---- pane sizing ----
    /// Pane 1's track along the split axis, a GridLength as in a Grid track (DIPs, Auto or a
    /// fraction); Auto by default.
    [[nodiscard]] GridLength pane1_length() const
    {
        return pane1_length_;
    }
    /// Changes pane 1's track and lays the view out again. Only used while both panes are shown.
    void set_pane1_length(GridLength length)
    {
        pane1_length_ = length;
        mark_dirty(DirtyFlags::Layout);
    }
    /// Pane 2's track along the split axis; one fraction by default, so it takes the rest.
    [[nodiscard]] GridLength pane2_length() const
    {
        return pane2_length_;
    }
    /// Changes pane 2's track and lays the view out again.
    void set_pane2_length(GridLength length)
    {
        pane2_length_ = length;
        mark_dirty(DirtyFlags::Layout);
    }

    // ---- mode inputs ----
    /// The pane shown in SinglePane mode; Pane1 by default.
    [[nodiscard]] TwoPanePriority pane_priority() const
    {
        return pane_priority_;
    }
    /// Changes the single-pane choice and lays the view out again.
    void set_pane_priority(TwoPanePriority priority)
    {
        pane_priority_ = priority;
        mark_dirty(DirtyFlags::Layout);
    }

    /// How the panes are arranged in Wide mode; LeftRight by default.
    [[nodiscard]] TwoPaneWideModeConfiguration wide_mode_configuration() const
    {
        return wide_mode_configuration_;
    }
    /// Changes the Wide arrangement and lays the view out again.
    void set_wide_mode_configuration(TwoPaneWideModeConfiguration configuration)
    {
        wide_mode_configuration_ = configuration;
        mark_dirty(DirtyFlags::Layout);
    }

    /// How the panes are arranged in Tall mode; TopBottom by default.
    [[nodiscard]] TwoPaneTallModeConfiguration tall_mode_configuration() const
    {
        return tall_mode_configuration_;
    }
    /// Changes the Tall arrangement and lays the view out again.
    void set_tall_mode_configuration(TwoPaneTallModeConfiguration configuration)
    {
        tall_mode_configuration_ = configuration;
        mark_dirty(DirtyFlags::Layout);
    }

    /// The width threshold in DIPs; 641 by default. Wide needs more available width than this.
    /// An unbounded width never qualifies.
    [[nodiscard]] float min_wide_mode_width() const
    {
        return min_wide_mode_width_;
    }
    /// Sets the Wide threshold, clamped to >= 0, and lays the view out again.
    void set_min_wide_mode_width(float width)
    {
        min_wide_mode_width_ = (std::max)(0.0f, width);
        mark_dirty(DirtyFlags::Layout);
    }
    /// The height threshold in DIPs; 641 by default. Tall needs more available height than this.
    /// Only tested when Wide did not apply.
    [[nodiscard]] float min_tall_mode_height() const
    {
        return min_tall_mode_height_;
    }
    /// Sets the Tall threshold, clamped to >= 0, and lays the view out again.
    void set_min_tall_mode_height(float height)
    {
        min_tall_mode_height_ = (std::max)(0.0f, height);
        mark_dirty(DirtyFlags::Layout);
    }

    // ---- mode output ----
    /// The mode the most recent measure() pass picked; SinglePane before the first. No event
    /// reports a change. A caller that reacts to it reads it after layout, for example from its
    /// own tick_override().
    [[nodiscard]] TwoPaneMode mode() const;
    /// Only the panes the mode shows. A hidden pane takes no part in any walk over the tree, such
    /// as layout and rendering.
    void for_each_child(ChildVisitor visit) const override
    {
        if (pane1_visible() && pane1_)
        {
            visit(*pane1_);
        }
        if (pane2_visible() && pane2_)
        {
            visit(*pane2_);
        }
    }

protected:
    /// The view itself is not focusable. Tab reaches into the shown panes.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // The six-way arrangement: which side of which axis each pane sits on. The public
    // TwoPaneMode collapses it; measure_override() resolves it and the rest of the class reads it.
    enum class Configuration
    {
        Pane1Only,
        Pane2Only,
        LeftRight,
        RightLeft,
        TopBottom,
        BottomTop,
    };

    [[nodiscard]] bool pane1_visible() const;
    [[nodiscard]] bool pane2_visible() const;

    std::unique_ptr<Element> pane1_;
    std::unique_ptr<Element> pane2_;

    GridLength pane1_length_ = GridLength::auto_();
    GridLength pane2_length_ = GridLength::fraction();

    TwoPanePriority              pane_priority_           = TwoPanePriority::Pane1;
    TwoPaneWideModeConfiguration wide_mode_configuration_ = TwoPaneWideModeConfiguration::LeftRight;
    TwoPaneTallModeConfiguration tall_mode_configuration_ = TwoPaneTallModeConfiguration::TopBottom;
    float                        min_wide_mode_width_     = 641.0f;
    float                        min_tall_mode_height_    = 641.0f;

    mutable Configuration resolved_   = Configuration::Pane1Only;
    mutable float         pane1_size_ = 0.0f; // resolved pixel extent along the split axis;
    mutable float         pane2_size_ = 0.0f; // meaningless (and unused) outside Wide/Tall
};

/// Two regions that sit side by side, stacked or alone, depending on the space they get. Builds a
/// ui::TwoPaneControl. No event reports the mode. Read TwoPaneControl::mode() after layout.
///
/// Each measure picks the mode from the available size. It is Wide when the width exceeds
/// `min_wide_mode_width`, else Tall when the height exceeds `min_tall_mode_height`, else the single
/// pane `pane_priority` names.
///
/// Example:
/// @code
/// using ui::operator""_dip;
///
/// window.set_content(ui::TwoPane{
///     .pane1_length        = 320_dip,
///     .min_wide_mode_width = 500.0f,
///     .pane1               = ui::ListView<std::string>{.items = {"Inbox", "Sent", "Drafts"}},
///     .pane2               = ui::Text{.text = "Select an item"},
/// });
/// @endcode
struct TwoPane
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<TwoPane>` points at.
    using control_type = TwoPaneControl;

    // ---- properties ----
    /// Pane 1's share of the split axis (Auto by default).
    Binding<GridLength> pane1_length{GridLength::auto_()};
    /// Pane 2's share; one fraction by default, so it takes the rest.
    Binding<GridLength> pane2_length{GridLength::fraction()};
    /// The pane shown alone in SinglePane mode.
    Binding<TwoPanePriority> pane_priority{TwoPanePriority::Pane1};
    /// The Wide arrangement: LeftRight (the default), RightLeft, or SinglePane to never split.
    Binding<TwoPaneWideModeConfiguration> wide_mode_configuration{TwoPaneWideModeConfiguration::LeftRight};
    /// The Tall arrangement: TopBottom (the default), BottomTop, or SinglePane to never stack.
    Binding<TwoPaneTallModeConfiguration> tall_mode_configuration{TwoPaneTallModeConfiguration::TopBottom};
    /// The width threshold in DIPs. Wide needs more available width than this.
    Binding<float> min_wide_mode_width{641.0f};
    /// The height threshold in DIPs. Tall needs more available height than this.
    Binding<float> min_tall_mode_height{641.0f};

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
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
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
    ControlOut<TwoPane> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The first region: left or top by default, and the one shown alone unless `pane_priority`
    /// says otherwise.
    Spec pane1{};
    /// The second region.
    Spec pane2{};

    /// Consumes the spec and returns the built ui::TwoPaneControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
