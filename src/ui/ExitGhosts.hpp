// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"

#include <memory>
#include <vector>

namespace ui
{

class Element;
class Renderer;
struct Theme;

namespace detail
{

// What a container shows of the children it no longer has. A child that leaves with a transition
// is handed over here: from that moment it is out of the container's layout, input, tab ring and
// automation tree, and is kept alive only to be drawn through its exit, then destroyed.
class ExitGhosts
{
public:
    ExitGhosts();
    ~ExitGhosts();
    ExitGhosts(ExitGhosts&&) noexcept;
    ExitGhosts& operator=(ExitGhosts&&) noexcept;

    // Takes `element` where it is now, to leave with `storyboard`. `origin` is the container's own
    // position; the ghost follows the container if that moves while it plays. An element with a
    // native control anywhere inside is destroyed at once instead: its child window is not drawn
    // through the renderer, so it could not fade.
    void add(std::unique_ptr<Element> element, Point origin, TransitionStoryboard storyboard);

    // Draws every ghost at its current step. `origin` is the container's position now.
    void render(Renderer& renderer, const Theme& theme, Point origin) const;

    // Steps every ghost and destroys the finished ones. True when there was anything to step,
    // which is when the container has to be drawn again.
    bool advance(Duration delta);

    [[nodiscard]] bool empty() const
    {
        return ghosts_.empty();
    }

private:
    struct Ghost
    {
        std::unique_ptr<Element> element;
        Point                    offset{}; // container-relative position when it went
        TransitionStoryboard     storyboard;
        Duration                 elapsed{};
    };
    std::vector<Ghost> ghosts_;
};

// The container's ghosts, created on the first exit.
ExitGhosts& ensure(ExitGhostsPtr& ghosts);

} // namespace detail

} // namespace ui
