// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ui
{

/// The shared base of the layout panels that own a list of children:
/// `Canvas`, `WrapPanel`, `TilePanel`, `RelativePanel` and `LayoutPanel`.
///
/// A panel owns its children by `std::unique_ptr` and hands callers back non-owning observers. It
/// draws an optional background behind the children, plus a border with corner radius and padding
/// on the panels that publish them, and lists the children for every tree-wide walk. Children draw
/// in `render_order()`, which is insertion order unless the panel overrides it. The window
/// hit-tests them topmost first in that order, so a press reaches only the topmost child under it.
/// `orphan()`
/// takes a child back out alive. `Panel` cannot be constructed directly; derive from it and
/// implement `Element::measure_override()` / `arrange_override()`, or use one of the panels above.
///
/// Example:
/// @code
/// auto canvas = std::make_unique<ui::CanvasControl>();
/// canvas->set_background([](const ui::Theme& t) { return t.colors().background; });
/// canvas->set_background_transition(ui::BrushTransition{}); // blend on theme swaps
/// ui::ButtonControl* ok = canvas->add(std::make_unique<ui::ButtonControl>("OK"), 24.0f, 16.0f);
/// ok->on_click([] { /* ... */ });
/// for (const auto& child : canvas->children()) // insertion order
///     child->set_margin(ui::Thickness{4.0f});
/// window.set_content(std::move(canvas)); // the window takes ownership of the panel and its children
/// @endcode
class Panel : public Element
{
public:
    /// The owned children in insertion order (not necessarily drawing order, see
    /// `render_order()`). The panel keeps ownership; the pointers stay valid for its lifetime.
    [[nodiscard]] std::span<const std::unique_ptr<Element>> children() const
    {
        return children_;
    }

    /// The background, filled behind the children; nullopt (the default) draws nothing.
    /// Returns nullopt as well while a ThemeColor resource is set (see `background_resource()`).
    [[nodiscard]] const std::optional<Color>& background() const
    {
        return background_.color();
    }
    /// The theme resource the background is drawn from, or null when a fixed color (or
    /// nothing) is set. A ThemeColor is re-read whenever the panel is drawn, so it follows
    /// theme changes without a setter call (ui/BackgroundBrush.hpp).
    [[nodiscard]] ThemeColor background_resource() const
    {
        return background_.resource();
    }
    /// Fixed background color; replaces any resource. Marks the panel for redraw.
    void set_background(std::optional<Color> color)
    {
        background_.set(color);
        mark_dirty(DirtyFlags::Visual);
    }
    /// Theme-resource background, e.g. `[](const Theme& t) { return t.colors().background; }`;
    /// replaces any fixed color. Marks the panel for redraw.
    void set_background(ThemeColor resource)
    {
        background_.set(resource);
        mark_dirty(DirtyFlags::Visual);
    }
    /// With a transition set, a later change of what the background draws (a new color, or a
    /// resource under a new theme) blends from the color on screen (Animation.hpp's
    /// BrushTransition). nullopt (the default) changes it at once.
    [[nodiscard]] const std::optional<BrushTransition>& background_transition() const
    {
        return background_.transition();
    }
    /// Enables (or, with nullopt, disables) the background blend; takes effect on the next change.
    void set_background_transition(std::optional<BrushTransition> transition)
    {
        background_.set_transition(transition);
    }
    /// Takes `child` out of the panel alive and hands it back, for adding somewhere else. Focus
    /// inside it is cleared. Null when it is not one of this panel's children.
    [[nodiscard]] std::unique_ptr<Element> orphan(const Element* child);

    /// Lists the children in insertion order, the order Tab visits them.
    void for_each_child(ChildVisitor visit) const override;
    /// The children in `render_order()`, not insertion order, so screen readers see them as they
    /// are stacked on screen (a Canvas's z-index), and hit_region() finds the topmost one.
    void automation_children(std::vector<Element*>& out) const override;

protected:
    /// Only derived panels construct a Panel; it starts empty, with no background or border.
    Panel() = default;

    /// Takes ownership of `child`, parents it and appends it. Returns the observer.
    Element* adopt_child(std::unique_ptr<Element> child);
    /// Removes `child`, an observer adopt_child() returned, and lets it go. It is destroyed once
    /// the input or tick being handled has returned, since it may be the one whose handler asked
    /// for this. False when `child` is not one of this panel's children.
    bool remove_child(Element* child);
    /// Removes and lets go of every child, as remove_child() does each.
    void clear_children();

    /// The children in drawing order, bottom first. Pointer input is hit-tested in the reverse,
    /// topmost first.
    [[nodiscard]] virtual std::vector<Element*> render_order() const;

    // ---- border and padding, published by the panels that have them ----
    /// The outline color; nullopt (the default) draws no border even with a non-zero
    /// `border_thickness()`. Nullopt as well while a ThemeColor is set.
    [[nodiscard]] std::optional<Color> border_brush() const
    {
        return border_brush_.color();
    }
    /// The theme resource the border is drawn from, or null.
    [[nodiscard]] ThemeColor border_brush_resource() const
    {
        return border_brush_.resource();
    }
    /// A fixed outline color; replaces any resource. Marks the panel for redraw.
    void set_border_brush(std::optional<Color> color)
    {
        border_brush_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// An outline color read from the theme each time the panel is drawn; replaces any fixed
    /// color. Marks the panel for redraw.
    void set_border_brush(ThemeColor resource)
    {
        border_brush_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border width in DIPs per side (default zero); it insets the children like padding.
    [[nodiscard]] const Thickness& border_thickness() const
    {
        return border_thickness_;
    }
    /// Sets the border width; marks the panel for layout and redraw.
    void set_border_thickness(const Thickness& thickness)
    {
        border_thickness_ = thickness;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The corner radius in DIPs, uniform for all four corners (default 0, square).
    [[nodiscard]] float corner_radius() const
    {
        return corner_radius_;
    }
    /// Sets the corner radius; marks the panel for redraw.
    void set_corner_radius(float radius)
    {
        corner_radius_ = radius;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The padding in DIPs between the border and the children (default zero).
    [[nodiscard]] const Thickness& padding() const
    {
        return padding_;
    }
    /// Sets the padding; marks the panel for layout.
    void set_padding(const Thickness& padding)
    {
        padding_ = padding;
        mark_dirty(DirtyFlags::Layout);
    }

    /// Border plus padding, the inset taken off every side.
    [[nodiscard]] Thickness content_inset() const
    {
        return Edges{.left   = border_thickness_.left + padding_.left,
                     .top    = border_thickness_.top + padding_.top,
                     .right  = border_thickness_.right + padding_.right,
                     .bottom = border_thickness_.bottom + padding_.bottom};
    }

    /// The owned children in insertion order; derived panels read it in measure/arrange, append
    /// only through `adopt_child()` and remove only through `remove_child()`/`clear_children()`.
    std::vector<std::unique_ptr<Element>> children_;

    // ---- Element ----
    /// Draws the background and border, then every child in `render_order()`.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// The children in `render_order()`, topmost first.
    void hit_test_children(Point p, std::vector<Element*>& out) const override;
    /// Advances the background transition, then ticks every child.
    void tick_override(Duration delta) override;


private:
    BackgroundBrush background_;
    Paint           border_brush_;
    Thickness       border_thickness_{};
    float           corner_radius_ = 0.0f;
    Thickness       padding_{};
};

} // namespace ui
