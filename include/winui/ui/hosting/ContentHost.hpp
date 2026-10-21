// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"

#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ui
{

/// A node that owns exactly one child and can replace it at runtime. Layout, rendering, input,
/// focus and animation all pass through to the child, so wrapping a subtree in one changes
/// nothing visible about it.
///
/// It has two uses. The first is a swappable slot for UI whose structure varies (a wizard page, a
/// master/detail pane): `ui::Match` builds one and rebuilds its child when a Property changes. The
/// second is a base class for a control made of one child plus its own chrome (a card, a padded
/// group). Unlike most controls it is not `final`: override measure_override(),
/// arrange_override() and render_override(), and call the ContentHost version for the child.
/// Border and DensityScope derive from it.
///
/// Example:
/// @code
/// // A reusable card: padding plus a rounded background, the child forwarded.
/// class Card final : public ui::ContentHost
/// {
/// public:
///     explicit Card(std::unique_ptr<ui::Element> child)
///     {
///         set_child(std::move(child));
///     }
///
/// protected:
///     ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
///     {
///         const ui::Size inner = ui::ContentHost::measure_override(
///             ui::Size{std::max(0.0f, available.width - 32.0f), std::max(0.0f, available.height - 32.0f)},
///             context);
///         return ui::Size{inner.width + 32.0f, inner.height + 32.0f};
///     }
///     void arrange_override(const ui::Rect& final_rect, const ui::ArrangeContext& context) override
///     {
///         ui::ContentHost::arrange_override(final_rect.inset(ui::Thickness{16.0f}), context);
///     }
///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
///     {
///         const ui::Theme& theme = context.theme;
///         renderer.fill_rounded_rect(bounds(), 8.0f, theme.colors().background);
///         ui::ContentHost::render_override(renderer, context); // then the child
///     }
/// };
///
/// // As a runtime-swappable slot:
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::ContentHost* slot = stack->add(std::make_unique<ui::ContentHost>());
/// slot->set_child(std::make_unique<Card>(std::make_unique<ui::TextControl>("Step 1")));
/// ui::ButtonControl* next = stack->add(std::make_unique<ui::ButtonControl>("Next"));
/// next->on_click([slot] { slot->set_child(std::make_unique<ui::TextControl>("Step 2")); });
/// window.set_content(std::move(stack));
/// @endcode
class ContentHost : public Element
{
public:
    /// An empty host; it measures to nothing until set_child().
    ContentHost() = default;

    /// Takes ownership of the child and returns a non-owning observer, the same convention as
    /// StackControl::add(). Replaces and destroys the previous child, then updates the layout and
    /// redraws. A null `child` leaves the host empty, like clear_child().
    ///
    /// With a ContentTransition among the children_transitions() the change is animated:
    /// the new child slides and fades in while the old one fades out over it. The old child is
    /// out of layout, input and the tab ring at once, and destroyed when its exit has played.
    ///
    /// Safe to call from a handler of the child being replaced: while a Window is dispatching,
    /// the old child is destroyed only once that dispatch has returned.
    template <typename T>
    T* set_child(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "ContentHost content must be an Element");
        T* observer = child.get();
        if (observer)
        {
            set_parent_of(*observer, this);
        }
        retire_child();
        child_ = std::move(child);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }

    /// Destroys the current child, leaving the host empty (it then measures to nothing).
    /// Updates the layout and redraws.
    void clear_child()
    {
        retire_child();
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Takes the child out alive and hands it back, leaving the host empty, for adding it
    /// somewhere else. Focus inside it is cleared and it plays no exit transition. Null when the
    /// host is empty. Updates the layout and redraws.
    [[nodiscard]] std::unique_ptr<Element> orphan_child()
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return detail::orphan(std::move(child_));
    }

    /// The current child as a non-owning observer, or nullptr when empty.
    [[nodiscard]] Element* child() const
    {
        return child_.get();
    }
    /// True while the host has no child, or its child is not Visible: the pointer then reaches what
    /// is behind the host. A host that draws something of its own (a background, a header)
    /// overrides this to return false.
    [[nodiscard]] bool hit_test_transparent() const override
    {
        return child_ == nullptr || child_->visibility() != Visibility::Visible;
    }
    /// Lists the child, if any.
    void for_each_child(ChildVisitor visit) const override
    {
        if (child_)
        {
            visit(*child_);
        }
    }

protected:
    /// The child's desired size, or zero when empty. A subclass adding chrome calls this with
    /// the space left over, then adds its own.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Gives the child the whole of `final_rect`; a subclass passes the inset rect instead.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Renders the child; a subclass draws its chrome first, then calls this.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the exit animation of a replaced child.
    void tick_override(Duration delta) override;


private:
    // Sees the child that is about to go out, when a ContentTransition asks for it.
    void retire_child();

    std::unique_ptr<Element> child_;
    detail::ExitGhostsPtr    ghosts_;
};

} // namespace ui
