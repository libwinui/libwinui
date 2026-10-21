// SPDX-License-Identifier: MIT
#include "winui/ui/panes/SidePane.hpp"

#include "../../render/Compositor.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ui
{

namespace
{

// The pane's slide: overlay 350 ms in, 120 ms out on
// "0.1,0.9 0.2,1.0"; inline 200/100 ms on "0.0,0.35 0.15,1.0".
constexpr double      SidePaneOverlayOpenMs  = 350.0;
constexpr double      SidePaneOverlayCloseMs = 120.0;
constexpr double      SidePaneInlineOpenMs   = 200.0;
constexpr double      SidePaneInlineCloseMs  = 100.0;
constexpr CubicBezier SidePaneOverlayCurve{0.1f, 0.9f, 0.2f, 1.0f};
constexpr CubicBezier SidePaneInlineCurve{0.0f, 0.35f, 0.15f, 1.0f};

// The light-dismiss overlay is the page's own "alt" color at 0x99, in every theme.
constexpr float OverlayAlpha = 0.6f;

} // namespace

// ---- state ----

bool SidePaneControl::light_dismissible() const
{
    return display_mode_ == SidePaneDisplayMode::Overlay || display_mode_ == SidePaneDisplayMode::CompactOverlay;
}

bool SidePaneControl::compact() const
{
    return display_mode_ == SidePaneDisplayMode::CompactOverlay || display_mode_ == SidePaneDisplayMode::CompactInline;
}

bool SidePaneControl::can_light_dismiss() const
{
    return pane_open_ && light_dismissible();
}

bool SidePaneControl::pane_live() const
{
    return pane_ != nullptr && (pane_open_ || compact() || progress_ > 0.0);
}

float SidePaneControl::resolved_open_length() const
{
    return std::max(0.0f, open_pane_length_ ? *open_pane_length_ : pane_measured_width_);
}

float SidePaneControl::pane_progress() const
{
    const CubicBezier& curve = overlay_transition_ ? SidePaneOverlayCurve : SidePaneInlineCurve;
    if (pane_open_)
    {
        return easing::evaluate(curve, progress_);
    }
    return 1.0f - easing::evaluate(curve, 1.0 - progress_);
}

Rect SidePaneControl::visible_pane_bounds() const
{
    if (!pane_live())
    {
        return Rect{pane_rect_.x, pane_rect_.y, 0.0f, 0.0f};
    }
    const float open   = pane_rect_.width;
    const float closed = compact() ? std::min(compact_pane_length_, open) : 0.0f;
    const float width  = closed + (open - closed) * pane_progress();
    const float x      = pane_placement_ == SidePanePlacement::Left ? pane_rect_.x : pane_rect_.right() - width;
    return Rect{x, pane_rect_.y, width, pane_rect_.height};
}

void SidePaneControl::set_pane_open(bool open)
{
    if (pane_open_ == open)
    {
        return;
    }
    if (open)
    {
        if (on_pane_opening_)
        {
            on_pane_opening_();
        }
    }
    else if (on_pane_closing_)
    {
        // An explicit close raises on_pane_closing too, but it cannot be
        // canceled.
        (void)on_pane_closing_();
    }
    pane_open_          = open;
    overlay_transition_ = light_dismissible();
    if (!ticking_)
    {
        progress_ = open ? 1.0 : 0.0;
    }
    update_focus_trap();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (open && on_pane_opened_)
    {
        on_pane_opened_();
    }
    else if (!open && on_pane_closed_)
    {
        on_pane_closed_();
    }
}

void SidePaneControl::set_display_mode(SidePaneDisplayMode mode)
{
    if (display_mode_ == mode)
    {
        return;
    }
    display_mode_ = mode;
    update_focus_trap();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void SidePaneControl::try_light_dismiss()
{
    if (!can_light_dismiss())
    {
        return;
    }
    const bool cancel = on_pane_closing_ && detail::canceled(on_pane_closing_());
    if (cancel)
    {
        return;
    }
    // Closing without raising on_pane_closing a second time.
    auto closing     = std::move(on_pane_closing_);
    on_pane_closing_ = {};
    set_pane_open(false);
    on_pane_closing_ = std::move(closing);
}

void SidePaneControl::update_focus_trap()
{
    const bool wanted = can_light_dismiss();
    if (wanted && !trapping_)
    {
        pending_request_ = FocusRequest::Enter;
    }
    else if (!wanted)
    {
        if (trapping_)
        {
            trapping_        = false;
            pending_request_ = FocusRequest::Restore;
        }
        else if (pending_request_ == FocusRequest::Enter)
        {
            pending_request_ = FocusRequest::None; // closed again before Window ever saw it open
        }
    }
}

FocusRequest SidePaneControl::take_focus_request()
{
    const FocusRequest request = pending_request_;
    pending_request_           = FocusRequest::None;
    if (request == FocusRequest::Enter)
    {
        trapping_ = true; // from now on the pane alone is in the ring
    }
    return request;
}

// ---- layout ----

Size SidePaneControl::measure_override(Size available, const MeasureContext& context) const
{
    // The pane is measured in the whole room first, for an unset
    // open_pane_length(); it is then laid out at that length.
    Size pane_desired{};
    if (pane_)
    {
        if (!open_pane_length_)
        {
            pane_measured_width_ = pane_->measure(available, context).width;
        }
        pane_desired = pane_->measure(Size{resolved_open_length(), available.height}, context);
    }
    const float open   = resolved_open_length();
    float       column = 0.0f;
    switch (display_mode_)
    {
        case SidePaneDisplayMode::Overlay:
            break;
        case SidePaneDisplayMode::Inline:
            column = pane_open_ ? open : 0.0f;
            break;
        case SidePaneDisplayMode::CompactOverlay:
            column = compact_pane_length_;
            break;
        case SidePaneDisplayMode::CompactInline:
            column = pane_open_ ? open : compact_pane_length_;
            break;
    }
    Size content_desired{};
    if (content_)
    {
        const Size offer{std::max(0.0f, available.width - column), available.height};
        content_desired = content_->measure(offer, context);
    }
    float width = column + content_desired.width;
    if (pane_live())
    {
        width = std::max(width, pane_open_ ? open : column);
    }
    const float height = std::max(content_desired.height, pane_live() ? pane_desired.height : 0.0f);
    return Size{width, height};
}

void SidePaneControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const float open   = std::min(resolved_open_length(), final_rect.width);
    const bool  left   = pane_placement_ == SidePanePlacement::Left;
    float       column = 0.0f;
    switch (display_mode_)
    {
        case SidePaneDisplayMode::Overlay:
            break;
        case SidePaneDisplayMode::Inline:
            column = pane_open_ ? open : 0.0f;
            break;
        case SidePaneDisplayMode::CompactOverlay:
            column = std::min(compact_pane_length_, final_rect.width);
            break;
        case SidePaneDisplayMode::CompactInline:
            column = pane_open_ ? open : std::min(compact_pane_length_, final_rect.width);
            break;
    }
    pane_rect_    = Rect{left ? final_rect.x : final_rect.right() - open, final_rect.y, open, final_rect.height};
    content_rect_ = Rect{left ? final_rect.x + column : final_rect.x,
                         final_rect.y,
                         std::max(0.0f, final_rect.width - column),
                         final_rect.height};
    if (content_)
    {
        content_->arrange(content_rect_);
    }
    place_children();
}

void SidePaneControl::place_children()
{
    if (!pane_ || !pane_live())
    {
        return;
    }
    // A pane that is not compact slides in and out; a compact one stays put and is uncovered
    // by a widening clip.
    const Rect  visible = visible_pane_bounds();
    const float hidden  = compact() ? 0.0f : pane_rect_.width - visible.width;
    const float shift   = pane_placement_ == SidePanePlacement::Left ? -hidden : hidden;
    pane_->arrange(Rect{pane_rect_.x + shift, pane_rect_.y, pane_rect_.width, pane_rect_.height});
}

// ---- render ----

void SidePaneControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    renderer.push_clip(bounds());
    if (content_)
    {
        content_->render(renderer, context);
    }
    const float progress = pane_progress();
    if (pane_live() && light_dismissible() && light_dismiss_overlay_mode_ == LightDismissOverlayMode::On && progress > 0.0f)
    {
        // The light-dismiss overlay, faded with the pane.
        renderer.fill_rounded_rect(content_rect_,
                                   0.0f,
                                   theme.colors().background.with_alpha(OverlayAlpha * std::clamp(progress, 0.0f, 1.0f)));
    }
    if (pane_live())
    {
        const Rect visible = visible_pane_bounds();
        if (visible.width > 0.0f)
        {
            const Color background = pane_background_.resolve(theme).value_or(
                theme.surfaces.split_view_pane.value_or(theme.surfaces.menu));
            renderer.push_clip(visible);
            renderer.fill_rounded_rect(pane_->bounds(), 0.0f, background);
            pane_->render(renderer, context);
            renderer.pop_clip();
        }
    }
    renderer.pop_clip();
}

// ---- input ----

bool SidePaneControl::pointer_override(const PointerEvent& e)
{
    // Here only where the light-dismiss layer covers the content (covers_children_at()): a release
    // on it closes the pane, and the content sees nothing.
    if (!can_light_dismiss() || (pane_live() && visible_pane_bounds().contains(e.position)))
    {
        return false;
    }
    if (e.action == PointerAction::Up && e.button == PointerButton::Left && content_rect_.contains(e.position))
    {
        try_light_dismiss();
    }
    return e.action == PointerAction::Down || e.action == PointerAction::Up;
}

void SidePaneControl::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    if (pane_ && pane_live())
    {
        out.push_back(pane_.get());
    }
    if (content_)
    {
        out.push_back(content_.get());
    }
}

bool SidePaneControl::covers_children_at(Point p) const
{
    const bool in_pane = pane_live() && visible_pane_bounds().contains(p);
    if (in_pane)
    {
        return false;
    }
    // The light-dismiss layer over the content, and a pane sliding in past what is shown of it.
    return can_light_dismiss() || (pane_ && pane_live() && pane_->bounds().contains(p));
}

bool SidePaneControl::key_override(const KeyEvent& e)
{
    // Escape closes a light-dismissable pane.
    if (e.key == Key::Escape && e.action == KeyAction::Down && can_light_dismiss())
    {
        try_light_dismiss();
        return true;
    }
    return false;
}

// ---- focus and forwarding ----

void SidePaneControl::collect_focusables(std::vector<Element*>& out)
{
    // While the pane holds focus (Tab stays inside a light-dismissable pane) it is
    // the whole ring; otherwise the pane, when any of it shows, then the content.
    if (pane_ && (trapping_ || (pane_open_ || compact())))
    {
        pane_->collect_focusables(out);
    }
    if (content_ && !trapping_)
    {
        content_->collect_focusables(out);
    }
}

void SidePaneControl::for_each_child(ChildVisitor visit) const
{
    if (content_)
    {
        visit(*content_);
    }
    if (pane_)
    {
        visit(*pane_);
    }
}

// The walks below leave out a pane retired from layout (closed, not compact), which
// for_each_child() still lists so its dirty flags get cleared.

void SidePaneControl::sync_native(NativeHost& host)
{
    if (content_)
    {
        content_->sync_native(host);
    }
    if (pane_ && pane_live())
    {
        pane_->sync_native(host);
    }
}

void SidePaneControl::tick_override(Duration delta)
{
    ticking_            = true;
    const double target = pane_open_ ? 1.0 : 0.0;
    if (progress_ != target)
    {
        const double duration = overlay_transition_ ? (pane_open_ ? SidePaneOverlayOpenMs : SidePaneOverlayCloseMs)
                                                    : (pane_open_ ? SidePaneInlineOpenMs : SidePaneInlineCloseMs);
        const double step     = animation_delta(delta).count() / duration;
        progress_             = pane_open_ ? std::min(1.0, progress_ + step) : std::max(0.0, progress_ - step);
        place_children();
        // A finished close retires the pane from layout, which is a size change; before that only
        // the pane moves.
        mark_dirty(progress_ > 0.0 || compact() ? (DirtyFlags::Visual) : (DirtyFlags::Layout | DirtyFlags::Visual));
    }
}

void SidePaneControl::collect_live(std::vector<Element*>& out)
{
    // Itself too: Window polls take_focus_request() on every owner.
    out.push_back(this);
    if (content_)
    {
        detail::TreeAccess::collect_live(*content_, out);
    }
    if (pane_ && pane_live())
    {
        detail::TreeAccess::collect_live(*pane_, out);
    }
}

void SidePaneControl::automation_children(std::vector<Element*>& out) const
{
    if (content_)
    {
        out.push_back(content_.get());
    }
    if (pane_ && pane_live())
    {
        out.push_back(pane_.get());
    }
}

std::unique_ptr<ui::Element> SidePane::build() &&
{
    auto                 node = std::make_unique<ui::SidePaneControl>();
    ui::SidePaneControl* raw  = node.get();
    if (pane)
    {
        node->set_pane(pane.take());
    }
    if (content)
    {
        node->set_content(content.take());
    }
    apply(light_dismiss_overlay_mode,
          *node,
          [raw = node.get()](const LightDismissOverlayMode& v) { raw->set_light_dismiss_overlay_mode(v); });
    apply(display_mode, *node, [raw](const ui::SidePaneDisplayMode& v) { raw->set_display_mode(v); });
    apply(pane_placement, *node, [raw](const ui::SidePanePlacement& v) { raw->set_pane_placement(v); });
    apply(open_pane_length, *node, [raw](const std::optional<float>& v) { raw->set_open_pane_length(v); });
    apply(compact_pane_length, *node, [raw](const float& v) { raw->set_compact_pane_length(v); });
    apply(pane_background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_pane_background(c); }); });
    // Opened after the rest, so the first frame opens in the mode it was declared with.
    apply(is_pane_open, *node, [raw](const bool& v) { raw->set_pane_open(v); });

    if (on_pane_opening)
    {
        node->on_pane_opening(std::move(on_pane_opening));
    }
    // A two-way `.is_pane_open` follows the control: the user opening or closing it writes the property.
    const auto is_pane_open_writer = ui::shared_writer(is_pane_open);
    if (auto handler = ui::write_back_value<bool>(is_pane_open_writer, true, std::move(on_pane_opened)))
    {
        node->on_pane_opened(std::move(handler));
    }
    if (on_pane_closing)
    {
        node->on_pane_closing(std::move(on_pane_closing));
    }
    if (auto handler = ui::write_back_value<bool>(is_pane_open_writer, false, std::move(on_pane_closed)))
    {
        node->on_pane_closed(std::move(handler));
    }
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
