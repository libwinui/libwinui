// SPDX-License-Identifier: MIT
// Reaching the window-only parts of an Element from a test that drives a control on its own.
#pragma once

#include "winui/ui/Element.hpp"

namespace ui::testing
{

/// What a test needs from an Element that only its window or container calls in an app.
struct ElementAccess
{
    /// Clears `flags` on `element` and its subtree, as the window does after drawing a frame.
    static void clear_dirty(Element& element, DirtyFlags flags = DirtyFlags::All)
    {
        element.clear_dirty(flags);
    }
    /// Runs the composition update the window runs after layout and input, against
    /// `compositor`, the test compositor.
    static void sync_composition(Element& element, detail::Compositor& compositor, const Theme& theme)
    {
        element.sync_composition(compositor, theme);
    }
    /// Shows `element` focused or not, as its window does when keyboard focus arrives or leaves.
    static void set_focused(Element& element, bool focused)
    {
        element.set_focused(focused);
    }
    /// Sets `element`'s focus-visible state, as its window does when the input modality changes.
    static void set_focus_visible(Element& element, bool visible)
    {
        element.set_focus_visible(visible);
    }
    /// The popup `element` has open (Element::open_popup()), or null.
    [[nodiscard]] static PopupRequest* popup_request(const Element& element)
    {
        return element.popup_request();
    }
    /// Dismisses `element`'s popup as the window does on a press outside it or Escape.
    static void dismiss_popup(Element& element, PopupCloseReason reason = PopupCloseReason::LightDismiss)
    {
        element.dismiss_popup(reason);
    }
    /// Runs `element`'s popup place callback as the window does; returns false without one.
    static bool place_popup(Element& element, Element& content, Size client, const MeasureContext& context)
    {
        return element.place_popup(content, client, context);
    }
    /// The elements the window asks for popups, tool tips, global shortcuts and focus
    /// requests, from `element` down.
    static void collect_live(Element& element, std::vector<Element*>& out)
    {
        element.collect_live(out);
    }
    /// Whether the compositor shows `element`'s layers instead of the frame.
    [[nodiscard]] static bool layers_composed(const Element& element)
    {
        return element.layers_composed();
    }
};

/// Clears `flags` on `element` and its subtree, as the window does after drawing a frame, so a
/// test can then check what a change marks dirty.
///
/// Example:
/// @code
/// ui::ButtonControl button{"Save"};
/// ui::testing::clear_dirty(button);
/// button.set_text("Save all");
/// REQUIRE(button.is_dirty(ui::DirtyFlags::Layout));
/// @endcode
inline void clear_dirty(Element& element, DirtyFlags flags = DirtyFlags::All)
{
    ElementAccess::clear_dirty(element, flags);
}

/// Shows `element` focused or not, as its window does when keyboard focus arrives or leaves, for
/// a test that drives a control on its own.
///
/// Example:
/// @code
/// ui::TextBoxControl box;
/// ui::testing::set_focused(box, true);
/// box.handle_key(ui::KeyEvent{.key = ui::Key::End});
/// @endcode
inline void set_focused(Element& element, bool focused)
{
    ElementAccess::set_focused(element, focused);
}

/// Sets `element`'s focus-visible state, as its window does when the input modality changes.
inline void set_focus_visible(Element& element, bool visible)
{
    ElementAccess::set_focus_visible(element, visible);
}

/// Draws `element` as its window draws the root of a tree, for a test that checks a focus ring
/// without a Window. It draws the element, then the focus ring of the focused, focus-visible
/// element inside it on top of everything.
///
/// Example:
/// @code
/// ui::ButtonControl button{"Save"};
/// // ... measure and arrange it ...
/// ui::testing::set_focused(button, true);
/// ui::testing::TestRenderer renderer;
/// ui::testing::render_with_focus_ring(button, renderer);
/// @endcode
inline void render_with_focus_ring(const Element& element, Renderer& renderer, const Theme& theme = Theme::default_light())
{
    const detail::FocusRingPass pass;
    element.render(renderer, theme);
    detail::render_focus_rings(renderer, element, theme, pass);
}

/// Whether `element` has a popup open (Element::open_popup()).
///
/// Example:
/// @code
/// ui::FlyoutControl flyout;
/// flyout.set_content(std::make_unique<ui::TextControl>("Saved"));
/// flyout.show();
/// REQUIRE(ui::testing::popup_open(flyout));
/// @endcode
[[nodiscard]] inline bool popup_open(const Element& element)
{
    return ElementAccess::popup_request(element) != nullptr;
}

/// The content of the popup `element` has open, or null when none is open.
[[nodiscard]] inline Element* popup_content(const Element& element)
{
    const PopupRequest* request = ElementAccess::popup_request(element);
    return request != nullptr ? request->content : nullptr;
}

/// The window rect the popup `element` has open hangs from: the request's anchor (the element by
/// default) or the rect inside it. An empty rect when none is open.
[[nodiscard]] inline Rect popup_anchor(const Element& element)
{
    const PopupRequest* request = ElementAccess::popup_request(element);
    if (request == nullptr)
    {
        return Rect{};
    }
    const Rect bounds = (request->anchor != nullptr ? request->anchor : &element)->bounds();
    if (!request->anchor_rect)
    {
        return bounds;
    }
    return Rect{bounds.x + request->anchor_rect->x,
                bounds.y + request->anchor_rect->y,
                request->anchor_rect->width,
                request->anchor_rect->height};
}

/// Dismisses the popup `element` has open, as its window does on a press outside it or Escape.
inline void dismiss_popup(Element& element)
{
    ElementAccess::dismiss_popup(element);
}

} // namespace ui::testing
