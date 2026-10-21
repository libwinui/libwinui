// SPDX-License-Identifier: MIT
#include "winui/ui/panels/Panel.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

Element* Panel::adopt_child(std::unique_ptr<Element> child)
{
    Element* observer = child.get();
    if (observer == nullptr)
    {
        return nullptr;
    }
    set_parent_of(*observer, this);
    children_.push_back(std::move(child));
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return observer;
}

bool Panel::remove_child(Element* child)
{
    const auto it = std::ranges::find_if(children_,
                                         [child](const std::unique_ptr<Element>& c) { return c.get() == child; });
    if (it == children_.end())
    {
        return false;
    }
    std::unique_ptr<Element> owned = std::move(*it);
    children_.erase(it);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    // Not destroyed here: the child may be the one whose handler removed it.
    detail::retire(std::move(owned));
    return true;
}

std::unique_ptr<Element> Panel::orphan(const Element* child)
{
    const auto it = std::ranges::find_if(children_,
                                         [child](const std::unique_ptr<Element>& c) { return c.get() == child; });
    if (it == children_.end())
    {
        return nullptr;
    }
    std::unique_ptr<Element> owned = std::move(*it);
    children_.erase(it);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return detail::orphan(std::move(owned));
}

void Panel::clear_children()
{
    if (children_.empty())
    {
        return;
    }
    std::vector<std::unique_ptr<Element>> gone = std::move(children_);
    children_.clear();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    for (std::unique_ptr<Element>& child : gone)
    {
        detail::retire(std::move(child));
    }
}

std::vector<Element*> Panel::render_order() const
{
    std::vector<Element*> order;
    order.reserve(children_.size());
    for (const auto& child : children_)
    {
        order.push_back(child.get());
    }
    return order;
}

void Panel::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    draw_border(renderer, bounds(), background_.resolve(theme), border_brush_.resolve(theme), border_thickness_, corner_radius_);
    for (Element* child : render_order())
    {
        child->render(renderer, context);
    }
}

void Panel::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    const std::vector<Element*> order = render_order();
    out.assign(order.rbegin(), order.rend());
}

void Panel::for_each_child(ChildVisitor visit) const
{
    for (std::size_t i = 0; i < children_.size(); ++i) // by index: a handler may add a child
    {
        visit(*children_[i]);
    }
}

void Panel::tick_override(Duration delta)
{
    if (background_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
}

void Panel::automation_children(std::vector<Element*>& out) const
{
    for (Element* child : render_order())
    {
        out.push_back(child);
    }
}

} // namespace ui
