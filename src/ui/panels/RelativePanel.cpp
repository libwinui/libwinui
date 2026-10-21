// SPDX-License-Identifier: MIT
#include "winui/ui/panels/RelativePanel.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

namespace
{

constexpr float RelativePanelInfinity = std::numeric_limits<float>::infinity();

} // namespace

// ---- attached properties ----

void RelativePanelControl::set_constraints(const Element* child, const RelativePanelConstraints& constraints)
{
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i].get() == child)
        {
            constraints_[i] = constraints;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
            return;
        }
    }
}

RelativePanelConstraints RelativePanelControl::constraints(const Element* child) const
{
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i].get() == child)
        {
            return constraints_[i];
        }
    }
    return {};
}

// ---- a node's anchoring rules ----

bool RelativePanelControl::Node::left_anchored() const
{
    return c->align_left_with_panel || align_left_with != nullptr ||
           (right_of != nullptr && align_horizontal_center_with == nullptr);
}

bool RelativePanelControl::Node::top_anchored() const
{
    return c->align_top_with_panel || align_top_with != nullptr ||
           (below != nullptr && align_vertical_center_with == nullptr);
}

bool RelativePanelControl::Node::right_anchored() const
{
    return c->align_right_with_panel || align_right_with != nullptr ||
           (left_of != nullptr && align_horizontal_center_with == nullptr);
}

bool RelativePanelControl::Node::bottom_anchored() const
{
    return c->align_bottom_with_panel || align_bottom_with != nullptr ||
           (above != nullptr && align_vertical_center_with == nullptr);
}

bool RelativePanelControl::Node::horizontal_center_anchored() const
{
    const bool no_edges = !c->align_left_with_panel && !c->align_right_with_panel && align_left_with == nullptr &&
                          align_right_with == nullptr;
    return (c->align_horizontal_center_with_panel && no_edges && left_of == nullptr && right_of == nullptr) ||
           (align_horizontal_center_with != nullptr && no_edges);
}

bool RelativePanelControl::Node::vertical_center_anchored() const
{
    const bool no_edges = !c->align_top_with_panel && !c->align_bottom_with_panel && align_top_with == nullptr &&
                          align_bottom_with == nullptr;
    return (c->align_vertical_center_with_panel && no_edges && above == nullptr && below == nullptr) ||
           (align_vertical_center_with != nullptr && no_edges);
}

// ---- the graph ----

void RelativePanelControl::generate_graph() const
{
    // One node per child, rebuilt every measure; a reference to
    // something that is not a child of this panel resolves to nothing.
    nodes_.clear();
    nodes_.resize(children_.size());
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        nodes_[i].element = children_[i].get();
        nodes_[i].c       = &constraints_[i];
    }
    auto find = [this](const Element* target) -> Node*
    {
        if (target == nullptr)
        {
            return nullptr;
        }
        for (Node& n : nodes_)
        {
            if (n.element == target)
            {
                return &n;
            }
        }
        return nullptr;
    };
    for (Node& n : nodes_)
    {
        const RelativePanelConstraints& c = *n.c;
        n.left_of                         = find(c.left_of);
        n.above                           = find(c.above);
        n.right_of                        = find(c.right_of);
        n.below                           = find(c.below);
        n.align_horizontal_center_with    = find(c.align_horizontal_center_with);
        n.align_vertical_center_with      = find(c.align_vertical_center_with);
        n.align_left_with                 = find(c.align_left_with);
        n.align_top_with                  = find(c.align_top_with);
        n.align_right_with                = find(c.align_right_with);
        n.align_bottom_with               = find(c.align_bottom_with);
    }
}

void RelativePanelControl::measure_rect_horizontally(const Node* node, Size available, float& x, float& width)
{
    // Start from the whole room and pull in each side
    // its constraints decide.
    bool centered_from_left  = false;
    bool centered_from_right = false;
    x                        = 0.0f;
    width                    = available.width;
    if (available.width == RelativePanelInfinity)
    {
        return;
    }
    if (!node->c->align_left_with_panel)
    {
        if (node->align_left_with != nullptr)
        {
            const float restricted = node->align_left_with->arrange_rect.x;
            x                      = restricted;
            width -= restricted;
        }
        else if (node->align_horizontal_center_with != nullptr)
        {
            centered_from_left = true;
        }
        else if (node->right_of != nullptr)
        {
            const float restricted = node->right_of->arrange_rect.x + node->right_of->arrange_rect.width;
            x                      = restricted;
            width -= restricted;
        }
    }
    if (!node->c->align_right_with_panel)
    {
        if (node->align_right_with != nullptr)
        {
            width -= available.width -
                     (node->align_right_with->arrange_rect.x + node->align_right_with->arrange_rect.width);
        }
        else if (node->align_horizontal_center_with != nullptr)
        {
            centered_from_right = true;
        }
        else if (node->left_of != nullptr)
        {
            width -= available.width - node->left_of->arrange_rect.x;
        }
    }
    if (centered_from_left && centered_from_right)
    {
        const Rect& n      = node->align_horizontal_center_with->arrange_rect;
        const float center = n.x + n.width / 2.0f;
        width              = std::min(center, available.width - center) * 2.0f;
        x                  = center - width / 2.0f;
    }
}

void RelativePanelControl::measure_rect_vertically(const Node* node, Size available, float& y, float& height)
{
    bool centered_from_top    = false;
    bool centered_from_bottom = false;
    y                         = 0.0f;
    height                    = available.height;
    if (available.height == RelativePanelInfinity)
    {
        return;
    }
    if (!node->c->align_top_with_panel)
    {
        if (node->align_top_with != nullptr)
        {
            const float restricted = node->align_top_with->arrange_rect.y;
            y                      = restricted;
            height -= restricted;
        }
        else if (node->align_vertical_center_with != nullptr)
        {
            centered_from_top = true;
        }
        else if (node->below != nullptr)
        {
            const float restricted = node->below->arrange_rect.y + node->below->arrange_rect.height;
            y                      = restricted;
            height -= restricted;
        }
    }
    if (!node->c->align_bottom_with_panel)
    {
        if (node->align_bottom_with != nullptr)
        {
            height -= available.height -
                      (node->align_bottom_with->arrange_rect.y + node->align_bottom_with->arrange_rect.height);
        }
        else if (node->align_vertical_center_with != nullptr)
        {
            centered_from_bottom = true;
        }
        else if (node->above != nullptr)
        {
            height -= available.height - node->above->arrange_rect.y;
        }
    }
    if (centered_from_top && centered_from_bottom)
    {
        const Rect& n      = node->align_vertical_center_with->arrange_rect;
        const float center = n.y + n.height / 2.0f;
        height             = std::min(center, available.height - center) * 2.0f;
        y                  = center - height / 2.0f;
    }
}

void RelativePanelControl::arrange_rect_horizontally(const Node* node, float& x, float& width)
{
    // Both edges anchored stretches across the measure
    // rect; one edge pins that side; a center centers; nothing pins the left.
    const Rect& m       = node->measure_rect;
    const float desired = std::min(m.width, node->desired.width);
    x                   = m.x;
    width               = desired;
    if (node->left_anchored())
    {
        width = node->right_anchored() ? m.width : desired;
    }
    else if (node->right_anchored())
    {
        x = m.x + m.width - desired;
    }
    else if (node->horizontal_center_anchored())
    {
        x = m.x + m.width / 2.0f - desired / 2.0f;
    }
}

void RelativePanelControl::arrange_rect_vertically(const Node* node, float& y, float& height)
{
    const Rect& m       = node->measure_rect;
    const float desired = std::min(m.height, node->desired.height);
    y                   = m.y;
    height              = desired;
    if (node->top_anchored())
    {
        height = node->bottom_anchored() ? m.height : desired;
    }
    else if (node->bottom_anchored())
    {
        y = m.y + m.height - desired;
    }
    else if (node->vertical_center_anchored())
    {
        y = m.y + m.height / 2.0f - desired / 2.0f;
    }
}

void RelativePanelControl::measure_node(Node* node, Size available, const MeasureContext& context) const
{
    if (node == nullptr || node->measured)
    {
        return;
    }
    // Dependencies first. A dependency still pending is a cycle closing;
    // that reference is dropped.
    node->pending = true;
    for (Node** dep :
         {&node->left_of,
          &node->above,
          &node->right_of,
          &node->below,
          &node->align_left_with,
          &node->align_top_with,
          &node->align_right_with,
          &node->align_bottom_with,
          &node->align_horizontal_center_with,
          &node->align_vertical_center_with})
    {
        if (*dep != nullptr && (*dep)->pending)
        {
            *dep = nullptr;
            continue;
        }
        measure_node(*dep, available, context);
    }
    node->pending = false;

    measure_rect_horizontally(node, available, node->measure_rect.x, node->measure_rect.width);
    measure_rect_vertically(node, available, node->measure_rect.y, node->measure_rect.height);
    const Size constrained{std::max(node->measure_rect.width, 0.0f), std::max(node->measure_rect.height, 0.0f)};
    node->desired  = node->element->measure(constrained, context);
    node->measured = true;

    // A pseudo-arrange, valid when the real arrange gets the same room; skipped against infinity.
    if (available.width != RelativePanelInfinity)
    {
        arrange_rect_horizontally(node, node->arrange_rect.x, node->arrange_rect.width);
        node->arranged_horizontally = true;
    }
    if (available.height != RelativePanelInfinity)
    {
        arrange_rect_vertically(node, node->arrange_rect.y, node->arrange_rect.height);
        node->arranged_vertically = true;
    }
}

void RelativePanelControl::arrange_node_horizontally(Node* node, Size final_size) const
{
    if (node == nullptr || node->arranged_horizontally)
    {
        return;
    }
    for (Node* dep :
         {node->left_of,
          node->above,
          node->right_of,
          node->below,
          node->align_left_with,
          node->align_top_with,
          node->align_right_with,
          node->align_bottom_with,
          node->align_horizontal_center_with,
          node->align_vertical_center_with})
    {
        arrange_node_horizontally(dep, final_size);
    }
    measure_rect_horizontally(node, final_size, node->measure_rect.x, node->measure_rect.width);
    arrange_rect_horizontally(node, node->arrange_rect.x, node->arrange_rect.width);
    node->arranged_horizontally = true;
}

void RelativePanelControl::arrange_node_vertically(Node* node, Size final_size) const
{
    if (node == nullptr || node->arranged_vertically)
    {
        return;
    }
    for (Node* dep :
         {node->left_of,
          node->above,
          node->right_of,
          node->below,
          node->align_left_with,
          node->align_top_with,
          node->align_right_with,
          node->align_bottom_with,
          node->align_horizontal_center_with,
          node->align_vertical_center_with})
    {
        arrange_node_vertically(dep, final_size);
    }
    measure_rect_vertically(node, final_size, node->measure_rect.y, node->measure_rect.height);
    arrange_rect_vertically(node, node->arrange_rect.y, node->arrange_rect.height);
    node->arranged_vertically = true;
}

// ---- desired size: a walk along each chain of dependencies ----

void RelativePanelControl::accumulate_positive_width(const Node* node, float x) const
{
    const float initial_x           = x;
    bool        centered_from_left  = false;
    bool        centered_from_right = false;
    x += node->desired.width;
    max_x_ = std::max(max_x_, x);
    if (node->c->align_left_with_panel)
    {
        if (!max_capped_)
        {
            max_x_      = x;
            max_capped_ = true;
        }
    }
    else if (node->align_left_with != nullptr)
    {
        if (node->align_left_with != node->align_right_with)
        {
            accumulate_negative_width(node->align_left_with, x);
        }
    }
    else if (node->align_horizontal_center_with != nullptr)
    {
        centered_from_left = true;
    }
    else if (node->right_of != nullptr)
    {
        accumulate_positive_width(node->right_of, x);
    }

    if (node->c->align_right_with_panel)
    {
        if (min_capped_)
        {
            min_x_ = std::min(min_x_, initial_x);
        }
        else
        {
            min_x_      = initial_x;
            min_capped_ = true;
        }
    }
    else if (node->align_right_with != nullptr)
    {
        accumulate_positive_width(node->align_right_with, initial_x);
    }
    else if (node->align_horizontal_center_with != nullptr)
    {
        centered_from_right = true;
    }
    else if (node->left_of != nullptr)
    {
        accumulate_negative_width(node->left_of, initial_x);
    }

    if (centered_from_left && centered_from_right)
    {
        const float center_x = x - node->desired.width / 2.0f;
        const float edge_x   = center_x - node->align_horizontal_center_with->desired.width / 2.0f;
        min_x_               = std::min(min_x_, edge_x);
        accumulate_positive_width(node->align_horizontal_center_with, edge_x);
    }
    else if (node->horizontal_center_anchored())
    {
        const float center_x = x - node->desired.width / 2.0f;
        const float upper    = max_x_ - center_x;
        const float lower    = center_x - min_x_;
        max_x_               = std::max(upper, lower) * 2.0f;
        min_x_               = 0.0f;
    }
}

void RelativePanelControl::accumulate_negative_width(const Node* node, float x) const
{
    const float initial_x           = x;
    bool        centered_from_left  = false;
    bool        centered_from_right = false;
    x -= node->desired.width;
    min_x_ = std::min(min_x_, x);
    if (node->c->align_right_with_panel)
    {
        if (!min_capped_)
        {
            min_x_      = x;
            min_capped_ = true;
        }
    }
    else if (node->align_right_with != nullptr)
    {
        if (node->align_right_with != node->align_left_with)
        {
            accumulate_positive_width(node->align_right_with, x);
        }
    }
    else if (node->align_horizontal_center_with != nullptr)
    {
        centered_from_right = true;
    }
    else if (node->left_of != nullptr)
    {
        accumulate_negative_width(node->left_of, x);
    }

    if (node->c->align_left_with_panel)
    {
        if (max_capped_)
        {
            max_x_ = std::max(max_x_, initial_x);
        }
        else
        {
            max_x_      = initial_x;
            max_capped_ = true;
        }
    }
    else if (node->align_left_with != nullptr)
    {
        accumulate_negative_width(node->align_left_with, initial_x);
    }
    else if (node->align_horizontal_center_with != nullptr)
    {
        centered_from_left = true;
    }
    else if (node->right_of != nullptr)
    {
        accumulate_positive_width(node->right_of, initial_x);
    }

    if (centered_from_left && centered_from_right)
    {
        const float center_x = x + node->desired.width / 2.0f;
        const float edge_x   = center_x + node->align_horizontal_center_with->desired.width / 2.0f;
        max_x_               = std::max(max_x_, edge_x);
        accumulate_negative_width(node->align_horizontal_center_with, edge_x);
    }
    else if (node->horizontal_center_anchored())
    {
        const float center_x = x + node->desired.width / 2.0f;
        const float upper    = max_x_ - center_x;
        const float lower    = center_x - min_x_;
        max_x_               = std::max(upper, lower) * 2.0f;
        min_x_               = 0.0f;
    }
}

void RelativePanelControl::accumulate_positive_height(const Node* node, float y) const
{
    const float initial_y            = y;
    bool        centered_from_top    = false;
    bool        centered_from_bottom = false;
    y += node->desired.height;
    max_y_ = std::max(max_y_, y);
    if (node->c->align_top_with_panel)
    {
        if (!max_capped_)
        {
            max_y_      = y;
            max_capped_ = true;
        }
    }
    else if (node->align_top_with != nullptr)
    {
        if (node->align_top_with != node->align_bottom_with)
        {
            accumulate_negative_height(node->align_top_with, y);
        }
    }
    else if (node->align_vertical_center_with != nullptr)
    {
        centered_from_top = true;
    }
    else if (node->below != nullptr)
    {
        accumulate_positive_height(node->below, y);
    }

    if (node->c->align_bottom_with_panel)
    {
        if (min_capped_)
        {
            min_y_ = std::min(min_y_, initial_y);
        }
        else
        {
            min_y_      = initial_y;
            min_capped_ = true;
        }
    }
    else if (node->align_bottom_with != nullptr)
    {
        accumulate_positive_height(node->align_bottom_with, initial_y);
    }
    else if (node->align_vertical_center_with != nullptr)
    {
        centered_from_bottom = true;
    }
    else if (node->above != nullptr)
    {
        accumulate_negative_height(node->above, initial_y);
    }

    if (centered_from_top && centered_from_bottom)
    {
        const float center_y = y - node->desired.height / 2.0f;
        const float edge_y   = center_y - node->align_vertical_center_with->desired.height / 2.0f;
        min_y_               = std::min(min_y_, edge_y);
        accumulate_positive_height(node->align_vertical_center_with, edge_y);
    }
    else if (node->vertical_center_anchored())
    {
        const float center_y = y - node->desired.height / 2.0f;
        const float upper    = max_y_ - center_y;
        const float lower    = center_y - min_y_;
        max_y_               = std::max(upper, lower) * 2.0f;
        min_y_               = 0.0f;
    }
}

void RelativePanelControl::accumulate_negative_height(const Node* node, float y) const
{
    const float initial_y            = y;
    bool        centered_from_top    = false;
    bool        centered_from_bottom = false;
    y -= node->desired.height;
    min_y_ = std::min(min_y_, y);
    if (node->c->align_bottom_with_panel)
    {
        if (!min_capped_)
        {
            min_y_      = y;
            min_capped_ = true;
        }
    }
    else if (node->align_bottom_with != nullptr)
    {
        if (node->align_bottom_with != node->align_top_with)
        {
            accumulate_positive_height(node->align_bottom_with, y);
        }
    }
    else if (node->align_vertical_center_with != nullptr)
    {
        centered_from_bottom = true;
    }
    else if (node->above != nullptr)
    {
        accumulate_negative_height(node->above, y);
    }

    if (node->c->align_top_with_panel)
    {
        if (max_capped_)
        {
            max_y_ = std::max(max_y_, initial_y);
        }
        else
        {
            max_y_      = initial_y;
            max_capped_ = true;
        }
    }
    else if (node->align_top_with != nullptr)
    {
        accumulate_negative_height(node->align_top_with, initial_y);
    }
    else if (node->align_vertical_center_with != nullptr)
    {
        centered_from_top = true;
    }
    else if (node->below != nullptr)
    {
        accumulate_positive_height(node->below, initial_y);
    }

    if (centered_from_top && centered_from_bottom)
    {
        const float center_y = y + node->desired.height / 2.0f;
        const float edge_y   = center_y + node->align_vertical_center_with->desired.height / 2.0f;
        max_y_               = std::max(max_y_, edge_y);
        accumulate_negative_height(node->align_vertical_center_with, edge_y);
    }
    else if (node->vertical_center_anchored())
    {
        const float center_y = y + node->desired.height / 2.0f;
        const float upper    = max_y_ - center_y;
        const float lower    = center_y - min_y_;
        max_y_               = std::max(upper, lower) * 2.0f;
        min_y_               = 0.0f;
    }
}

Size RelativePanelControl::calculate_desired_size() const
{
    // A node nothing depends on (on an axis) starts a chain.
    for (Node& n : nodes_)
    {
        n.horizontal_leaf = true;
        n.vertical_leaf   = true;
    }
    for (const Node& n : nodes_)
    {
        bool centered_from_left   = false;
        bool centered_from_right  = false;
        bool centered_from_top    = false;
        bool centered_from_bottom = false;
        if (!n.c->align_left_with_panel)
        {
            if (n.align_left_with != nullptr)
            {
                n.align_left_with->horizontal_leaf = false;
            }
            else if (n.align_horizontal_center_with != nullptr)
            {
                centered_from_left = true;
            }
            else if (n.right_of != nullptr)
            {
                n.right_of->horizontal_leaf = false;
            }
        }
        if (!n.c->align_top_with_panel)
        {
            if (n.align_top_with != nullptr)
            {
                n.align_top_with->vertical_leaf = false;
            }
            else if (n.align_vertical_center_with != nullptr)
            {
                centered_from_top = true;
            }
            else if (n.below != nullptr)
            {
                n.below->vertical_leaf = false;
            }
        }
        if (!n.c->align_right_with_panel)
        {
            if (n.align_right_with != nullptr)
            {
                n.align_right_with->horizontal_leaf = false;
            }
            else if (n.align_horizontal_center_with != nullptr)
            {
                centered_from_right = true;
            }
            else if (n.left_of != nullptr)
            {
                n.left_of->horizontal_leaf = false;
            }
        }
        if (!n.c->align_bottom_with_panel)
        {
            if (n.align_bottom_with != nullptr)
            {
                n.align_bottom_with->vertical_leaf = false;
            }
            else if (n.align_vertical_center_with != nullptr)
            {
                centered_from_bottom = true;
            }
            else if (n.above != nullptr)
            {
                n.above->vertical_leaf = false;
            }
        }
        if (centered_from_left && centered_from_right)
        {
            n.align_horizontal_center_with->horizontal_leaf = false;
        }
        if (centered_from_top && centered_from_bottom)
        {
            n.align_vertical_center_with->vertical_leaf = false;
        }
    }

    Size desired{};
    for (const Node& n : nodes_)
    {
        if (n.horizontal_leaf)
        {
            min_x_      = 0.0f;
            max_x_      = 0.0f;
            min_capped_ = false;
            max_capped_ = false;
            accumulate_positive_width(&n, 0.0f);
            desired.width = std::max(desired.width, max_x_ - min_x_);
        }
        if (n.vertical_leaf)
        {
            min_y_      = 0.0f;
            max_y_      = 0.0f;
            min_capped_ = false;
            max_capped_ = false;
            accumulate_positive_height(&n, 0.0f);
            desired.height = std::max(desired.height, max_y_ - min_y_);
        }
    }
    return desired;
}

// ---- Element ----

Size RelativePanelControl::measure_override(Size available, const MeasureContext& context) const
{
    // The room inside the border and padding (an unbounded axis
    // stays unbounded), every node measured, the chains measured for the desired size.
    const Thickness inset = content_inset();
    const Size      room{std::max(0.0f, available.width - inset.horizontal()),
                         std::max(0.0f, available.height - inset.vertical())};
    generate_graph();
    for (Node& n : nodes_)
    {
        measure_node(&n, room, context);
    }
    resolved_for_     = room;
    const Size chains = calculate_desired_size();
    return Size{chains.width + inset.horizontal(), chains.height + inset.vertical()};
}

void RelativePanelControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (nodes_.size() != children_.size())
    {
        return;
    }
    // Arrange re-resolves an axis only when the
    // room differs from what measure resolved against.
    const Thickness inset = content_inset();
    const Size      final_size{std::max(0.0f, final_rect.width - inset.horizontal()),
                               std::max(0.0f, final_rect.height - inset.vertical())};
    if (resolved_for_.width != final_size.width)
    {
        for (Node& n : nodes_)
        {
            n.arranged_horizontally = false;
        }
        for (Node& n : nodes_)
        {
            arrange_node_horizontally(&n, final_size);
        }
    }
    if (resolved_for_.height != final_size.height)
    {
        for (Node& n : nodes_)
        {
            n.arranged_vertically = false;
        }
        for (Node& n : nodes_)
        {
            arrange_node_vertically(&n, final_size);
        }
    }
    resolved_for_ = final_size;
    for (const Node& n : nodes_)
    {
        const float x = std::max(n.arrange_rect.x + inset.left, 0.0f);
        const float y = std::max(n.arrange_rect.y + inset.top, 0.0f);
        n.element->arrange(Rect{final_rect.x + x,
                                final_rect.y + y,
                                std::max(n.arrange_rect.width, 0.0f),
                                std::max(n.arrange_rect.height, 0.0f)});
    }
}

std::unique_ptr<ui::Element> RelativePanel::build() &&
{
    auto                      node = std::make_unique<ui::RelativePanelControl>();
    ui::RelativePanelControl* raw  = node.get();
    apply(background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    apply(border_brush,
          *node,
          [raw = node.get()](const ui::Paint& v)
          { ui::detail::set_paint(v, [raw](auto c) { raw->set_border_brush(c); }); });
    apply(border_thickness, *node, [raw = node.get()](const Thickness& v) { raw->set_border_thickness(v); });
    apply(corner_radius, *node, [raw = node.get()](const float& v) { raw->set_corner_radius(v); });
    apply(padding, *node, [raw = node.get()](const Thickness& v) { raw->set_padding(v); });

    // Every child first, then the names: a constraint may name a sibling declared after it.
    std::vector<std::pair<const RelativeItem*, ui::Element*>> placed;
    for (auto& item : content.items_)
    {
        if (item.node)
        {
            placed.emplace_back(&item.names, node->add(std::move(item.node)));
        }
    }
    auto find = [&placed](const std::string& name) -> const ui::Element*
    {
        if (name.empty())
        {
            return nullptr;
        }
        for (const auto& [names, element] : placed)
        {
            if (names->name == name)
            {
                return element;
            }
        }
        // A typo would otherwise leave the child unconstrained without a word.
        assert(false && "a RelativeItem names a sibling that is not in this RelativePanel");
        return nullptr;
    };
    for (const auto& [names, element] : placed)
    {
        ui::RelativePanelConstraints c{};
        c.left_of                            = find(names->left_of);
        c.above                              = find(names->above);
        c.right_of                           = find(names->right_of);
        c.below                              = find(names->below);
        c.align_horizontal_center_with       = find(names->align_horizontal_center_with);
        c.align_vertical_center_with         = find(names->align_vertical_center_with);
        c.align_left_with                    = find(names->align_left_with);
        c.align_top_with                     = find(names->align_top_with);
        c.align_right_with                   = find(names->align_right_with);
        c.align_bottom_with                  = find(names->align_bottom_with);
        c.align_left_with_panel              = names->align_left_with_panel;
        c.align_top_with_panel               = names->align_top_with_panel;
        c.align_right_with_panel             = names->align_right_with_panel;
        c.align_bottom_with_panel            = names->align_bottom_with_panel;
        c.align_horizontal_center_with_panel = names->align_horizontal_center_with_panel;
        c.align_vertical_center_with_panel   = names->align_vertical_center_with_panel;
        node->set_constraints(element, c);
    }
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
