// SPDX-License-Identifier: MIT
#include "winui/ui/dialogs/Flyout.hpp"

#include "FlyoutSpec.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/SplitButton.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace ui
{

namespace
{
// The content padding, applied inside the 1px border, and the flyout's minimum and maximum sizes.
constexpr Thickness FlyoutPadding         = Edges{.left = 16.0f, .top = 15.0f, .right = 16.0f, .bottom = 17.0f};
constexpr float     FlyoutFlyoutBorder    = 1.0f;
constexpr float     FlyoutFlyoutMinWidth  = 96.0f;
constexpr float     FlyoutFlyoutMaxWidth  = 456.0f;
constexpr float     FlyoutFlyoutMinHeight = 40.0f;
constexpr float     FlyoutMaxHeight       = 758.0f;
constexpr float     FlyoutMargin          = 4.0f; // FlyoutMargin: the gap to the anchor

// Padding plus the border it sits inside.
constexpr Thickness FlyoutChrome = Edges{.left   = FlyoutPadding.left + FlyoutFlyoutBorder,
                                         .top    = FlyoutPadding.top + FlyoutFlyoutBorder,
                                         .right  = FlyoutPadding.right + FlyoutFlyoutBorder,
                                         .bottom = FlyoutPadding.bottom + FlyoutFlyoutBorder};
} // namespace

// The flyout's chrome card around content_. Reads content_ live through friendship and never
// caches it, so a later FlyoutControl::set_content() cannot leave it dangling.
class FlyoutSurface final : public Element
{
public:
    explicit FlyoutSurface(FlyoutControl* owner) : owner_(owner)
    {
    }

    void collect_focusables(std::vector<Element*>& out) override
    {
        if (owner_->content_)
        {
            owner_->content_->collect_focusables(out);
        }
    }

    // A ComboBox, a ToolTip or a menu inside the flyout floats its own popup over this one
    // (Window's nested popups).
    void collect_live(std::vector<Element*>& out) override
    {
        if (owner_->content_)
        {
            detail::TreeAccess::collect_live(*owner_->content_, out);
        }
    }

    // The content is what is under the pointer here (the cursor's walk, UI Automation).
    void automation_children(std::vector<Element*>& out) const override
    {
        if (owner_->content_)
        {
            out.push_back(owner_->content_.get());
        }
    }

protected:
    // The offer is the flyout surface's maximum width, never the anchor's width: content wraps at 456 less
    // the chrome (or the window), and the height is what the chosen side has left, capped at 758.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        if (!owner_->content_)
        {
            return {};
        }
        const float max_width  = std::min(FlyoutFlyoutMaxWidth, available.width);
        const float max_height = std::min(FlyoutMaxHeight, available.height);
        const Size  inner      = owner_->content_->measure(Size{std::max(0.0f, max_width - FlyoutChrome.horizontal()),
                                                                std::max(0.0f, max_height - FlyoutChrome.vertical())},
                                                           context);
        return Size{std::clamp(inner.width + FlyoutChrome.horizontal(),
                               FlyoutFlyoutMinWidth,
                               std::max(FlyoutFlyoutMinWidth, max_width)),
                    std::clamp(inner.height + FlyoutChrome.vertical(),
                               FlyoutFlyoutMinHeight,
                               std::max(FlyoutFlyoutMinHeight, max_height))};
    }

    // The flyout surface stretches its content both ways.
    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        if (owner_->content_)
        {
            owner_->content_->arrange(final_rect.inset(FlyoutChrome));
        }
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        fill_overlay_surface(renderer, theme, bounds(), theme.overlay_corner_radius, theme.colors().overlay);
        stroke_rounded_rect_inside(renderer,
                                   bounds(),
                                   theme.overlay_corner_radius,
                                   theme.surfaces.menu_border.value_or(theme.surfaces.field_border),
                                   theme.border_width);
        if (!owner_->content_)
        {
            return;
        }

        // Arbitrary content reads the page tokens (`colors.text`/`colors.background`), so swap in
        // the popup's own. Windows 8.1 flyouts are RequestedTheme="Light", white even in a dark app.
        if (theme.surfaces.menu_text == theme.colors().text && theme.surfaces.menu == theme.colors().background)
        {
            owner_->content_->render(renderer, context);
            return;
        }
        owner_->content_->render(renderer, theme.with_content_colors(theme.surfaces.menu_text, theme.surfaces.menu));
    }

    // The content is live, not a picture: pointer input and the tab ring reach it.
    void hit_test_children(Point /*p*/, std::vector<Element*>& out) const override
    {
        if (owner_->content_)
        {
            out.push_back(owner_->content_.get());
        }
    }


private:
    FlyoutControl* owner_; // non-owning; owner_ outlives this (built lazily by owner_->flyout_surface())
};

void FlyoutBase::adopt_anchor(Element* anchor)
{
    if (auto* button = dynamic_cast<ButtonControl*>(anchor))
    {
        button->set_flyout(this);
    }
    else if (auto* split = dynamic_cast<SplitButtonControl*>(anchor))
    {
        split->set_flyout(this); // opened by its secondary part only
    }
}

bool FlyoutBase::pointer_override(const PointerEvent& e)
{
    if (e.button != PointerButton::Left)
    {
        return false;
    }
    if (e.action == PointerAction::Down)
    {
        // Only while closed: the open popup's content is a child too, and its unhandled presses
        // bubble here as well.
        if (phase_ != FlyoutPhase::Closed || anchor_ == nullptr || !is_enabled() || !anchor_->is_enabled() ||
            !anchor_->bounds().contains(e.position))
        {
            return false;
        }
        anchor_pressed_ = true;
        return true;
    }
    if (e.action == PointerAction::Up)
    {
        const bool pressed = std::exchange(anchor_pressed_, false);
        if (pressed && phase_ == FlyoutPhase::Closed && anchor_ != nullptr && anchor_->bounds().contains(e.position))
        {
            show();
        }
        return pressed;
    }
    return false;
}

void FlyoutBase::sync_popup()
{
    Element* const surface = phase_ != FlyoutPhase::Closed ? flyout_surface() : nullptr;
    if (surface == nullptr)
    {
        close_popup();
        return;
    }
    // The anchor's own bounds, grown by FlyoutMargin along the placement axis: the gap between
    // anchor and flyout.
    const Rect anchor = anchor_ ? anchor_->bounds() : bounds();
    Rect       inside{0.0f, 0.0f, anchor.width, anchor.height};
    if (placement_ == PopupPlacement::Above || placement_ == PopupPlacement::Below)
    {
        inside.y -= FlyoutMargin;
        inside.height += 2.0f * FlyoutMargin;
    }
    else
    {
        inside.x -= FlyoutMargin;
        inside.width += 2.0f * FlyoutMargin;
    }
    PopupRequest request{
        .content             = surface,
        .anchor              = anchor_.get(),
        .anchor_rect         = inside,
        .placement           = placement_,
        .placement_alignment = alignment_,
        .light_dismiss       = light_dismiss_,
        .takes_focus         = is_open() && show_mode_ == FlyoutShowMode::Standard,
        .on_dismiss          = [this](PopupCloseReason) { flyout_dismissed(); },
    };
    if (point_)
    {
        // A flyout shown at a point: the point is the top-left
        // corner, moved left or up by as much as the flyout overflows the window (at most its size).
        request.place = [point = *point_](Element& content, Size client, const MeasureContext& context)
        {
            const Size  desired = content.measure(client, context);
            const float width   = std::min(desired.width, client.width);
            const float height  = std::min(desired.height, client.height);
            float       x       = point.x;
            float       y       = point.y;
            if (x + width > client.width)
            {
                x -= std::min(width, x + width - client.width);
            }
            if (y + height > client.height)
            {
                y -= std::min(height, y + height - client.height);
            }
            content.arrange(Rect{std::max(0.0f, x), std::max(0.0f, y), width, height});
            return true;
        };
    }
    flyout_chrome(request.chrome);
    open_popup(std::move(request));
}

void FlyoutBase::begin_open()
{
    opening_override();
    phase_    = FlyoutPhase::Opening;
    phase_ms_ = 0.0;
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
    if (on_opened_)
    {
        on_opened_();
    }
}

void FlyoutBase::show()
{
    if (is_open())
    {
        return;
    }
    // Also reachable from Closing (a quick re-toggle while the close transition is still
    // running): restarts the open transition fresh, rather than reversing the one in flight.
    point_.reset();
    begin_open();
}

void FlyoutBase::show_at(Point point)
{
    if (is_open())
    {
        begin_close(PopupCloseReason::Programmatic); // moving it: no closing or closed to report
    }
    point_ = point;
    begin_open();
}

void FlyoutBase::hide()
{
    close(PopupCloseReason::Programmatic);
}

void FlyoutBase::close(PopupCloseReason reason)
{
    if (!is_open() || closing_)
    {
        return;
    }
    PopupClosingArgs args{.reason = reason};
    bool             canceled = false;
    if (on_closing_)
    {
        closing_ = true;
        canceled = detail::canceled(on_closing_(args));
        closing_ = false;
    }
    if (canceled || !is_open())
    {
        return;
    }
    begin_close(reason);
}

void FlyoutBase::begin_close(PopupCloseReason reason)
{
    closing_override();
    close_reason_ = reason;
    phase_        = FlyoutPhase::Closing;
    phase_ms_     = 0.0;
    mark_dirty(DirtyFlags::Visual);
    sync_popup(); // it no longer takes focus
}

void FlyoutBase::toggle()
{
    is_open() ? hide() : show();
}

float FlyoutBase::transition_progress() const
{
    if ((phase_ != FlyoutPhase::Opening && phase_ != FlyoutPhase::Closing) || !animations_enabled())
    {
        return 1.0f;
    }
    const double length = phase_ == FlyoutPhase::Opening ? open_ms_ : close_ms_;
    return length > 0.0 ? static_cast<float>(std::clamp(phase_ms_ / length, 0.0, 1.0)) : 1.0f;
}

float FlyoutBase::transition_opacity() const
{
    switch (phase_)
    {
        case FlyoutPhase::Closed:
            return 0.0f;
        case FlyoutPhase::Opening:
            return transition_progress();
        case FlyoutPhase::Open:
            return 1.0f;
        case FlyoutPhase::Closing:
            return 1.0f - transition_progress();
    }
    return 1.0f;
}

Size FlyoutBase::measure_override(Size available, const MeasureContext& context) const
{
    return anchor_ ? anchor_->measure(available, context) : Size{};
}

void FlyoutBase::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (anchor_)
    {
        // Plain passthrough; the popup hangs from the anchor, which has the flyout's bounds.
        anchor_->arrange(final_rect);
    }
    if (phase_ != FlyoutPhase::Closed)
    {
        sync_popup(); // the anchor rect follows the anchor's size
    }
}

void FlyoutBase::render_override(Renderer& renderer, const RenderContext& context) const
{
    if (anchor_)
    {
        anchor_->render(renderer, context);
    }
}

bool FlyoutBase::key_override(const KeyEvent& e)
{
    return anchor_ ? anchor_->handle_key(e) : false;
}

bool FlyoutBase::text_override(const TextInputEvent& e)
{
    return anchor_ ? anchor_->handle_text(e) : false;
}

bool FlyoutBase::focusable() const
{
    return anchor_ && anchor_->focusable();
}

void FlyoutBase::focus_changed(bool focused)
{
    if (anchor_)
    {
        set_focused_of(*anchor_, focused);
    }
}

void FlyoutBase::focus_visible_changed(bool visible)
{
    if (anchor_)
    {
        set_focus_visible_of(*anchor_, visible);
    }
}

void FlyoutBase::clear_dirty_override(DirtyFlags flags)
{
    // The popup is outside the tree Window clears; left dirty, its later changes would
    // stop reaching the window (mark_dirty() only forwards flags that are newly set).
    if (Element* popup = popup_element())
    {
        clear_dirty_of(*popup, flags);
    }
}

void FlyoutBase::tick_override(Duration delta)
{
    if (phase_ != FlyoutPhase::Closed)
    {
        if (Element* popup = popup_element())
        {
            popup->tick(delta);
        }
    }
    if (phase_ == FlyoutPhase::Opening || phase_ == FlyoutPhase::Closing)
    {
        const bool   opening = phase_ == FlyoutPhase::Opening;
        const double length  = opening ? open_ms_ : close_ms_;
        phase_ms_ += animation_delta(delta).count();
        mark_dirty(DirtyFlags::Visual); // keep Window::tick()'s dirty check true for the transition
        if (phase_ms_ >= length)
        {
            phase_    = opening ? FlyoutPhase::Open : FlyoutPhase::Closed;
            phase_ms_ = length;
            if (!opening)
            {
                point_.reset();
                close_popup();
                if (on_closed_)
                {
                    on_closed_(close_reason_); // last: the handler may tear the page down
                }
            }
        }
    }
}

Element* FlyoutControl::flyout_surface()
{
    if (!content_)
    {
        return nullptr;
    }
    if (!surface_)
    {
        surface_ = std::make_unique<FlyoutSurface>(this);
    }
    return surface_.get();
}

std::unique_ptr<ui::Element> Flyout::build() &&
{
    auto node = std::make_unique<ui::FlyoutControl>();
    apply(placement, *node, [raw = node.get()](const PopupPlacement& v) { raw->set_placement(v); });
    apply(placement_alignment,
          *node,
          [raw = node.get()](const PopupPlacementAlignment& v) { raw->set_placement_alignment(v); });
    apply(show_mode, *node, [raw = node.get()](const FlyoutShowMode& v) { raw->set_show_mode(v); });
    if (anchor)
    {
        node->set_anchor(anchor.take());
    }
    if (content)
    {
        node->set_content(content.take());
    }
    detail::apply_flyout_open(*node, is_open, light_dismiss, on_opened, on_closing, on_closed);

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
