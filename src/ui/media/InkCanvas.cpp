// SPDX-License-Identifier: MIT
#include "winui/ui/media/InkCanvas.hpp"

#include "InkRender.hpp"
#include "InkStencils.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui
{

namespace
{
constexpr float InkMinPointSpacing = 0.5f; // DIPs a move must cover to add a point
constexpr float InkEraserReach     = 4.0f; // how far past a stroke's edge the stroke eraser still takes it
constexpr Color InkLassoColor      = Color::from_rgba(32, 32, 32);

// The lasso outline: dark dashes over a white line, so it shows on paper and on ink alike.
void draw_lasso_outline(Renderer& renderer, std::span<const Point> outline)
{
    PathFigure figure{.start = outline.front(), .segments = {}, .filled = false};
    for (const Point& p : outline.subspan(1))
    {
        figure.segments.push_back({.p1 = p});
    }
    DrawGeometry line{.geometry     = PathGeometry{.figures = {std::move(figure)}},
                      .stroke       = Brush{colors::White},
                      .stroke_style = StrokeStyle{.thickness = 1.0f, .line_join = PenLineJoin::Round}};
    renderer.draw_geometry(line);
    line.stroke                  = Brush{InkLassoColor};
    line.stroke_style.dash_array = {4.0f, 3.0f};
    renderer.draw_geometry(line);
}
} // namespace

// ---- InkStroke ----

Rect InkStroke::bounding_rect() const
{
    if (points.empty())
    {
        return Rect{};
    }
    float left   = points.front().position.x;
    float top    = points.front().position.y;
    float right  = left;
    float bottom = top;
    float reach  = 0.0f;
    for (const InkPoint& p : points)
    {
        left   = std::min(left, p.position.x);
        top    = std::min(top, p.position.y);
        right  = std::max(right, p.position.x);
        bottom = std::max(bottom, p.position.y);
        reach  = std::max(reach, detail::ink_pressure_scale(attributes, p.pressure));
    }
    const float hw = attributes.size.width * 0.5f * reach;
    const float hh = attributes.size.height * 0.5f * reach;
    return Rect{left - hw, top - hh, right - left + 2.0f * hw, bottom - top + 2.0f * hh};
}

// ---- InkStrokeContainer ----

const InkStroke* InkStrokeContainer::stroke_by_id(std::uint32_t id) const
{
    const auto it = std::ranges::find(strokes_, id, &InkStroke::id);
    return it == strokes_.end() ? nullptr : &*it;
}

InkStroke& InkStrokeContainer::push(InkStroke stroke)
{
    stroke.id = next_id_++;
    strokes_.push_back(std::move(stroke));
    return strokes_.back();
}

std::size_t InkStrokeContainer::erase_where_id(std::uint32_t id)
{
    return std::erase_if(strokes_, [id](const InkStroke& s) { return s.id == id; });
}

void InkStrokeContainer::changed()
{
    ++version_;
    if (on_changed_)
    {
        on_changed_();
    }
}

std::uint32_t InkStrokeContainer::add_stroke(InkStroke stroke)
{
    if (stroke.points.empty())
    {
        return 0;
    }
    const std::uint32_t id = push(std::move(stroke)).id;
    changed();
    return id;
}

void InkStrokeContainer::add_strokes(std::vector<InkStroke> strokes)
{
    bool any = false;
    for (InkStroke& s : strokes)
    {
        if (!s.points.empty())
        {
            push(std::move(s));
            any = true;
        }
    }
    if (any)
    {
        changed();
    }
}

bool InkStrokeContainer::remove_stroke(std::uint32_t id)
{
    if (id == 0 || erase_where_id(id) == 0)
    {
        return false;
    }
    changed();
    return true;
}

void InkStrokeContainer::clear()
{
    if (strokes_.empty())
    {
        return;
    }
    strokes_.clear();
    changed();
}

void InkStrokeContainer::replace(std::vector<InkStroke> strokes)
{
    strokes_.clear();
    for (InkStroke& s : strokes)
    {
        if (!s.points.empty())
        {
            push(std::move(s));
        }
    }
    changed();
}

Rect InkStrokeContainer::bounding_rect() const
{
    std::optional<Rect> total;
    for (const InkStroke& s : strokes_)
    {
        const Rect r = s.bounding_rect();
        if (!total)
        {
            total = r;
            continue;
        }
        const float left   = std::min(total->left(), r.left());
        const float top    = std::min(total->top(), r.top());
        const float right  = std::max(total->right(), r.right());
        const float bottom = std::max(total->bottom(), r.bottom());
        total              = Rect{left, top, right - left, bottom - top};
    }
    return total.value_or(Rect{});
}

// ---- selection ----

namespace
{
// The union of the bounding rects of the strokes `pick` accepts; empty for none.
template <typename Pick>
Rect ink_union(std::span<const InkStroke> strokes, Pick pick)
{
    std::optional<Rect> total;
    for (const InkStroke& s : strokes)
    {
        if (!pick(s))
        {
            continue;
        }
        const Rect r = s.bounding_rect();
        if (!total)
        {
            total = r;
            continue;
        }
        const float left   = std::min(total->left(), r.left());
        const float top    = std::min(total->top(), r.top());
        const float right  = std::max(total->right(), r.right());
        const float bottom = std::max(total->bottom(), r.bottom());
        total              = Rect{left, top, right - left, bottom - top};
    }
    return total.value_or(Rect{});
}
} // namespace

Rect InkStrokeContainer::select_with_polyline(std::span<const Point> points)
{
    for (InkStroke& s : strokes_)
    {
        s.selected = points.size() >= 3 &&
                     std::ranges::all_of(s.points,
                                         [points](const InkPoint& p)
                                         { return detail::ink_point_in_polygon(p.position, points); });
    }
    changed();
    return selection_bounding_rect();
}

Rect InkStrokeContainer::select_with_line(Point from, Point to)
{
    for (InkStroke& s : strokes_)
    {
        s.selected = detail::ink_stroke_touches(s, from, to, 0.0f);
    }
    changed();
    return selection_bounding_rect();
}

bool InkStrokeContainer::set_selected(std::uint32_t id, bool selected)
{
    const auto it = std::ranges::find(strokes_, id, &InkStroke::id);
    if (id == 0 || it == strokes_.end())
    {
        return false;
    }
    if (it->selected != selected)
    {
        it->selected = selected;
        changed();
    }
    return true;
}

void InkStrokeContainer::clear_selection()
{
    if (!has_selection())
    {
        return;
    }
    for (InkStroke& s : strokes_)
    {
        s.selected = false;
    }
    changed();
}

bool InkStrokeContainer::has_selection() const
{
    return std::ranges::any_of(strokes_, &InkStroke::selected);
}

Rect InkStrokeContainer::selection_bounding_rect() const
{
    return ink_union(strokes_, [](const InkStroke& s) { return s.selected; });
}

Rect InkStrokeContainer::move_selected(Point translation)
{
    if (!has_selection())
    {
        return Rect{};
    }
    for (InkStroke& s : strokes_)
    {
        if (s.selected)
        {
            for (InkPoint& p : s.points)
            {
                p.position = Point{p.position.x + translation.x, p.position.y + translation.y};
            }
        }
    }
    changed();
    return selection_bounding_rect();
}

Rect InkStrokeContainer::delete_selected()
{
    const Rect covered = selection_bounding_rect();
    if (std::erase_if(strokes_, [](const InkStroke& s) { return s.selected; }) == 0)
    {
        return Rect{};
    }
    changed();
    return covered;
}

// ---- InkCanvasControl ----

InkCanvasControl::InkCanvasControl()
{
    strokes_.on_changed_ = [this]
    {
        mark_dirty(DirtyFlags::Visual);
        if (on_strokes_changed_)
        {
            on_strokes_changed_();
        }
    };
}

InkCanvasControl::~InkCanvasControl() = default;

void InkCanvasControl::set_drawing_attributes(const InkDrawingAttributes& attributes)
{
    attributes_ = attributes;
}

void InkCanvasControl::set_input_mode(InkCanvasInputMode mode)
{
    if (mode_ == mode)
    {
        return;
    }
    commit_wet();
    erasing_ = false;
    mode_    = mode;
}

void InkCanvasControl::enabled_changed()
{
    commit_wet();
    erasing_ = false;
}

void InkCanvasControl::set_ruler(const InkRuler& ruler)
{
    if (ruler_ == ruler)
    {
        return;
    }
    ruler_ = ruler;
    mark_dirty(DirtyFlags::Visual);
}

void InkCanvasControl::set_lasso_outline(std::vector<Point> outline)
{
    if (lasso_.empty() && outline.empty())
    {
        return;
    }
    lasso_ = std::move(outline);
    mark_dirty(DirtyFlags::Visual);
}

void InkCanvasControl::set_protractor(const InkProtractor& protractor)
{
    if (protractor_ == protractor)
    {
        return;
    }
    protractor_ = protractor;
    mark_dirty(DirtyFlags::Visual);
}

Point InkCanvasControl::ruler_center() const
{
    return ruler_.center.value_or(Point{bounds().width * 0.5f, bounds().height * 0.5f});
}

Point InkCanvasControl::protractor_center() const
{
    return protractor_.center.value_or(Point{bounds().width * 0.5f, bounds().height * 0.5f});
}

InkPoint InkCanvasControl::snapped(InkPoint p) const
{
    if (snap_ == 0)
    {
        return p;
    }
    const detail::InkStencilTarget target{.ruler = snap_ != 2, .hit = detail::InkStencilHit::Edge, .side = snap_};
    const float half_nib = (std::max)(wet_ ? wet_->attributes.size.width : attributes_.size.width,
                                      wet_ ? wet_->attributes.size.height : attributes_.size.height) *
                           0.5f;
    p.position = detail::ink_stencil_snap(p.position, target, ruler_, ruler_center(), protractor_, protractor_center(), half_nib);
    return p;
}

bool InkCanvasControl::wheel_override(const PointerEvent& e)
{
    if (!is_enabled() || e.horizontal_wheel || !bounds().contains(e.position))
    {
        return false;
    }
    const detail::InkStencilTarget
        target = detail::ink_stencil_hit(to_local(e).position, ruler_, ruler_center(), protractor_, protractor_center());
    if (target.hit == detail::InkStencilHit::None)
    {
        return false;
    }
    // A degree for every 8 DIPs of wheel turn: about 6 degrees a notch.
    float& angle = target.ruler ? ruler_.angle : protractor_.angle;
    angle        = std::fmod(angle + e.wheel_delta / 8.0f, 360.0f);
    mark_dirty(DirtyFlags::Visual);
    if (on_stencils_changed_)
    {
        on_stencils_changed_();
    }
    return true;
}

InkPoint InkCanvasControl::to_local(const PointerEvent& e) const
{
    return InkPoint{Point{e.position.x - bounds().x, e.position.y - bounds().y}, std::clamp(e.pressure, 0.0f, 1.0f)};
}

void InkCanvasControl::commit_wet()
{
    if (!wet_)
    {
        return;
    }
    InkStroke stroke = std::move(*wet_);
    wet_.reset();
    mark_dirty(DirtyFlags::Visual);
    const InkStroke collected = strokes_.push(std::move(stroke));
    strokes_.changed();
    if (on_strokes_collected_)
    {
        on_strokes_collected_(std::span<const InkStroke>{&collected, 1});
    }
}

void InkCanvasControl::erase_along(Point from, Point to)
{
    if (erase_mode_ == InkCanvasEraseMode::Point)
    {
        // Each cut stroke gives way to what is left of it, in its place in the drawing order.
        std::vector<InkStroke> erased;
        std::vector<InkStroke> next;
        next.reserve(strokes_.strokes_.size());
        for (InkStroke& s : strokes_.strokes_)
        {
            std::optional<std::vector<InkStroke>> left = detail::ink_stroke_cut(s, from, to, eraser_size_ * 0.5f);
            if (!left)
            {
                next.push_back(std::move(s));
                continue;
            }
            for (InkStroke& piece : *left)
            {
                piece.id = strokes_.next_id_++;
                next.push_back(std::move(piece));
            }
            erased.push_back(std::move(s));
        }
        strokes_.strokes_ = std::move(next);
        if (erased.empty())
        {
            return;
        }
        strokes_.changed();
        if (on_strokes_erased_)
        {
            on_strokes_erased_(erased);
        }
        return;
    }

    std::vector<InkStroke> erased;
    for (const InkStroke& s : strokes_.strokes_)
    {
        if (detail::ink_stroke_touches(s, from, to, InkEraserReach))
        {
            erased.push_back(s);
        }
    }
    if (erased.empty())
    {
        return;
    }
    for (const InkStroke& s : erased)
    {
        strokes_.erase_where_id(s.id);
    }
    strokes_.changed();
    if (on_strokes_erased_)
    {
        on_strokes_erased_(erased);
    }
}

Size InkCanvasControl::measure_override(Size /*available*/, const MeasureContext&) const
{
    return Size{};
}

void InkCanvasControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (strokes_.empty() && !wet_ && !ruler_.visible && !protractor_.visible && lasso_.size() < 2)
    {
        return;
    }
    if (dry_version_ != strokes_.version_)
    {
        dry_cache_.clear();
        // Highlighters first, so the pen ink lands on top of them.
        for (const bool highlighters : {true, false})
        {
            for (const InkStroke& s : strokes_.strokes_)
            {
                if (s.attributes.draw_as_highlighter != highlighters)
                {
                    continue;
                }
                if (s.selected)
                {
                    for (DrawGeometry& d : detail::ink_selected_drawing(s))
                    {
                        dry_cache_.push_back(std::move(d));
                    }
                }
                else
                {
                    for (DrawGeometry& d : detail::ink_stroke_drawings(s))
                    {
                        dry_cache_.push_back(std::move(d));
                    }
                }
            }
        }
        dry_version_ = strokes_.version_;
    }

    renderer.push_clip(bounds());
    renderer.push_transform(Matrix::translation(bounds().x, bounds().y));
    for (const DrawGeometry& d : dry_cache_)
    {
        renderer.draw_geometry(d);
    }
    if (wet_)
    {
        for (const DrawGeometry& d : detail::ink_stroke_drawings(*wet_))
        {
            renderer.draw_geometry(d);
        }
    }
    if (protractor_.visible)
    {
        detail::draw_ink_protractor(renderer, theme, protractor_, protractor_center());
    }
    if (ruler_.visible)
    {
        detail::draw_ink_ruler(renderer, theme, ruler_, ruler_center());
    }
    if (lasso_.size() >= 2)
    {
        draw_lasso_outline(renderer, lasso_);
    }
    renderer.pop_transform();
    renderer.pop_clip();
}

void InkCanvasControl::raise_unprocessed(const PointerEvent& e)
{
    if (on_unprocessed_input_)
    {
        PointerEvent local = e;
        local.position     = to_local(e).position;
        on_unprocessed_input_(local);
    }
}

bool InkCanvasControl::pointer_override(const PointerEvent& e)
{
    // A drag left unprocessed reports every event until its own button comes up.
    if (unprocessed_button_ != PointerButton::None)
    {
        if (e.action == PointerAction::Wheel)
        {
            return false;
        }
        if (e.action == PointerAction::Leave || (e.action == PointerAction::Up && e.button == unprocessed_button_))
        {
            unprocessed_button_ = PointerButton::None;
        }
        raise_unprocessed(e);
        return false;
    }
    // A press on a stencil's body drags it until its release.
    if (stencil_drag_ != 0)
    {
        const Point p = to_local(e).position;
        if (e.action == PointerAction::Move)
        {
            const Point from = stencil_drag_ == 1 ? ruler_center() : protractor_center();
            const Point to{from.x + p.x - stencil_last_.x, from.y + p.y - stencil_last_.y};
            (stencil_drag_ == 1 ? ruler_.center : protractor_.center) = to;
            stencil_last_                                             = p;
            mark_dirty(DirtyFlags::Visual);
            if (on_stencils_changed_)
            {
                on_stencils_changed_();
            }
        }
        else if (e.action == PointerAction::Leave || (e.action == PointerAction::Up && e.button == PointerButton::Left))
        {
            stencil_drag_ = 0;
        }
        return false;
    }
    const bool over  = is_enabled() && bounds().contains(e.position) && has_device(devices_, e.device);
    const bool right = e.button == PointerButton::Right;
    if (e.action == PointerAction::Down && e.button == PointerButton::Left && over && !wet_ && !erasing_)
    {
        const Point p = to_local(e).position;
        const detail::InkStencilTarget
            target = detail::ink_stencil_hit(p, ruler_, ruler_center(), protractor_, protractor_center());
        snap_      = target.hit == detail::InkStencilHit::Edge ? (target.ruler ? target.side : 2) : 0;
        if (target.hit == detail::InkStencilHit::Body)
        {
            stencil_drag_ = target.ruler ? 1 : 2;
            stencil_last_ = p;
            return true;
        }
    }
    const bool skipped = mode_ == InkCanvasInputMode::None ||
                         (right && right_drag_ == InkCanvasRightDragAction::LeaveUnprocessed);
    if (e.action == PointerAction::Down && over && skipped && (right || e.button == PointerButton::Left) && !wet_ &&
        !erasing_)
    {
        unprocessed_button_ = e.button;
        raise_unprocessed(e);
        return false;
    }
    if (mode_ == InkCanvasInputMode::None)
    {
        // Hover and the other buttons over the canvas.
        if (over && e.action != PointerAction::Wheel)
        {
            raise_unprocessed(e);
        }
        return false;
    }

    switch (e.action)
    {
        case PointerAction::Down:
        {
            if ((e.button != PointerButton::Left && !right) || !over)
            {
                return false;
            }
            commit_wet();
            inking_button_        = e.button;
            const bool pen_eraser = e.device == PointerDeviceType::Pen && e.eraser;
            if (pen_eraser && !eraser_input_)
            {
                return false;
            }
            if (mode_ == InkCanvasInputMode::Inking && !pen_eraser)
            {
                wet_ = InkStroke{.points = {}, .attributes = attributes_};
                wet_->points.push_back(snapped(to_local(e)));
                mark_dirty(DirtyFlags::Visual);
            }
            else if (mode_ != InkCanvasInputMode::None)
            {
                erasing_    = true;
                last_erase_ = to_local(e).position;
                erase_along(last_erase_, last_erase_);
            }
            return wet_.has_value() || erasing_;
        }

        case PointerAction::Move:
            if (wet_)
            {
                const InkPoint p    = snapped(to_local(e));
                const Point    last = wet_->points.back().position;
                if (std::hypot(p.position.x - last.x, p.position.y - last.y) >= InkMinPointSpacing)
                {
                    wet_->points.push_back(p);
                    mark_dirty(DirtyFlags::Visual);
                }
            }
            else if (erasing_)
            {
                const Point p = to_local(e).position;
                erase_along(last_erase_, p);
                last_erase_ = p;
            }
            return false;

        case PointerAction::Up:
            if (e.button != inking_button_)
            {
                return false;
            }
            inking_button_ = PointerButton::None;
            if (wet_)
            {
                // A pen leaves the surface at next to no pressure: the release point keeps the last
                // move's, or every stroke would end in a taper.
                InkPoint p = snapped(to_local(e));
                p.pressure = wet_->points.back().pressure;
                if (wet_->points.back().position != p.position)
                {
                    wet_->points.push_back(p);
                }
                commit_wet();
            }
            erasing_ = false;
            return true; // the press that started the stroke or the erase

        case PointerAction::Leave:
            // The pointer left the window with the button held: what was drawn so far stays.
            commit_wet();
            erasing_       = false;
            inking_button_ = PointerButton::None;
            return false;

        case PointerAction::Wheel:
            return false;
    }
    return false;
}

std::unique_ptr<ui::Element> InkCanvas::build() &&
{
    auto                  node = std::make_unique<ui::InkCanvasControl>();
    ui::InkCanvasControl* raw  = node.get();

    apply(drawing_attributes, *node, [raw](const ui::InkDrawingAttributes& v) { raw->set_drawing_attributes(v); });
    apply(input_mode, *node, [raw](const ui::InkCanvasInputMode& v) { raw->set_input_mode(v); });
    apply(input_device_types, *node, [raw](const ui::PointerDeviceTypes& v) { raw->set_input_device_types(v); });
    apply(eraser_input_enabled, *node, [raw](const bool& v) { raw->set_eraser_input_enabled(v); });
    apply(erase_mode, *node, [raw](const ui::InkCanvasEraseMode& v) { raw->set_erase_mode(v); });
    apply(eraser_size, *node, [raw](const float& v) { raw->set_eraser_size(v); });
    apply(right_drag_action, *node, [raw](const ui::InkCanvasRightDragAction& v) { raw->set_right_drag_action(v); });
    apply(ruler, *node, [raw](const ui::InkRuler& v) { raw->set_ruler(v); });
    apply(protractor, *node, [raw](const ui::InkProtractor& v) { raw->set_protractor(v); });
    apply(enabled, *node, [raw](const bool& v) { raw->set_enabled(v); });
    if (!strokes.empty())
    {
        node->strokes().replace(std::move(strokes));
    }

    if (on_strokes_collected)
    {
        node->on_strokes_collected(std::move(on_strokes_collected));
    }
    if (on_strokes_erased)
    {
        node->on_strokes_erased(std::move(on_strokes_erased));
    }
    if (on_strokes_changed)
    {
        node->on_strokes_changed(std::move(on_strokes_changed));
    }
    if (on_unprocessed_input)
    {
        node->on_unprocessed_input(std::move(on_unprocessed_input));
    }

    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
