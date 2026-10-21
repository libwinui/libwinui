// SPDX-License-Identifier: MIT
#include "winui/ui/dialogs/TeachingTip.hpp"

#include "../LocalText.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

namespace detail
{
// Where the card sits against its target. The first word is the side of the target the card is
// on; the second, for the corner spots, is the way the card extends from the tail. The public
// face is PopupPlacement + PopupPlacementAlignment; Auto and Center have none.
enum class TeachingTipSpot : unsigned char
{
    Auto,        // targeted: Top first, then the fallback order; untargeted: bottom center
    Top,         // above the target, centered on it
    Bottom,      // below the target, centered on it
    Left,        // left of the target, centered on it
    Right,       // right of the target, centered on it
    TopRight,    // above, the card extending to the right of the tail
    TopLeft,     // above, the card extending to the left of the tail
    BottomRight, // below, the card extending to the right of the tail
    BottomLeft,  // below, the card extending to the left of the tail
    LeftTop,     // left, the card extending upwards from the tail
    LeftBottom,  // left, the card extending downwards from the tail
    RightTop,    // right, the card extending upwards from the tail
    RightBottom, // right, the card extending downwards from the tail
    Center,      // centered over the target (untargeted: the window); only ever a last resort
};
} // namespace detail

namespace
{
// TeachingTip metrics.
constexpr float Strip        = 8.0f;  // the tail's strip on every side
constexpr float TailHalf     = 10.0f; // half the polygon's 20 DIP long side
constexpr float TailDepth    = 10.0f; // the polygon's short side: 8 in the strip, 2 under the card
constexpr float TailToCorner = 28.0f; // the closest a tail's center comes to a corner: strip + margin + half the tail
constexpr float TeachingTipMinWidth  = 320.0f; // strips included
constexpr float TeachingTipMaxWidth  = 336.0f;
constexpr float TeachingTipMinHeight = 40.0f;
constexpr float TeachingTipMaxHeight = 520.0f;
constexpr float ContentMargin        = 12.0f; // around the card's content
constexpr float TeachingTipIconGap   = 12.0f; // between the icon and the titles
constexpr float TeachingTipIconSize  = 20.0f; // the icon column for a path or picture: a Symbol's size
constexpr float TitlesCloseGap       = 28.0f; // right of the titles when the close X sits in the header
constexpr float SectionGap           = 12.0f; // the content's and the button row's top margins
constexpr float ButtonGap            = 8.0f;  // 4 + 4, between the two buttons
constexpr float AltCloseSize         = 40.0f; // the header close button
constexpr float AltCloseInset        = 4.0f;  // the close button's outer padding; the visible square is 32
constexpr float AltCloseGlyph        = 16.0f; // the close button's X
constexpr float WindowEdge           = 24.0f; // an untargeted tip's distance from the window edge

constexpr IconGlyph TeachingTipCloseGlyph{0xE711, "Segoe Fluent Icons", AltCloseGlyph}; // the close button's X

using P = detail::TeachingTipSpot;

// The spot a placement and an alignment ask for: Leading runs the card right (or down) from the
// tail, Trailing left (or up).
[[nodiscard]] P spot_for(std::optional<PopupPlacement> placement, PopupPlacementAlignment alignment)
{
    if (!placement)
    {
        return P::Auto;
    }
    const auto pick = [alignment](P center, P leading, P trailing)
    {
        switch (alignment)
        {
            case PopupPlacementAlignment::Center:
                break;
            case PopupPlacementAlignment::Leading:
                return leading;
            case PopupPlacementAlignment::Trailing:
                return trailing;
        }
        return center;
    };
    switch (*placement)
    {
        case PopupPlacement::Above:
            break;
        case PopupPlacement::Below:
            return pick(P::Bottom, P::BottomRight, P::BottomLeft);
        case PopupPlacement::Left:
            return pick(P::Left, P::LeftBottom, P::LeftTop);
        case PopupPlacement::Right:
            return pick(P::Right, P::RightBottom, P::RightTop);
    }
    return pick(P::Top, P::TopRight, P::TopLeft);
}

[[nodiscard]] bool is_bottom(P p)
{
    return p == P::Bottom || p == P::BottomLeft || p == P::BottomRight;
}
[[nodiscard]] bool is_left(P p)
{
    return p == P::Left || p == P::LeftTop || p == P::LeftBottom;
}
[[nodiscard]] bool is_right(P p)
{
    return p == P::Right || p == P::RightTop || p == P::RightBottom;
}

// The placements to try, in order, when the preferred one does not fit.
[[nodiscard]] std::array<P, 13> fallback_order(P preferred)
{
    std::array<P, 13> order{P::Top,
                            P::Bottom,
                            P::Left,
                            P::Right,
                            P::TopLeft,
                            P::TopRight,
                            P::BottomLeft,
                            P::BottomRight,
                            P::LeftTop,
                            P::LeftBottom,
                            P::RightTop,
                            P::RightBottom,
                            P::Center};
    const auto        swap_vertical = [&order]
    {
        std::swap(order[0], order[1]);
        std::swap(order[4], order[6]);
        std::swap(order[5], order[7]);
    };
    const auto lateral_first = [&order]
    {
        std::swap(order[0], order[2]);
        std::swap(order[1], order[3]);
        std::swap(order[4], order[8]);
        std::swap(order[5], order[9]);
        std::swap(order[6], order[10]);
        std::swap(order[7], order[11]);
    };
    if (is_bottom(preferred))
    {
        swap_vertical();
    }
    else if (is_left(preferred))
    {
        lateral_first();
    }
    else if (is_right(preferred))
    {
        lateral_first();
        swap_vertical();
    }
    const auto pivot = std::find(order.begin(), order.end(), preferred);
    if (pivot != order.end())
    {
        std::rotate(order.begin(), pivot, pivot + 1);
    }
    return order;
}

TextStyle title_style(const Theme& theme, bool semibold)
{
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    style.vertical   = TextAlign::Leading;
    style.weight     = semibold ? FontWeight::SemiBold : FontWeight::Regular;
    style.wrapping   = TextWrapping::WrapWholeWords;
    return style;
}

Size measure_text(const TextMeasurer& measurer, const std::string& text, const TextStyle& style, float max_width)
{
    if (text.empty())
    {
        return {};
    }
    return max_width > 0.0f ? measurer.measure_wrapped(text, style, max_width) : measurer.measure(text, style);
}
} // namespace

// The card and its tail. Reads its owner's state live through friendship, so it
// never caches a pointer a later set_content() could leave dangling.
class TeachingTipSurface final : public Element
{
public:
    explicit TeachingTipSurface(TeachingTipControl* owner) : owner_(owner)
    {
    }

    void collect_focusables(std::vector<Element*>& out) override
    {
        for (Element* child : children())
        {
            child->collect_focusables(out);
        }
    }

    void collect_live(std::vector<Element*>& out) override
    {
        // Window's owner walk does not reach popups inside a popup, and the card is not one.
        (void)out;
    }

    // The tail's polygon. Its base runs 2 DIPs inside the card, so the fill covers
    // the card's frame there and the slanted sides carry the frame on.
    [[nodiscard]] static std::vector<Point> tail(const Rect& s, P placement)
    {
        std::vector<Point> points;
        const auto         along_x = [&](float cx, float edge, float direction)
        {
            points = {Point{cx - TailHalf, edge - direction * TailDepth},
                      Point{cx, edge},
                      Point{cx + TailHalf, edge - direction * TailDepth}};
        };
        const auto along_y = [&](float cy, float edge, float direction)
        {
            points = {Point{edge - direction * TailDepth, cy - TailHalf},
                      Point{edge, cy},
                      Point{edge - direction * TailDepth, cy + TailHalf}};
        };
        const float cx = s.x + s.width * 0.5f;
        const float cy = s.y + s.height * 0.5f;
        switch (placement)
        {
            case P::Auto: // not a placement a tip settles on
            case P::Top:
            case P::Center:
                along_x(cx, s.bottom(), 1.0f);
                break;
            case P::TopRight: // the card extends right, so the tail is near its left corner
                along_x(s.x + TailToCorner, s.bottom(), 1.0f);
                break;
            case P::TopLeft:
                along_x(s.right() - TailToCorner, s.bottom(), 1.0f);
                break;
            case P::Bottom:
                along_x(cx, s.y, -1.0f);
                break;
            case P::BottomRight:
                along_x(s.x + TailToCorner, s.y, -1.0f);
                break;
            case P::BottomLeft:
                along_x(s.right() - TailToCorner, s.y, -1.0f);
                break;
            case P::Left:
                along_y(cy, s.right(), 1.0f);
                break;
            case P::LeftTop: // the card extends up, so the tail is near its bottom corner
                along_y(s.bottom() - TailToCorner, s.right(), 1.0f);
                break;
            case P::LeftBottom:
                along_y(s.y + TailToCorner, s.right(), 1.0f);
                break;
            case P::Right:
                along_y(cy, s.x, -1.0f);
                break;
            case P::RightTop:
                along_y(s.bottom() - TailToCorner, s.x, -1.0f);
                break;
            case P::RightBottom:
                along_y(s.y + TailToCorner, s.x, -1.0f);
                break;
        }
        return points;
    }

    // What the last measure found; the placement needs them before anything is arranged.
    [[nodiscard]] float hero_height() const
    {
        return hero_h_;
    }
    [[nodiscard]] float non_hero_height() const
    {
        return non_hero_h_;
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        const TeachingTipControl& t           = *owner_;
        const float               border      = context.theme.border_width;
        const float               max_outer_w = std::clamp(available.width, TeachingTipMinWidth, TeachingTipMaxWidth);
        const float max_outer_h = std::clamp(available.height, TeachingTipMinHeight, TeachingTipMaxHeight);
        const float chrome_w    = 2.0f * (Strip + border + ContentMargin);

        const auto measure_inside = [&](float inner)
        {
            icon_w_               = t.icon_ ? t.icon_.extent(TeachingTipIconSize) + TeachingTipIconGap : 0.0f;
            const float close_gap = t.alternate_close_visible() ? TitlesCloseGap : 0.0f;
            const float titles_w  = std::max(0.0f, inner - icon_w_ - close_gap);
            title_    = measure_text(context.measurer, t.title_, title_style(context.theme, true), titles_w);
            subtitle_ = measure_text(context.measurer, t.subtitle_, title_style(context.theme, false), titles_w);
            content_  = t.content_ ? t.content_->measure(Size{inner, Unbounded}, context) : Size{};
            return icon_w_ + std::max(title_.width, subtitle_.width) + close_gap;
        };

        // Both buttons at the width of the wider, 8 apart.
        button_h_            = 0.0f;
        float      buttons_w = 0.0f;
        const bool action    = !t.action_text_.empty();
        const bool closing   = !t.close_text_.empty();
        for (ButtonControl* b : {action ? t.action_button_.get() : nullptr, closing ? t.close_button_.get() : nullptr})
        {
            if (b != nullptr)
            {
                const Size s = b->measure(Size{Unbounded, Unbounded}, context);
                buttons_w    = std::max(buttons_w, s.width);
                button_h_    = std::max(button_h_, s.height);
            }
        }
        if (action && closing)
        {
            buttons_w = 2.0f * buttons_w + ButtonGap;
        }
        if (t.alternate_close_)
        {
            const float visible = AltCloseSize - 2.0f * AltCloseInset;
            (void)t.alternate_close_->measure(Size{visible, visible}, context);
        }

        // Widest first, to learn how wide the card wants to be, then once more at that width so
        // wrapped text breaks where arrange will draw it.
        const float widest  = std::max(0.0f, max_outer_w - chrome_w);
        const float natural = std::max({measure_inside(widest), content_.width, buttons_w});
        const float outer_w = std::clamp(natural + chrome_w, TeachingTipMinWidth, std::max(TeachingTipMinWidth, max_outer_w));
        inner_w_ = std::max(0.0f, outer_w - chrome_w);
        (void)measure_inside(inner_w_);

        header_h_ = std::max(t.icon_ ? t.icon_.extent(TeachingTipIconSize) : 0.0f, title_.height + subtitle_.height);
        float non_hero = ContentMargin + header_h_;
        if (t.content_)
        {
            non_hero += SectionGap + content_.height;
        }
        if (action || closing)
        {
            non_hero += SectionGap + button_h_;
        }
        non_hero += ContentMargin;
        if (t.alternate_close_visible())
        {
            non_hero = std::max(non_hero, AltCloseSize); // the X shares the card's content area
        }
        non_hero_h_ = non_hero;

        hero_h_ = t.hero_ ? t.hero_->measure(Size{outer_w - 2.0f * (Strip + border), Unbounded}, context).height : 0.0f;
        border_ = border;

        const float outer_h = std::clamp(hero_h_ + non_hero_h_ + 2.0f * (Strip + border),
                                         TeachingTipMinHeight,
                                         std::max(TeachingTipMinHeight, max_outer_h));
        return Size{outer_w, outer_h};
    }

    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        TeachingTipControl& t     = *owner_;
        const Rect          card  = final_rect.inset(Thickness{Strip});
        const Rect          inner = card.inset(Thickness{border_});

        const bool bottom_hero = t.hero_ && t.hero_at_bottom();
        const Rect hero_rect   = !t.hero_      ? Rect{}
                                 : bottom_hero ? Rect{inner.x, inner.bottom() - hero_h_, inner.width, hero_h_}
                                               : Rect{inner.x, inner.y, inner.width, hero_h_};
        if (t.hero_)
        {
            t.hero_->arrange(hero_rect);
        }
        hero_rect_               = hero_rect;
        const float non_hero_top = bottom_hero ? inner.y : inner.y + hero_h_;
        non_hero_rect_           = Rect{inner.x, non_hero_top, inner.width, std::max(0.0f, inner.height - hero_h_)};

        const float x         = non_hero_rect_.x + ContentMargin;
        float       y         = non_hero_rect_.y + ContentMargin;
        const float icon_size = t.icon_.extent(TeachingTipIconSize);
        icon_rect_            = t.icon_ ? Rect{x, y, icon_size, icon_size} : Rect{};
        const float titles_x  = x + icon_w_;
        const float titles_w = std::max(0.0f, inner_w_ - icon_w_ - (t.alternate_close_visible() ? TitlesCloseGap : 0.0f));
        title_rect_    = Rect{titles_x, y, titles_w, title_.height};
        subtitle_rect_ = Rect{titles_x, y + title_.height, titles_w, subtitle_.height};
        y += header_h_;

        if (t.content_)
        {
            y += SectionGap;
            t.content_->arrange(Rect{x, y, inner_w_, content_.height});
            y += content_.height;
        }

        const bool action  = !t.action_text_.empty();
        const bool closing = !t.close_text_.empty();
        if (action || closing)
        {
            y += SectionGap;
            if (action && closing)
            {
                const float half = std::max(0.0f, (inner_w_ - ButtonGap) * 0.5f);
                t.action_button_->arrange(Rect{x, y, half, button_h_});
                t.close_button_->arrange(Rect{x + half + ButtonGap, y, half, button_h_});
            }
            else
            {
                (action ? t.action_button_ : t.close_button_)->arrange(Rect{x, y, inner_w_, button_h_});
            }
        }

        // The X: a 40x40 cell against the non-hero area's top-right corner, 32x32 of it visible.
        if (t.alternate_close_visible())
        {
            t.alternate_close_->arrange(Rect{non_hero_rect_.right() - AltCloseSize + AltCloseInset,
                                             non_hero_rect_.y + AltCloseInset,
                                             AltCloseSize - 2.0f * AltCloseInset,
                                             AltCloseSize - 2.0f * AltCloseInset});
        }
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme&              theme = context.theme;
        const TeachingTipControl& t     = *owner_;
        if (!t.open_)
        {
            return; // closed during placement (it fit nowhere)
        }
        const Color background = t.light_dismiss_ ? theme.surfaces.menu
                                                  : theme.surfaces.teaching_tip.value_or(theme.surfaces.menu);
        const Color border     = theme.surfaces.surface_stroke.value_or(
            theme.surfaces.menu_border.value_or(theme.surfaces.field_border));
        const Color text   = theme.surfaces.menu_text;
        const float radius = theme.overlay_corner_radius;
        const Rect  card   = bounds().inset(Thickness{Strip});

        // The in-app acrylic, only while light-dismissable.
        const bool acrylic = t.light_dismiss_ && theme.overlay_material;
        if (acrylic)
        {
            fill_overlay_surface(renderer, theme, card, radius, background);
        }
        else
        {
            renderer.fill_rounded_rect(card, radius, background);
        }

        // The card's content reads the card's colors, not the page's (as FlyoutSurface does).
        const Theme inside = theme.with_content_colors(text, background);

        if (t.hero_)
        {
            renderer.push_clip(hero_rect_);
            t.hero_->render(renderer, inside);
            renderer.pop_clip();
        }
        t.icon_.draw_in_slot(renderer, icon_rect_, text, icon_rect_.width, theme.icon_font_family);
        if (!t.title_.empty())
        {
            renderer.draw_text(t.title_, title_rect_, text, title_style(theme, true));
        }
        if (!t.subtitle_.empty())
        {
            renderer.draw_text(t.subtitle_, subtitle_rect_, text, title_style(theme, false));
        }
        if (t.content_)
        {
            renderer.push_clip(non_hero_rect_); // clipped, not scrolled
            t.content_->render(renderer, inside);
            renderer.pop_clip();
        }
        if (!t.action_text_.empty())
        {
            t.action_button_->render(renderer, inside);
        }
        if (!t.close_text_.empty())
        {
            t.close_button_->render(renderer, inside);
        }
        if (t.alternate_close_visible())
        {
            t.alternate_close_->render(renderer, inside);
        }

        stroke_rounded_rect_inside(renderer, card, radius, border, theme.border_width);

        if (t.tail_shown())
        {
            std::vector<Point> points = tail(bounds(), t.spot());
            if (acrylic)
            {
                // The tail is the same material as the card, filled through the brush.
                AcrylicBrush brush   = *theme.overlay_material;
                brush.fallback_color = background;
                renderer.draw_geometry(DrawGeometry{.geometry = PathGeometry::polyline(points, true), .fill = Brush{brush}});
            }
            else
            {
                renderer.fill_polygon(FillPolygon{points, background});
            }
            // The two slanted sides; the base stays open, inside the card.
            renderer.stroke_polyline(StrokePolyline{std::move(points), border, theme.border_width, false});
        }
    }

    void hit_test_children(Point /*p*/, std::vector<Element*>& out) const override
    {
        const std::vector<Element*> live = children(); // drawn in this order
        out.assign(live.rbegin(), live.rend());
    }


private:
    // The live children, in tab order: content, action, close, then the X.
    [[nodiscard]] std::vector<Element*> children() const
    {
        const TeachingTipControl& t = *owner_;
        std::vector<Element*>     out;
        if (t.hero_)
        {
            out.push_back(t.hero_.get());
        }
        if (t.content_)
        {
            out.push_back(t.content_.get());
        }
        if (!t.action_text_.empty())
        {
            out.push_back(t.action_button_.get());
        }
        if (!t.close_text_.empty())
        {
            out.push_back(t.close_button_.get());
        }
        if (t.alternate_close_visible())
        {
            out.push_back(t.alternate_close_.get());
        }
        return out;
    }

    TeachingTipControl* owner_; // non-owning; owner_ outlives this (it owns it)

    // Cached by measure_override() for arrange_override() and render().
    mutable Size  title_{};
    mutable Size  subtitle_{};
    mutable Size  content_{};
    mutable float icon_w_     = 0.0f;
    mutable float inner_w_    = 0.0f;
    mutable float header_h_   = 0.0f;
    mutable float button_h_   = 0.0f;
    mutable float non_hero_h_ = 0.0f;
    mutable float hero_h_     = 0.0f;
    mutable float border_     = 1.0f;

    // Cached by arrange_override() for render().
    Rect hero_rect_{};
    Rect non_hero_rect_{};
    Rect icon_rect_{};
    Rect title_rect_{};
    Rect subtitle_rect_{};
};

TeachingTipControl::TeachingTipControl() :
    action_button_(std::make_unique<ButtonControl>()),
    close_button_(std::make_unique<ButtonControl>()),
    alternate_close_(std::make_unique<ButtonControl>())
{
    set_parent_of(*action_button_, this);
    action_button_->on_click(
        [this]
        {
            if (on_action_button_click_)
            {
                on_action_button_click_(); // only the action handler: the action does not close the tip
            }
        });

    const auto close_clicked = [this]
    {
        if (on_close_button_click_)
        {
            on_close_button_click_();
        }
        close(PopupCloseReason::CloseButton);
    };
    set_parent_of(*close_button_, this);
    close_button_->on_click(close_clicked);

    set_parent_of(*alternate_close_, this);
    alternate_close_->set_variant(ButtonVariant::Subtle);
    alternate_close_->set_padding(Thickness{0.0f});
    alternate_close_->set_content(std::make_unique<IconControl>(TeachingTipCloseGlyph));
    detail::follow_strings(*this,
                           [this] { alternate_close_->set_accessibility(Accessibility{.name = strings().common.close}); });
    alternate_close_->on_click(close_clicked);

    set_hit_test_visible(false); // the card draws in the popup layer, so nothing here takes a press
}

TeachingTipControl::~TeachingTipControl() = default;

void TeachingTipControl::adopt(std::unique_ptr<Element>& slot, std::unique_ptr<Element> element)
{
    if (element)
    {
        set_parent_of(*element, this);
    }
    slot = std::move(element);
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_title(std::string title)
{
    title_ = std::move(title);
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_subtitle(std::string subtitle)
{
    subtitle_ = std::move(subtitle);
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_icon(IconSource icon)
{
    icon_ = std::move(icon);
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_hero_content_placement(TeachingTipHeroPlacement placement)
{
    hero_placement_ = placement;
    effective_.reset(); // the placement is decided again when the hero moves
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_action_button_text(std::string text)
{
    action_text_ = std::move(text);
    update_buttons();
}

void TeachingTipControl::set_close_button_text(std::string text)
{
    close_text_ = std::move(text);
    update_buttons();
}

void TeachingTipControl::update_buttons()
{
    action_button_->set_text(action_text_);
    close_button_->set_text(close_text_);
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_target(Element* target)
{
    target_.reset(target);
    effective_.reset();
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_placement(std::optional<PopupPlacement> placement)
{
    placement_ = placement;
    effective_.reset();
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_placement_alignment(PopupPlacementAlignment alignment)
{
    alignment_ = alignment;
    effective_.reset();
    mark_dirty(DirtyFlags::Visual);
}

P TeachingTipControl::spot() const
{
    return effective_.value_or(P::Top);
}

PopupPlacement TeachingTipControl::effective_placement() const
{
    switch (spot())
    {
        case P::Bottom:
        case P::BottomRight:
        case P::BottomLeft:
            return PopupPlacement::Below;
        case P::Left:
        case P::LeftTop:
        case P::LeftBottom:
            return PopupPlacement::Left;
        case P::Right:
        case P::RightTop:
        case P::RightBottom:
            return PopupPlacement::Right;
        default:
            return PopupPlacement::Above;
    }
}

PopupPlacementAlignment TeachingTipControl::effective_placement_alignment() const
{
    switch (spot())
    {
        case P::TopRight:
        case P::BottomRight:
        case P::LeftBottom:
        case P::RightBottom:
            return PopupPlacementAlignment::Leading;
        case P::TopLeft:
        case P::BottomLeft:
        case P::LeftTop:
        case P::RightTop:
            return PopupPlacementAlignment::Trailing;
        default:
            return PopupPlacementAlignment::Center;
    }
}

void TeachingTipControl::set_placement_margin(Thickness margin)
{
    placement_margin_ = margin;
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_tail_visibility(AutoVisibility visibility)
{
    tail_visibility_ = visibility;
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::enabled_changed()
{
    action_button_->set_enabled(is_enabled());
    close_button_->set_enabled(is_enabled());
    alternate_close_->set_enabled(is_enabled());
    mark_dirty(DirtyFlags::Visual);
}

void TeachingTipControl::set_light_dismiss(bool enabled)
{
    light_dismiss_ = enabled;
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

bool TeachingTipControl::tail_shown() const
{
    // The tail is collapsed on request, and for an untargeted tip unless asked for.
    if (tail_visibility_ == AutoVisibility::Collapsed)
    {
        return false;
    }
    return targeted_ || tail_visibility_ == AutoVisibility::Visible;
}

bool TeachingTipControl::hero_at_bottom() const
{
    switch (hero_placement_)
    {
        case TeachingTipHeroPlacement::Top:
            return false;
        case TeachingTipHeroPlacement::Bottom:
            return true;
        case TeachingTipHeroPlacement::Auto:
            break;
    }
    // Away from the tail: a tail on the top edge, or high on a side, sends the hero down.
    const P p = spot();
    return tail_shown() && (is_bottom(p) || p == P::LeftBottom || p == P::RightBottom);
}

void TeachingTipControl::set_open(bool open)
{
    if (!open)
    {
        close(PopupCloseReason::Programmatic);
        return;
    }
    if (open_)
    {
        return;
    }
    open_ = true;
    effective_.reset(); // decided afresh on every open, at the next layout
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
    if (on_opened_)
    {
        on_opened_(); // at once: there is no expand animation to wait for
    }
}

void TeachingTipControl::close(PopupCloseReason reason)
{
    if (!open_ || closing_)
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
    if (canceled)
    {
        return;
    }
    open_ = false;
    effective_.reset();
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
    if (on_closed_)
    {
        on_closed_(reason);
    }
}

void TeachingTipControl::sync_popup()
{
    if (!open_)
    {
        close_popup();
        return;
    }
    if (!surface_)
    {
        surface_ = std::make_unique<TeachingTipSurface>(this);
    }
    open_popup({
        .content       = surface_.get(),
        .light_dismiss = light_dismiss_,
        .takes_focus   = light_dismiss_, // a plain tip leaves focus where it is
        // The card, not the tail strips around it.
        .chrome = {.shadow_rect = [](const Rect& bounds) { return bounds.inset(Thickness{Strip}); }},
        .place  = [this](Element& content, Size client, const MeasureContext& context)
        { return place_card(content, client, context); },
        .on_dismiss = [this](PopupCloseReason) { close(PopupCloseReason::LightDismiss); },
    });
}

std::optional<P> TeachingTipControl::fit_targeted(Size tip, Rect target, Size client) const
{
    // The placement for a targeted tip, with the window as the bounds. `tip` adds one more strip
    // to the whole popup.
    std::array<bool, 14> ok{};
    ok.fill(true);
    ok[static_cast<std::size_t>(P::Auto)] = false;
    const auto off                        = [&ok](std::initializer_list<P> modes)
    {
        for (P m : modes)
        {
            ok[static_cast<std::size_t>(m)] = false;
        }
    };

    const float content_h = tip.height;
    const float content_w = tip.width;
    const float tip_h     = content_h + Strip;
    const float tip_w     = content_w + Strip;

    // Keep the tail off the hero: a side placement needs the text area taller than the hero by a
    // tail's length.
    if (hero_)
    {
        if (surface_ && surface_->hero_height() > surface_->non_hero_height() - 2.0f * TailHalf)
        {
            off({P::Left, P::Right});
        }
        if (hero_placement_ == TeachingTipHeroPlacement::Bottom)
        {
            off({P::Top, P::TopRight, P::TopLeft, P::RightTop, P::LeftTop, P::Center});
        }
        else if (hero_placement_ == TeachingTipHeroPlacement::Top)
        {
            off({P::Bottom, P::BottomLeft, P::BottomRight, P::RightBottom, P::LeftBottom});
        }
    }

    const float left   = target.x;
    const float top    = target.y;
    const float right  = client.width - target.right();
    const float bottom = client.height - target.bottom();
    const float half_w = target.width * 0.5f;
    const float half_h = target.height * 0.5f;

    if (left < 0.0f)
    {
        off({P::LeftBottom, P::Left, P::LeftTop});
    }
    if (right < 0.0f)
    {
        off({P::RightBottom, P::Right, P::RightTop});
    }
    if (top < 0.0f)
    {
        off({P::TopLeft, P::Top, P::TopRight});
    }
    if (bottom < 0.0f)
    {
        off({P::BottomLeft, P::Bottom, P::BottomRight});
    }
    if (left < -half_w || right < -half_w)
    {
        off({P::TopLeft, P::Top, P::TopRight, P::BottomLeft, P::Bottom, P::BottomRight, P::Center});
    }
    if (top < -half_h || bottom < -half_h)
    {
        off({P::LeftBottom, P::Left, P::LeftTop, P::RightBottom, P::Right, P::RightTop, P::Center});
    }

    if (tip_h > top)
    {
        off({P::Top, P::TopRight, P::TopLeft});
    }
    if (tip_h > top + half_h)
    {
        off({P::Center});
    }
    if (content_h - TailToCorner > top + half_h)
    {
        off({P::RightTop, P::LeftTop});
    }
    if (content_h * 0.5f > top + half_h || content_h * 0.5f > bottom + half_h)
    {
        off({P::Right, P::Left});
    }
    if (content_h - TailToCorner > bottom + half_h)
    {
        off({P::RightBottom, P::LeftBottom});
    }
    if (tip_h > bottom)
    {
        off({P::Bottom, P::BottomLeft, P::BottomRight});
    }
    if (tip_w > left)
    {
        off({P::Left, P::LeftTop, P::LeftBottom});
    }
    if (content_w - TailToCorner > left + half_w)
    {
        off({P::TopLeft, P::BottomLeft});
    }
    if (content_w * 0.5f > left + half_w || content_w * 0.5f > right + half_w)
    {
        off({P::Top, P::Bottom, P::Center});
    }
    if (content_w - TailToCorner > right + half_w)
    {
        off({P::TopRight, P::BottomRight});
    }
    if (tip_w > right)
    {
        off({P::Right, P::RightTop, P::RightBottom});
    }

    for (P mode : fallback_order(spot_for(placement_, alignment_)))
    {
        if (ok[static_cast<std::size_t>(mode)])
        {
            return mode;
        }
    }
    return std::nullopt;
}

bool TeachingTipControl::place_card(Element& content, Size client, const MeasureContext& context)
{
    const Size tip = content.measure(client, context);

    if (!effective_)
    {
        targeted_ = target_.get() != nullptr;
        if (targeted_)
        {
            effective_ = fit_targeted(tip, target_.get()->bounds(), client);
        }
        else if (client.height > tip.height && client.width > tip.width)
        {
            effective_ = P::Bottom; // an untargeted tip that fits the window
        }
        if (!effective_)
        {
            // Fits nowhere: the tip does not open, and says so through Closing/Closed.
            close(PopupCloseReason::Programmatic);
            content.arrange(Rect{});
            return true;
        }
    }

    const Thickness m = placement_margin_;
    float           x = 0.0f;
    float           y = 0.0f;
    if (targeted_ && target_.get() != nullptr)
    {
        // A targeted tip sits against its target.
        const Rect  t      = target_.get()->bounds();
        const float cx     = t.x + t.width * 0.5f;
        const float cy     = t.y + t.height * 0.5f;
        const float above  = t.y - tip.height - m.top;
        const float below  = t.bottom() + m.bottom;
        const float before = t.x - tip.width - m.left;
        const float after  = t.right() + m.right;
        switch (*effective_)
        {
            case P::Auto:
            case P::Top:
                y = above, x = cx - tip.width * 0.5f;
                break;
            case P::Bottom:
                y = below, x = cx - tip.width * 0.5f;
                break;
            case P::Left:
                x = before, y = cy - tip.height * 0.5f;
                break;
            case P::Right:
                x = after, y = cy - tip.height * 0.5f;
                break;
            case P::TopRight:
                y = above, x = cx - TailToCorner;
                break;
            case P::TopLeft:
                y = above, x = cx - tip.width + TailToCorner;
                break;
            case P::BottomRight:
                y = below, x = cx - TailToCorner;
                break;
            case P::BottomLeft:
                y = below, x = cx - tip.width + TailToCorner;
                break;
            case P::LeftTop:
                x = before, y = cy - tip.height + TailToCorner;
                break;
            case P::LeftBottom:
                x = before, y = cy - TailToCorner;
                break;
            case P::RightTop:
                x = after, y = cy - tip.height + TailToCorner;
                break;
            case P::RightBottom:
                x = after, y = cy - TailToCorner;
                break;
            case P::Center:
                y = cy - tip.height - m.top, x = cx - tip.width * 0.5f;
                break;
        }
    }
    else
    {
        // An untargeted tip sits against the window, by the *preferred* placement.
        const float near_x   = WindowEdge + m.left;
        const float near_y   = WindowEdge + m.top;
        const float far_x    = client.width - (tip.width + WindowEdge + m.right);
        const float far_y    = client.height - (tip.height + WindowEdge + m.bottom);
        const float center_x = client.width * 0.5f - tip.width * 0.5f + m.left - m.right;
        const float center_y = client.height * 0.5f - tip.height * 0.5f + m.top - m.bottom;
        switch (spot_for(placement_, alignment_))
        {
            case P::Auto:
            case P::Bottom:
                y = far_y, x = center_x;
                break;
            case P::Top:
                y = near_y, x = center_x;
                break;
            case P::Left:
                y = center_y, x = near_x;
                break;
            case P::Right:
                y = center_y, x = far_x;
                break;
            case P::TopRight:
            case P::RightTop:
                y = near_y, x = far_x;
                break;
            case P::TopLeft:
            case P::LeftTop:
                y = near_y, x = near_x;
                break;
            case P::BottomRight:
            case P::RightBottom:
                y = far_y, x = far_x;
                break;
            case P::BottomLeft:
            case P::LeftBottom:
                y = far_y, x = near_x;
                break;
            case P::Center:
                y = center_y, x = center_x;
                break;
        }
    }
    content.arrange(Rect{x, y, tip.width, tip.height});
    return true;
}

Size TeachingTipControl::measure_override(Size /*available*/, const MeasureContext&) const
{
    return {}; // the card lives in a popup, so the control itself takes no space
}

void TeachingTipControl::arrange_override(const Rect& /*final_rect*/, const ArrangeContext&)
{
}

void TeachingTipControl::render_override(Renderer& /*renderer*/, const RenderContext&) const
{
}

void TeachingTipControl::clear_dirty_override(DirtyFlags flags)
{
    for (Element* e : {content_.get(),
                       hero_.get(),
                       static_cast<Element*>(action_button_.get()),
                       static_cast<Element*>(close_button_.get()),
                       static_cast<Element*>(alternate_close_.get())})
    {
        if (e != nullptr)
        {
            clear_dirty_of(*e, flags);
        }
    }
}

void TeachingTipControl::tick_override(Duration delta)
{
    if (!open_)
    {
        return;
    }
    for (Element* e : {content_.get(),
                       hero_.get(),
                       static_cast<Element*>(action_button_.get()),
                       static_cast<Element*>(close_button_.get()),
                       static_cast<Element*>(alternate_close_.get())})
    {
        if (e != nullptr)
        {
            e->tick(delta);
        }
    }
}

std::unique_ptr<ui::Element> TeachingTip::build() &&
{
    auto node = std::make_unique<ui::TeachingTipControl>();
    apply(title, *node, [raw = node.get()](const std::string& value) { raw->set_title(value); });
    apply(subtitle, *node, [raw = node.get()](const std::string& value) { raw->set_subtitle(value); });
    apply(icon, *node, [raw = node.get()](const IconSource& v) { raw->set_icon(v); });
    apply(action_button_text, *node, [raw = node.get()](const std::string& v) { raw->set_action_button_text(v); });
    apply(close_button_text, *node, [raw = node.get()](const std::string& v) { raw->set_close_button_text(v); });
    apply(placement, *node, [raw = node.get()](const std::optional<PopupPlacement>& v) { raw->set_placement(v); });
    apply(placement_alignment,
          *node,
          [raw = node.get()](const PopupPlacementAlignment& v) { raw->set_placement_alignment(v); });
    apply(placement_margin, *node, [raw = node.get()](const Thickness& v) { raw->set_placement_margin(v); });
    apply(hero_content_placement,
          *node,
          [raw = node.get()](const TeachingTipHeroPlacement& v) { raw->set_hero_content_placement(v); });
    apply(tail_visibility, *node, [raw = node.get()](const AutoVisibility& v) { raw->set_tail_visibility(v); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    if (content)
    {
        node->set_content(content.take());
    }
    if (hero_content)
    {
        node->set_hero_content(hero_content.take());
    }
    apply(light_dismiss, *node, [raw = node.get()](const bool& value) { raw->set_light_dismiss(value); });
    // A two-way `.is_open` follows the control: the user opening or closing it writes the property.
    const auto is_open_writer = ui::shared_writer(is_open);
    apply(is_open,
          *node,
          [raw = node.get(), is_open_writer](const bool& value)
          {
              raw->set_open(value);
              // on_closing canceled the close the property asked for: put the property back.
              if (raw->is_open() != value && is_open_writer)
              {
                  (*is_open_writer)(raw->is_open());
              }
          });

    if (on_action_button_click)
    {
        node->on_action_button_click(std::move(on_action_button_click));
    }
    if (on_close_button_click)
    {
        node->on_close_button_click(std::move(on_close_button_click));
    }
    target.resolve(*node, [raw = node.get()](Element* to) { raw->set_target(to); });
    if (auto handler = ui::write_back_value<bool>(is_open_writer, true, std::move(on_opened)))
    {
        node->on_opened(std::move(handler));
    }
    if (on_closing)
    {
        node->on_closing(std::move(on_closing));
    }
    if (auto handler = ui::write_back_value<bool>(is_open_writer, false, std::move(on_closed)))
    {
        node->on_closed(std::move(handler));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
