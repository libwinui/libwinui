// SPDX-License-Identifier: MIT
#include "ExitGhosts.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace ui::detail
{

namespace
{
// A hosted child window draws itself and is placed by the host, which only walks the live tree.
// It would stay where it was, unfaded, for as long as its element is kept.
bool hosts_native(const Element& element)
{
    if (element.automation_is_native_host())
    {
        return true;
    }
    std::vector<Element*> children;
    element.automation_children(children);
    for (const Element* child : children)
    {
        if (hosts_native(*child))
        {
            return true;
        }
    }
    return false;
}
} // namespace

void ExitGhostsDelete::operator()(ExitGhosts* ghosts) const noexcept
{
    delete ghosts;
}

ExitGhosts& ensure(ExitGhostsPtr& ghosts)
{
    if (!ghosts)
    {
        ghosts.reset(new ExitGhosts);
    }
    return *ghosts;
}

ExitGhosts::ExitGhosts()                                 = default;
ExitGhosts::~ExitGhosts()                                = default;
ExitGhosts::ExitGhosts(ExitGhosts&&) noexcept            = default;
ExitGhosts& ExitGhosts::operator=(ExitGhosts&&) noexcept = default;

void ExitGhosts::add(std::unique_ptr<Element> element, Point origin, TransitionStoryboard storyboard)
{
    if (!element || !animations_enabled() || hosts_native(*element))
    {
        detail::retire(std::move(element)); // no exit to play
        return;
    }
    Ghost ghost;
    ghost.offset     = Point{element->bounds().x - origin.x, element->bounds().y - origin.y};
    ghost.element    = std::move(element);
    ghost.storyboard = std::move(storyboard);
    ghosts_.push_back(std::move(ghost));
}

void ExitGhosts::render(Renderer& renderer, const Theme& theme, Point origin) const
{
    for (const Ghost& ghost : ghosts_)
    {
        const TransitionTargetState target = sample_transition(ghost.storyboard, ghost.elapsed);
        if (target.opacity <= 0.0f)
        {
            continue;
        }
        // The element is never arranged again, so it is drawn where it last was, moved by however
        // far the container has moved since.
        const Rect  was = ghost.element->bounds();
        const Point moved{origin.x + ghost.offset.x - was.x, origin.y + ghost.offset.y - was.y};
        renderer.push_transform(1.0f, moved);
        const Matrix transform   = transition_transform(target, was);
        const bool   transformed = !transform.is_identity();
        if (transformed)
        {
            renderer.push_transform(transform);
        }
        renderer.push_opacity_layer(target.opacity);
        ghost.element->render(renderer, theme);
        renderer.pop_opacity_layer();
        if (transformed)
        {
            renderer.pop_transform();
        }
        renderer.pop_transform();
    }
}

bool ExitGhosts::advance(Duration delta)
{
    if (ghosts_.empty())
    {
        return false;
    }
    for (Ghost& ghost : ghosts_)
    {
        ghost.elapsed += animation_delta(delta);
    }
    std::erase_if(ghosts_, [](const Ghost& ghost) { return ghost.elapsed >= ghost.storyboard.duration(); });
    return true;
}

} // namespace ui::detail
