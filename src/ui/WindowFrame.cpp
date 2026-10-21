// SPDX-License-Identifier: MIT
#include "WindowFrame.hpp"

#include "WindowHost.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Strings.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowStyle.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui::detail
{
namespace
{

using FrameKind = ControlChrome::WindowFrame::Kind;

// The classic caption glyphs, Marlett's `0` `1` `2` `r` as USER32 draws them into a 16x14 button
// at 96 DPI: one string per pixel row, `#` set. Each sits at its offset inside the button.
struct FrameGlyph
{
    float                             left;
    float                             top;
    std::span<const std::string_view> rows;
};

constexpr std::array<std::string_view, 2> MinimizeRows{
    "######",
    "######",
};
constexpr std::array<std::string_view, 9> MaximizeRows{
    "#########",
    "#########",
    "#.......#",
    "#.......#",
    "#.......#",
    "#.......#",
    "#.......#",
    "#.......#",
    "#########",
};
constexpr std::array<std::string_view, 9> RestoreRows{
    "..######",
    "..######",
    "..#....#",
    "######.#",
    "######.#",
    "#....###",
    "#....#..",
    "#....#..",
    "######..",
};
// Luna's white caption glyphs, each in the 13 x 13 cell centered in a 21 x 21 button.
constexpr std::array<std::string_view, 3> LunaMinimizeRows{
    "#######",
    "#######",
    "#######",
};
constexpr std::array<std::string_view, 11> LunaMaximizeRows{
    "###########",
    "###########",
    "###########",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "#.........#",
    "###########",
};
constexpr std::array<std::string_view, 10> LunaRestoreRows{
    "...#######",
    "...#######",
    ".........#",
    "#######..#",
    "#######..#",
    "#.....#..#",
    "#.....#.##",
    "#.....#...",
    "#.....#...",
    "#######...",
};
constexpr std::array<std::string_view, 11> LunaCloseRows{
    "##.......##",
    "###.....###",
    ".###...###.",
    "..###.###..",
    "...#####...",
    "....###....",
    "...#####...",
    "..###.###..",
    ".###...###.",
    "###.....###",
    "##.......##",
};

// Aero Basic's caption glyphs: `#` the outline, `o` the fill, anything else left clear.
constexpr std::array<std::string_view, 6> AeroMinimizeRows{
    ".###########.",
    "#ooooooooooo#",
    "#ooooooooooo#",
    "#ooooooooooo#",
    "#ooooooooooo#",
    ".###########.",
};
constexpr std::array<std::string_view, 12> AeroMaximizeRows{
    ".###########.",
    "#ooooooooooo#",
    "#ooooooooooo#",
    "#ooooooooooo#",
    "#ooo#####ooo#",
    "#ooo#...#ooo#",
    "#ooo#...#ooo#",
    "#ooo#####ooo#",
    "#ooooooooooo#",
    "#ooooooooooo#",
    "#ooooooooooo#",
    ".###########.",
};
constexpr std::array<std::string_view, 15> AeroRestoreRows{
    ".....#########.",
    "....#ooooooooo#",
    "....#ooooooooo#",
    "....#oo#####oo#",
    ".###########oo#",
    "#ooooooooo##oo#",
    "#ooooooooo##oo#",
    "#oo#####oo##oo#",
    "#oo#...#oo#ooo#",
    "#oo#...#oo#ooo#",
    "#oo#...#oo#####",
    "#oo#####oo#....",
    "#ooooooooo#....",
    "#ooooooooo#....",
    ".#########.....",
};
constexpr std::array<std::string_view, 12> AeroCloseRows{
    ".###.....###.",
    "#ooo#...#ooo#",
    "#oooo#.#oooo#",
    ".#oooo#oooo#.",
    "..#ooooooo#..",
    "...#ooooo#...",
    "...#ooooo#...",
    "..#ooooooo#..",
    ".#oooo#oooo#.",
    "#oooo#.#oooo#",
    "#ooo#...#ooo#",
    ".###.....###.",
};

// Windows 8's caption glyphs at 100 %, each at its offset in its cell; close is the classic cross.
constexpr std::array<std::string_view, 2> MdlMinimizeRows{
    "########",
    "########",
};
constexpr std::array<std::string_view, 8> MdlMaximizeRows{
    "##########",
    "##########",
    "#........#",
    "#........#",
    "#........#",
    "#........#",
    "#........#",
    "##########",
};
constexpr std::array<std::string_view, 10> MdlRestoreRows{
    "..########",
    ".........#",
    "########.#",
    "########.#",
    "#......#.#",
    "#......#.#",
    "#......#.#",
    "#......#.#",
    "#......#..",
    "########..",
};

void fill_px(Renderer& renderer, float x, float y, float w, float h, const Color& color);

// The glass title's glow: rings of faint copies of the title, each a radius and a count, and the
// distance over which a ring's alpha falls to 1/e of the glow color's.
constexpr std::array<std::pair<float, int>, 4> GlowRings{{{1.0f, 8}, {2.0f, 12}, {3.5f, 16}, {5.0f, 20}}};
constexpr float                                GlowFalloff = 3.0f;

// One run per row for each of the two inks.
void fill_glyph2(Renderer& renderer, std::span<const std::string_view> rows, Point origin, const Color& outline, const Color& fill)
{
    for (std::size_t y = 0; y < rows.size(); ++y)
    {
        const std::string_view row = rows[y];
        std::size_t            x   = 0;
        while (x < row.size())
        {
            const char ink = row[x];
            if (ink != '#' && ink != 'o')
            {
                ++x;
                continue;
            }
            const std::size_t start = x;
            while (x < row.size() && row[x] == ink)
            {
                ++x;
            }
            fill_px(renderer,
                    origin.x + static_cast<float>(start),
                    origin.y + static_cast<float>(y),
                    static_cast<float>(x - start),
                    1.0f,
                    ink == '#' ? outline : fill);
        }
    }
}

// `brush`, a ramp with one stop per row of `rows`, without its first `cut` rows.
LinearGradientBrush without_top_rows(const LinearGradientBrush& brush, float rows, float cut)
{
    if (cut <= 0.0f)
    {
        return brush;
    }
    LinearGradientBrush out{{}, brush.axis};
    for (const GradientStop& stop : brush.stops)
    {
        const float row = stop.offset * rows;
        if (row >= cut)
        {
            out.stops.push_back(GradientStop{(row - cut) / (rows - cut), stop.color});
        }
    }
    return out;
}

constexpr std::array<std::string_view, 7> CloseRows{
    "##....##",
    ".##..##.",
    "..####..",
    "...##...",
    "..####..",
    ".##..##.",
    "##....##",
};

void fill_px(Renderer& renderer, float x, float y, float w, float h, const Color& color)
{
    renderer.fill_rounded_rect(Rect{x, y, w, h}, 0.0f, color);
}

void fill_glyph(Renderer& renderer, const FrameGlyph& glyph, Point origin, const Color& color)
{
    for (std::size_t y = 0; y < glyph.rows.size(); ++y)
    {
        const std::string_view row = glyph.rows[y];
        // One rectangle per run of set pixels.
        std::size_t x = 0;
        while (x < row.size())
        {
            if (row[x] != '#')
            {
                ++x;
                continue;
            }
            const std::size_t start = x;
            while (x < row.size() && row[x] == '#')
            {
                ++x;
            }
            fill_px(renderer,
                    origin.x + glyph.left + static_cast<float>(start),
                    origin.y + glyph.top + static_cast<float>(y),
                    static_cast<float>(x - start),
                    1.0f,
                    color);
        }
    }
}

// The band `thickness` wide just inside `outer`, as four rectangles, so what it surrounds is left
// to whatever draws there.
void fill_ring(Renderer& renderer, const Rect& outer, float thickness, const Color& color)
{
    if (thickness <= 0.0f)
    {
        return;
    }
    fill_px(renderer, outer.x, outer.y, outer.width, thickness, color);
    fill_px(renderer, outer.x, outer.bottom() - thickness, outer.width, thickness, color);
    fill_px(renderer, outer.x, outer.y + thickness, thickness, outer.height - 2.0f * thickness, color);
    fill_px(renderer, outer.right() - thickness, outer.y + thickness, thickness, outer.height - 2.0f * thickness, color);
}

// A 1 px ring just inside `outer` whose top corners follow `cut` (the pixels missing from each top
// corner, a row at a time): the row under a step runs back to the step above it.
void fill_cut_ring(Renderer& renderer, const Rect& outer, std::span<const float> cut, const Color& color)
{
    const float rows = static_cast<float>(cut.size());
    if (cut.empty() || outer.height <= rows)
    {
        fill_ring(renderer, outer, 1.0f, color);
        return;
    }
    fill_px(renderer, outer.x + cut[0], outer.y, outer.width - 2.0f * cut[0], 1.0f, color);
    for (std::size_t y = 1; y < cut.size(); ++y)
    {
        const float run = std::max(1.0f, cut[y - 1] - cut[y]);
        fill_px(renderer, outer.x + cut[y], outer.y + static_cast<float>(y), run, 1.0f, color);
        fill_px(renderer, outer.right() - cut[y] - run, outer.y + static_cast<float>(y), run, 1.0f, color);
    }
    // Below the steps the sides are straight, meeting the last step diagonally.
    const float top = outer.y + rows;
    fill_px(renderer, outer.x, top, 1.0f, outer.height - rows - 1.0f, color);
    fill_px(renderer, outer.right() - 1.0f, top, 1.0f, outer.height - rows - 1.0f, color);
    fill_px(renderer, outer.x, outer.bottom() - 1.0f, outer.width, 1.0f, color);
}

// The cut of the ring just inside the one fill_cut_ring() draws with `cut`: each row starts where
// the outer ring's row below its own ends.
std::vector<float> inner_cut(std::span<const float> cut)
{
    std::vector<float> inner;
    for (std::size_t y = 1; y < cut.size(); ++y)
    {
        const float outer_end = cut[y] + std::max(1.0f, cut[y - 1] - cut[y]);
        inner.push_back(std::max(0.0f, outer_end - 1.0f));
    }
    while (!inner.empty() && inner.back() <= 0.0f)
    {
        inner.pop_back();
    }
    return inner;
}

// A caption side edge (Luna), one column at a time as a gradient with a stop per caption row, the
// rows' positions taken from the caption brush. Nothing when the edge has a row count of its own.
void paint_caption_edge(Renderer&                                      renderer,
                        const LinearGradientBrush&                     caption_brush,
                        const ControlChrome::WindowFrame::CaptionEdge& edge,
                        const Rect&                                    caption,
                        bool                                           right)
{
    const std::size_t rows = caption_brush.stops.size();
    if (edge.columns[0].size() != rows || edge.columns[1].size() != rows)
    {
        return;
    }
    const auto column = [&](std::size_t x, auto&& color_of_row)
    {
        LinearGradientBrush brush{{}, GradientAxis::Vertical};
        for (std::size_t y = 0; y < rows; ++y)
        {
            brush.stops.push_back(GradientStop{caption_brush.stops[y].offset, color_of_row(y)});
        }
        const float left = right ? caption.right() - 1.0f - static_cast<float>(x) : caption.x + static_cast<float>(x);
        renderer.fill_rounded_rect_gradient(Rect{left, caption.y, 1.0f, caption.height}, 0.0f, brush);
    };
    for (std::size_t x = 0; x < 2; ++x)
    {
        column(x, [&](std::size_t y) { return edge.columns[x][y]; });
    }
    for (std::size_t i = 0; i < edge.fade.size(); ++i)
    {
        column(2 + i, [&](std::size_t y) { return lerp(caption_brush.stops[y].color, edge.columns[1][y], edge.fade[i]); });
    }
}

// A corner's art, a pixel at a time, its transparent pixels left out.
void paint_corner(Renderer& renderer, const ControlChrome::WindowFrame::CornerArt& art, float left, float top)
{
    for (std::size_t i = 0; art.width > 0 && i < art.pixels.size(); ++i)
    {
        if (art.pixels[i].a > 0.0f)
        {
            const auto width = static_cast<std::size_t>(art.width);
            fill_px(renderer,
                    left + static_cast<float>(i % width),
                    top + static_cast<float>(i / width),
                    1.0f,
                    1.0f,
                    art.pixels[i]);
        }
    }
}

// A set of pixels, drawn as one rectangle per horizontal run.
class Pixels
{
public:
    void set(int x, int y)
    {
        points_.emplace_back(y, x);
    }
    void row(int x0, int x1, int y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            set(x, y);
        }
    }
    // The 1 px drop shadow of the Windows 2.x caption art: every pixel right of `axis` again one
    // to the right, and the lowest row again one down and one right.
    [[nodiscard]] Pixels with_shadow(int axis) const
    {
        Pixels out    = *this;
        int    bottom = 0;
        for (const auto& [y, x] : points_)
        {
            bottom = std::max(bottom, y);
        }
        for (const auto& [y, x] : points_)
        {
            if (x > axis)
            {
                out.set(x + 1, y);
            }
            if (y == bottom)
            {
                out.set(x + 1, y + 1);
            }
        }
        return out;
    }
    // The same pixels upside down within rows `top` to `bottom`.
    [[nodiscard]] Pixels flipped(int top, int bottom) const
    {
        Pixels out;
        for (const auto& [y, x] : points_)
        {
            out.set(x, top + bottom - y);
        }
        return out;
    }
    void draw(Renderer& renderer, Point origin, const Color& color) const
    {
        std::vector<std::pair<int, int>> sorted = points_;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        std::size_t i = 0;
        while (i < sorted.size())
        {
            const auto [y, x0] = sorted[i];
            int x1             = x0;
            while (i + 1 < sorted.size() && sorted[i + 1].first == y && sorted[i + 1].second == x1 + 1)
            {
                ++i;
                ++x1;
            }
            fill_px(renderer,
                    origin.x + static_cast<float>(x0),
                    origin.y + static_cast<float>(y),
                    static_cast<float>(x1 - x0 + 1),
                    1.0f,
                    color);
            ++i;
        }
    }

private:
    std::vector<std::pair<int, int>> points_; // (y, x)
};

// The Windows 2.x up arrow, an outline: a head `half` pixels wide either side of `cx` rising from
// row `top`, on a stem `stem` pixels either side reaching row `bottom`.
Pixels outline_up_arrow(int cx, int top, int bottom, int half, int stem)
{
    Pixels p;
    for (int i = 0; i < half; ++i)
    {
        p.set(cx - i, top + i);
        p.set(cx + i, top + i);
    }
    const int base = top + half;
    p.row(cx - half, cx - stem, base);
    p.row(cx + stem, cx + half, base);
    for (int y = base + 1; y < bottom; ++y)
    {
        p.set(cx - stem, y);
        p.set(cx + stem, y);
    }
    p.row(cx - stem, cx + stem, bottom);
    return p;
}

// A caption button as UI Automation sees it: a Button the frame positions and invokes, never
// drawn by itself.
class CaptionButtonElement final : public Element, private InvokeProvider
{
public:
    CaptionButtonElement(WindowFrameElement& frame, Window& window, WindowFrameElement::Part part) :
        frame_(frame),
        window_(window),
        part_(part)
    {
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Button;
    }

protected:
    Size measure_override(Size /*available*/, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer& /*renderer*/, const RenderContext&) const override
    {
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        switch (part_)
        {
            case WindowFrameElement::Part::Minimize:
                return strings().window_frame.minimize;
            case WindowFrameElement::Part::Maximize:
                return window_.state().get() == WindowState::Maximized
                           ? strings().window_frame.restore
                           : strings().window_frame.maximize;
            case WindowFrameElement::Part::Close:
                return strings().window_frame.close;
            case WindowFrameElement::Part::Icon:
            case WindowFrameElement::Part::None:
                break;
        }
        return {};
    }

private:
    WindowFrameElement&      frame_;
    Window&                  window_;
    WindowFrameElement::Part part_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke = this;
    }
    bool invoke() override
    {
        return frame_.invoke(part_);
    }
};

// The caption as UI Automation sees it: a TitleBar named after the window, holding the buttons.
class TitleBarElement final : public Element
{
public:
    TitleBarElement(const WindowFrameElement& frame, const Window& window) : frame_(frame), window_(window)
    {
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::TitleBar;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        frame_.automation_buttons(out);
    }

protected:
    Size measure_override(Size /*available*/, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer& /*renderer*/, const RenderContext&) const override
    {
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return window_.title();
    }

private:
    const WindowFrameElement& frame_;
    const Window&             window_;
};

constexpr std::array<WindowFrameElement::Part, 3> ButtonParts{WindowFrameElement::Part::Minimize,
                                                              WindowFrameElement::Part::Maximize,
                                                              WindowFrameElement::Part::Close};

} // namespace

WindowFrameElement::WindowFrameElement(Window& window) :
    window_(window),
    title_bar_(std::make_unique<TitleBarElement>(*this, window))
{
    for (std::size_t i = 0; i < buttons_.size(); ++i)
    {
        buttons_[i] = std::make_unique<CaptionButtonElement>(*this, window_, ButtonParts[i]);
        set_parent_of(*buttons_[i], title_bar_.get());
    }
    add_subscription(window_.active().observe([this](const bool&) { mark_dirty(DirtyFlags::Visual); }));
    // Maximized drops the border, full screen the whole frame: the content area moves.
    add_subscription(
        window_.state().observe([this](const WindowState&) { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); }));
}

void WindowFrameElement::set_chrome(const ControlChrome::WindowFrame& chrome)
{
    if (chrome_ == chrome)
    {
        return;
    }
    chrome_ = chrome;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

bool WindowFrameElement::fullscreen() const
{
    return window_.state().get() == WindowState::Fullscreen;
}

float WindowFrameElement::border() const
{
    const WindowState state = window_.state().get();
    return state == WindowState::Maximized || state == WindowState::Fullscreen ? 0.0f : chrome_.border;
}

bool WindowFrameElement::caption_carries_top() const
{
    return chrome_.kind == FrameKind::Luna || chrome_.kind == FrameKind::AeroBasic || chrome_.kind == FrameKind::Metro;
}

float WindowFrameElement::top_cut() const
{
    return caption_carries_top() ? chrome_.border - border() : 0.0f;
}

Rect WindowFrameElement::caption_rect() const
{
    if (fullscreen())
    {
        return {};
    }
    // Luna's and Aero Basic's caption art runs the full width and carries the top of the frame.
    if (caption_carries_top())
    {
        return Rect{bounds().x, bounds().y, bounds().width, chrome_.caption_height - top_cut()};
    }
    const float b = border();
    return Rect{bounds().x + b, bounds().y + b, std::max(0.0f, bounds().width - 2.0f * b), chrome_.caption_height};
}

Rect WindowFrameElement::title_bar() const
{
    const Rect caption = caption_rect();
    if (!caption_carries_top())
    {
        return caption;
    }
    const float height = chrome_.caption_height - chrome_.border;
    return Rect{caption.x, caption.bottom() - height, caption.width, height};
}

Rect WindowFrameElement::content_rect(const Rect& client) const
{
    if (fullscreen())
    {
        return client;
    }
    const float b   = border();
    const float top = caption_carries_top() ? chrome_.caption_height - top_cut()
                                            : b + chrome_.caption_height + chrome_.caption_rule;
    return Rect{client.x + b, client.y + top, std::max(0.0f, client.width - 2.0f * b), std::max(0.0f, client.height - top - b)};
}

Rect WindowFrameElement::part_rect(Part part) const
{
    const Rect         caption = caption_rect();
    const WindowStyle& style   = window_.style();
    if (caption.width <= 0.0f || !style.caption_buttons)
    {
        return {};
    }
    const Size size = chrome_.button;

    if (chrome_.kind != FrameKind::Classic && !caption_carries_top())
    {
        // The Control-menu box on the left; minimize and maximize at the right, each only when the
        // window has it, and no close box until Windows 95.
        const bool buttons  = chrome_.kind != FrameKind::Win1x;
        const bool maximize = buttons && style.maximize_button;
        const bool minimize = buttons && style.minimize_button;
        const Rect right{caption.right() - size.width, caption.y, size.width, size.height};
        switch (part)
        {
            case Part::Icon:
                return Rect{caption.x, caption.y, chrome_.menu_box_width, caption.height};
            case Part::Maximize:
                return maximize ? right : Rect{};
            case Part::Minimize:
                return minimize ? (maximize ? Rect{right.x - size.width, right.y, size.width, size.height} : right)
                                : Rect{};
            case Part::Close:
            case Part::None:
                break;
        }
        return {};
    }

    const float top         = std::max(caption.y, caption.y + chrome_.button_top - top_cut());
    const float close_width = chrome_.close_width > 0.0f ? chrome_.close_width : size.width;
    const Rect  close{caption.right() - chrome_.button_right - close_width, top, close_width, size.height};
    // Minimize and maximize come as a pair: shown while either is wanted, the other then disabled.
    const bool  pair           = style.minimize_button || style.maximize_button;
    const float minimize_width = chrome_.minimize_width > 0.0f ? chrome_.minimize_width : size.width;
    const Rect  maximize{close.x - chrome_.close_gap - size.width, top, size.width, size.height};
    const Rect  minimize{maximize.x - chrome_.button_gap - minimize_width, top, minimize_width, size.height};
    switch (part)
    {
        case Part::Close:
            return close;
        case Part::Maximize:
            return pair ? maximize : Rect{};
        case Part::Minimize:
            return pair ? minimize : Rect{};
        case Part::Icon:
        {
            // The window's own picture or glyph, else the icon the host reported (a resource
            // icon, or the platform's default).
            const std::optional<WindowIcon>& icon = window_.icon();
            const bool                       own  = icon && (!icon->image.empty() || icon->glyph.codepoint != 0);
            if (!style.caption_icon || (!own && testing::seams(window_).caption_icon().empty()))
            {
                return {};
            }
            const Rect bar = title_bar();
            return Rect{caption.x + chrome_.icon_left,
                        bar.y + std::round((bar.height - chrome_.icon_size) * 0.5f),
                        chrome_.icon_size,
                        chrome_.icon_size};
        }
        case Part::None:
            break;
    }
    return {};
}

bool WindowFrameElement::part_enabled(Part part) const
{
    const WindowStyle& style = window_.style();
    switch (part)
    {
        case Part::Minimize:
            return style.minimize_button;
        case Part::Maximize:
            return style.maximize_button;
        case Part::Close:
            return style.closable;
        case Part::Icon:
            return true;
        case Part::None:
            break;
    }
    return false;
}

WindowFrameElement::Part WindowFrameElement::part_at(Point p) const
{
    for (const Part part : {Part::Close, Part::Maximize, Part::Minimize, Part::Icon})
    {
        const Rect r = part_rect(part);
        if (r.width > 0.0f && r.contains(p))
        {
            return part;
        }
    }
    return Part::None;
}

HitRegion WindowFrameElement::hit_region(Point p) const
{
    const Part part = part_at(p);
    if (part != Part::None && !part_enabled(part))
    {
        return HitRegion::Client; // a disabled button neither drags nor offers Snap Layouts
    }
    switch (part)
    {
        case Part::Icon:
            return HitRegion::SystemMenu;
        case Part::Minimize:
            return HitRegion::MinimizeButton;
        case Part::Maximize:
            return HitRegion::MaximizeButton;
        case Part::Close:
            return HitRegion::CloseButton;
        case Part::None:
            break;
    }
    return HitRegion::Caption;
}

Size WindowFrameElement::measure_override(Size available, const MeasureContext&) const
{
    return available;
}

TextStyle WindowFrameElement::title_style(bool active) const
{
    TextStyle style;
    style.font_family = chrome_.font_family;
    style.font_size   = chrome_.font_size;
    style.weight      = chrome_.font_weight;
    style.horizontal  = chrome_.title_centered ? TextAlign::Center : TextAlign::Leading;
    style.vertical    = TextAlign::Center;
    if (active && chrome_.active_text_shadow)
    {
        // The shadow pass a pixel down and right, then the title over it.
        style.effect = TextEffect{TextEffectKind::Emboss, *chrome_.active_text_shadow, chrome_.active_text, 1.0f, 1.0f};
    }
    return style;
}

bool WindowFrameElement::pointer_override(const PointerEvent& e)
{
    const Part under  = part_at(e.position);
    const Part target = under != Part::None && under != Part::Icon && part_enabled(under) ? under : Part::None;
    switch (e.action)
    {
        case PointerAction::Move:
            if (hovered_ != target)
            {
                hovered_ = target;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Leave:
            if (hovered_ != Part::None)
            {
                hovered_ = Part::None;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && target != Part::None)
            {
                pressed_ = target;
                hovered_ = target;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_ != Part::None)
            {
                const Part pressed = std::exchange(pressed_, Part::None);
                mark_dirty(DirtyFlags::Visual);
                if (pressed == target)
                {
                    click(pressed);
                }
            }
            break;
        case PointerAction::Wheel:
            break;
    }
    return false;
}

void WindowFrameElement::arrange_override(const Rect& /*final_rect*/, const ArrangeContext&)
{
    title_bar_->arrange(caption_rect());
    for (std::size_t i = 0; i < buttons_.size(); ++i)
    {
        buttons_[i]->arrange(part_rect(ButtonParts[i]));
    }
}

void WindowFrameElement::automation_buttons(std::vector<Element*>& out) const
{
    for (std::size_t i = 0; i < buttons_.size(); ++i)
    {
        if (part_rect(ButtonParts[i]).width > 0.0f)
        {
            out.push_back(buttons_[i].get());
        }
    }
}

bool WindowFrameElement::invoke(Part part)
{
    if (part == Part::Icon || part_rect(part).width <= 0.0f || !part_enabled(part))
    {
        return false;
    }
    click(part);
    return true;
}

void WindowFrameElement::click(Part part)
{
    switch (part)
    {
        case Part::Minimize:
            window_.request_state(WindowState::Minimized);
            break;
        case Part::Maximize:
            if (window_.state().get() == WindowState::Maximized)
            {
                window_.restore();
            }
            else
            {
                window_.request_state(WindowState::Maximized);
            }
            break;
        case Part::Close:
            window_.close();
            break;
        case Part::Icon:
        case Part::None:
            break;
    }
}

void WindowFrameElement::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (fullscreen())
    {
        return;
    }
    const bool  active       = window_.active().get();
    const Color border_color = active ? chrome_.active_border : chrome_.inactive_border;
    const float b            = border();
    const bool  classic      = chrome_.kind == FrameKind::Classic;
    const bool  painted      = caption_carries_top(); // painted strips around a full-width caption
    const bool  drawn_icon   = classic || painted;    // the window's own icon, not a Control-menu box
    const bool  glassy       = glass();

    // The frame. Classic: the border color under the raised window edge (DrawEdge's EDGE_RAISED,
    // not the soft button arrangement). Windows 1.x to 3.x: the border color between two frame
    // lines, or a single line in 1.x.
    if (glassy)
    {
        render_glass(renderer, active);
    }
    else if (b > 0.0f)
    {
        if (painted)
        {
            // The painted strips, under the caption: left and right a column at a time, then the
            // bottom a row at a time across both.
            const std::vector<Color>& left   = active ? chrome_.active_left : chrome_.inactive_left;
            const std::vector<Color>& right  = active ? chrome_.active_right : chrome_.inactive_right;
            const std::vector<Color>& bottom = active ? chrome_.active_bottom : chrome_.inactive_bottom;
            const float               top    = bounds().y + chrome_.caption_height;
            const float               height = std::max(0.0f, bounds().height - chrome_.caption_height);
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                fill_px(renderer, bounds().x + static_cast<float>(i), top, 1.0f, height, left[i]);
            }
            for (std::size_t i = 0; i < right.size(); ++i)
            {
                fill_px(renderer, bounds().right() - b + static_cast<float>(i), top, 1.0f, height, right[i]);
            }
            for (std::size_t i = 0; i < bottom.size(); ++i)
            {
                fill_px(renderer, bounds().x, bounds().bottom() - b + static_cast<float>(i), bounds().width, 1.0f, bottom[i]);
            }
        }
        else if (classic)
        {
            fill_ring(renderer, bounds(), b, border_color);
            draw_bevel_border(renderer,
                              bounds(),
                              BorderStyle::Raised,
                              chrome_.bevel_light,
                              chrome_.bevel_highlight,
                              chrome_.bevel_shadow,
                              chrome_.bevel_dark_shadow,
                              1.0f);
        }
        else
        {
            fill_ring(renderer, bounds(), 1.0f, chrome_.frame_line);
            if (b > 2.0f)
            {
                fill_ring(renderer, bounds().inflate(-1.0f), b - 2.0f, border_color);
                fill_ring(renderer, bounds().inflate(-(b - 1.0f)), 1.0f, chrome_.frame_line);
            }
            else if (b > 1.0f)
            {
                fill_ring(renderer, bounds().inflate(-1.0f), b - 1.0f, chrome_.frame_line);
            }
            if (chrome_.kind == FrameKind::Win3x)
            {
                // The sizing notches: a frame line across the border where each corner's sizing
                // zone ends, level with the caption's bottom edge.
                const float d = b + chrome_.caption_height;
                fill_px(renderer, bounds().x + d, bounds().y, 1.0f, b, chrome_.frame_line);
                fill_px(renderer, bounds().right() - d - 1.0f, bounds().y, 1.0f, b, chrome_.frame_line);
                fill_px(renderer, bounds().x + d, bounds().bottom() - b, 1.0f, b, chrome_.frame_line);
                fill_px(renderer, bounds().right() - d - 1.0f, bounds().bottom() - b, 1.0f, b, chrome_.frame_line);
                fill_px(renderer, bounds().x, bounds().y + d, b, 1.0f, chrome_.frame_line);
                fill_px(renderer, bounds().x, bounds().bottom() - d - 1.0f, b, 1.0f, chrome_.frame_line);
                fill_px(renderer, bounds().right() - b, bounds().y + d, b, 1.0f, chrome_.frame_line);
                fill_px(renderer, bounds().right() - b, bounds().bottom() - d - 1.0f, b, 1.0f, chrome_.frame_line);
            }
        }
    }

    const Rect caption = caption_rect();
    if (!glassy)
    {
        renderer.fill_rounded_rect_gradient(caption,
                                            0.0f,
                                            without_top_rows(active ? chrome_.active_caption : chrome_.inactive_caption,
                                                             chrome_.caption_height,
                                                             top_cut()));
        fill_px(renderer,
                caption.x,
                caption.bottom(),
                caption.width,
                chrome_.caption_rule,
                classic ? border_color : chrome_.frame_line);
    }

    // Aero Basic's caption outline and highlight run down its sides into the strips.
    if (chrome_.kind == FrameKind::AeroBasic && b > 0.0f && !glassy)
    {
        const LinearGradientBrush& ramp  = active ? chrome_.active_caption : chrome_.inactive_caption;
        const std::vector<Color>&  right = active ? chrome_.active_right : chrome_.inactive_right;
        fill_px(renderer, caption.x, caption.y, 1.0f, caption.height, ramp.stops[0].color);
        fill_px(renderer, caption.x + 1.0f, caption.y + 1.0f, 1.0f, caption.height - 1.0f, ramp.stops[1].color);
        fill_px(renderer, caption.right() - 1.0f, caption.y, 1.0f, caption.height, ramp.stops[0].color);
        fill_px(renderer, caption.right() - 2.0f, caption.y + 1.0f, 1.0f, caption.height - 1.0f, right[right.size() - 2]);
    }

    // Luna's caption shades its own sides, which the side strips below it continue.
    if (chrome_.kind == FrameKind::Luna && b > 0.0f)
    {
        const LinearGradientBrush& brush = active ? chrome_.active_caption : chrome_.inactive_caption;
        paint_caption_edge(renderer, brush, active ? chrome_.active_caption_left : chrome_.inactive_caption_left, caption, false);
        paint_caption_edge(renderer, brush, active ? chrome_.active_caption_right : chrome_.inactive_caption_right, caption, true);
    }

    // The rounded top corners: the art's own pixels over the caption's rows and sides. The host cuts
    // the transparent ones out of the window (Window::frame_corner_cut()).
    if (!glassy && !corner_cut().empty())
    {
        const auto& left  = active ? chrome_.active_top_left : chrome_.inactive_top_left;
        const auto& right = active ? chrome_.active_top_right : chrome_.inactive_top_right;
        paint_corner(renderer, left, bounds().x, bounds().y);
        paint_corner(renderer, right, bounds().right() - static_cast<float>(right.width), bounds().y);
    }

    // Windows 8's darker outer edge runs down the caption's sides into the strips.
    if (chrome_.kind == FrameKind::Metro && b > 0.0f)
    {
        const Color edge = (active ? chrome_.active_left : chrome_.inactive_left).front();
        fill_px(renderer, caption.x, caption.y, 2.0f, caption.height, edge);
        fill_px(renderer, caption.right() - 2.0f, caption.y, 2.0f, caption.height, edge);
    }

    // The icon or the Control-menu box, then the title from beside it to the first button.
    const Rect icon = part_rect(Part::Icon);
    if (icon.width > 0.0f)
    {
        if (drawn_icon)
        {
            const std::optional<WindowIcon>& source = window_.icon();
            if (source && !source->image.empty())
            {
                renderer.draw_image(source->image, icon, Stretch::Uniform);
            }
            else if (source && source->glyph.codepoint != 0)
            {
                if (source->background.a > 0.0f)
                {
                    renderer.fill_rounded_rect(icon, 0.0f, source->background);
                }
                // With no color of its own, the glyph takes the title's.
                const Color title_ink = active ? chrome_.active_text : chrome_.inactive_text;
                draw_icon(renderer, source->glyph, icon, source->foreground.value_or(title_ink), chrome_.icon_size * 0.75f);
            }
            else
            {
                renderer.draw_image(testing::seams(window_).caption_icon(), icon, Stretch::Uniform);
            }
        }
        else
        {
            render_menu_box(renderer, icon);
        }
    }
    const Rect  first   = part_rect(Part::Minimize).width > 0.0f   ? part_rect(Part::Minimize)
                          : part_rect(Part::Maximize).width > 0.0f ? part_rect(Part::Maximize)
                                                                   : part_rect(Part::Close);
    const float title_x = icon.width > 0.0f ? icon.right() + (drawn_icon ? chrome_.title_left : 1.0f)
                                            : caption.x + chrome_.title_left;
    const float title_r = first.width > 0.0f ? first.x - (drawn_icon ? 2.0f : 0.0f) : caption.right() - 2.0f;
    if (title_r > title_x && !window_.title().empty() && window_.style().caption_title)
    {
        const TextStyle style = title_style(active);
        const Rect      bar   = title_bar();
        const Rect      title{title_x, bar.y, title_r - title_x, bar.height};
        // On glass the title stays black when inactive and is smoothed in grayscale, glow and
        // all: ClearType over translucent pixels leaves colored fringes.
        const bool grayscale = glassy && theme.rasterization.text == TextAA::Subpixel;
        if (grayscale)
        {
            SetRasterization policy = theme.rasterization;
            policy.text             = TextAA::Grayscale;
            renderer.set_rasterization(policy);
        }
        if (glassy)
        {
            // The glow. Windows 7 blurred a white copy of the title behind it, so it hugs the
            // letters and fades within a few pixels. Without a blur, the copy is drawn on rings
            // round the title, each pass faint and fainter further out.
            renderer.push_clip(caption);
            const Color glow = chrome_.glass->glow;
            for (const auto& [radius, count] : GlowRings)
            {
                const float alpha = glow.a * std::exp(-(radius * radius) / (GlowFalloff * GlowFalloff));
                for (int i = 0; i < count; ++i)
                {
                    const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(count);
                    const Rect  at{title.x + radius * std::cos(angle),
                                   title.y + radius * std::sin(angle),
                                   title.width,
                                   title.height};
                    renderer.draw_text(window_.title(), at, glow.with_alpha(alpha), style);
                }
            }
            renderer.pop_clip();
        }
        renderer.push_clip(title);
        renderer.draw_text(window_.title(), title, active || glassy ? chrome_.active_text : chrome_.inactive_text, style);
        renderer.pop_clip();
        if (grayscale)
        {
            renderer.set_rasterization(theme.rasterization);
        }
    }

    for (const Part part : {Part::Minimize, Part::Maximize, Part::Close})
    {
        switch (chrome_.kind)
        {
            case FrameKind::Classic:
                render_button(renderer, part);
                break;
            case FrameKind::Win3x:
                render_win3x_button(renderer, part);
                break;
            case FrameKind::Win2x:
                render_win2x_button(renderer, part);
                break;
            case FrameKind::Luna:
                render_luna_button(renderer, part, active);
                break;
            case FrameKind::AeroBasic:
                render_aero_button(renderer, part, active);
                break;
            case FrameKind::Metro:
                render_mdl_button(renderer, part, active);
                break;
            case FrameKind::Win1x:
                break;
        }
    }
}

const ControlChrome::WindowFrame::Face& WindowFrameElement::face(Part part, bool active) const
{
    // Normal, hot, pressed or disabled, from the inactive set on an inactive window.
    std::size_t state = 0;
    if (!part_enabled(part))
    {
        state = 3;
    }
    else if (pressed_ == part && hovered_ == part)
    {
        state = 2;
    }
    else if (hovered_ == part && pressed_ == Part::None)
    {
        state = 1;
    }
    const auto& faces = part == Part::Close ? chrome_.close_faces : chrome_.caption_faces;
    return faces[state + (active ? 0 : 4)];
}

void WindowFrameElement::render_aero_button(Renderer& renderer, Part part, bool active) const
{
    const Rect r = part_rect(part);
    if (r.width <= 0.0f)
    {
        return;
    }
    // The ramp runs from the top outline to the bottom one; the sides take the outline and, just
    // inside it, the lighter edge.
    const auto& f = face(part, active);
    renderer.fill_rounded_rect_gradient(r, 0.0f, f.fill);
    fill_px(renderer, r.x, r.y, 1.0f, r.height, f.outline);
    fill_px(renderer, r.right() - 1.0f, r.y, 1.0f, r.height, f.outline);
    fill_px(renderer, r.x + 1.0f, r.y + 1.0f, 1.0f, r.height - 2.0f, f.inner);
    fill_px(renderer, r.right() - 2.0f, r.y + 1.0f, 1.0f, r.height - 2.0f, f.inner);

    switch (part)
    {
        case Part::Minimize:
            fill_glyph2(renderer, AeroMinimizeRows, Point{r.x + 7.0f, r.y + 10.0f}, chrome_.glyph_outline, f.glyph);
            break;
        case Part::Maximize:
            if (window_.state().get() == WindowState::Maximized)
            {
                fill_glyph2(renderer, AeroRestoreRows, Point{r.x + 6.0f, r.y + 2.0f}, chrome_.glyph_outline, f.glyph);
            }
            else
            {
                fill_glyph2(renderer, AeroMaximizeRows, Point{r.x + 7.0f, r.y + 4.0f}, chrome_.glyph_outline, f.glyph);
            }
            break;
        case Part::Close:
            fill_glyph2(renderer,
                        AeroCloseRows,
                        Point{r.x + std::floor((r.width - 13.0f) * 0.5f) - 1.0f, r.y + 4.0f},
                        chrome_.glyph_outline,
                        f.glyph);
            break;
        case Part::Icon:
        case Part::None:
            break;
    }
}

std::vector<float> WindowFrameElement::corner_cut() const
{
    if (border() <= 0.0f)
    {
        return {};
    }
    return {chrome_.top_corner_cut.begin(), chrome_.top_corner_cut.end()};
}

bool WindowFrameElement::glass() const
{
    return chrome_.glass.has_value() && testing::seams(window_).blur_behind();
}

void WindowFrameElement::render_glass(Renderer& renderer, bool active) const
{
    const auto& g       = *chrome_.glass;
    const Rect  content = content_rect(bounds());

    // The glass is everything but the rounded corners' cut pixels, which nothing paints: there is
    // no region on glass, so they show the bare blur. One band per stepped row, then the rest.
    const std::vector<float> cut = corner_cut();
    std::vector<Rect>        bands;
    for (std::size_t y = 0; y < cut.size(); ++y)
    {
        bands.push_back(Rect{bounds().x + cut[y], bounds().y + static_cast<float>(y), bounds().width - 2.0f * cut[y], 1.0f});
    }
    const float stepped = static_cast<float>(cut.size());
    bands.push_back(Rect{bounds().x, bounds().y + stepped, bounds().width, std::max(0.0f, bounds().height - stepped)});

    // The tint over the frame; the content area is filled opaque after it.
    for (const Rect& band : bands)
    {
        fill_px(renderer, band.x, band.y, band.width, band.height, active ? g.active_tint : g.inactive_tint);
    }

    // The reflection: three broad streaks slanting down to the left, brightest on the left.
    const float slant = bounds().height * 0.55f;
    for (const Rect& band : bands)
    {
        renderer.push_clip(band);
        for (const auto& [at, width, share] :
             {std::tuple{0.12f, 46.0f, 1.0f}, std::tuple{0.24f, 18.0f, 0.7f}, std::tuple{0.62f, 70.0f, 0.5f}})
        {
            const float x = bounds().x + bounds().width * at;
            FillPolygon streak;
            streak.points = {Point{x, bounds().y},
                             Point{x + width, bounds().y},
                             Point{x + width - slant, bounds().bottom()},
                             Point{x - slant, bounds().bottom()}};
            streak.fill   = g.streak.with_alpha(g.streak.a * share * (active ? 1.0f : 0.6f));
            renderer.fill_polygon(streak);
        }
        renderer.pop_clip();
    }

    fill_px(renderer, content.x, content.y, content.width, content.height, window_.background());

    // The outline and its highlight while there is a border, and the line round the content.
    if (border() > 0.0f)
    {
        fill_cut_ring(renderer, bounds(), cut, g.outline);
        fill_cut_ring(renderer, bounds().inflate(-1.0f), inner_cut(cut), g.highlight);
    }
    fill_ring(renderer, content.inflate(1.0f), 1.0f, g.content_edge);
}

void WindowFrameElement::render_mdl_button(Renderer& renderer, Part part, bool active) const
{
    const Rect r = part_rect(part);
    if (r.width <= 0.0f)
    {
        return;
    }
    // A flat fill and the glyph centered on it, in its cell from the art.
    const auto& f = face(part, active);
    if (f.fill.stops.front().color.a > 0.0f)
    {
        renderer.fill_rounded_rect_gradient(r, 0.0f, f.fill);
    }
    FrameGlyph glyph{};
    Size       cell{};
    switch (part)
    {
        case Part::Minimize:
            glyph = {3.0f, 9.0f, MdlMinimizeRows};
            cell  = {14.0f, 14.0f};
            break;
        case Part::Maximize:
            glyph = window_.state().get() == WindowState::Maximized
                        ? FrameGlyph{2.0f, 2.0f, MdlRestoreRows}
                        : FrameGlyph{4.0f, 3.0f, MdlMaximizeRows};
            cell  = window_.state().get() == WindowState::Maximized ? Size{14.0f, 14.0f} : Size{16.0f, 14.0f};
            break;
        case Part::Close:
            glyph = {3.0f, 4.0f, CloseRows};
            cell  = {13.0f, 14.0f};
            break;
        case Part::Icon:
        case Part::None:
            return;
    }
    const Point origin{r.x + std::floor((r.width - cell.width) * 0.5f), r.y + std::floor((r.height - cell.height) * 0.5f)};
    fill_glyph(renderer, glyph, origin, f.glyph);
}

void WindowFrameElement::render_luna_button(Renderer& renderer, Part part, bool active) const
{
    const Rect r = part_rect(part);
    if (r.width <= 0.0f)
    {
        return;
    }
    const auto& face = this->face(part, active);
    renderer.fill_rounded_rect_gradient(r.inflate(-1.0f), 2.0f, face.fill);
    renderer.stroke_rounded_rect(r.inflate(-0.5f), 3.0f, face.outline, 1.0f);

    FrameGlyph glyph{};
    switch (part)
    {
        case Part::Minimize:
            glyph = {5.0f, 13.0f, LunaMinimizeRows};
            break;
        case Part::Maximize:
            glyph = window_.state().get() == WindowState::Maximized
                        ? FrameGlyph{5.0f, 5.0f, LunaRestoreRows}
                        : FrameGlyph{5.0f, 5.0f, LunaMaximizeRows};
            break;
        case Part::Close:
            glyph = {5.0f, 5.0f, LunaCloseRows};
            break;
        case Part::Icon:
        case Part::None:
            return;
    }
    fill_glyph(renderer, glyph, Point{r.x, r.y}, face.glyph);
}

void WindowFrameElement::render_menu_box(Renderer& renderer, const Rect& box) const
{
    // The box, then the frame line that closes it off from the title.
    fill_px(renderer, box.x, box.y, box.width, box.height, chrome_.button_face);
    fill_px(renderer, box.right(), box.y, 1.0f, box.height, chrome_.frame_line);
    const float x = box.x;
    switch (chrome_.kind)
    {
        case FrameKind::Win3x:
        {
            // A 13 x 3 bar: frame-line outline, highlight inside, a shadow below and to the right.
            const float y = box.y + std::floor((box.height - 4.0f) * 0.5f);
            fill_px(renderer, x + 2.0f, y, 13.0f, 3.0f, chrome_.frame_line);
            fill_px(renderer, x + 3.0f, y + 1.0f, 11.0f, 1.0f, chrome_.bevel_highlight);
            fill_px(renderer, x + 15.0f, y + 1.0f, 1.0f, 3.0f, chrome_.bevel_shadow);
            fill_px(renderer, x + 3.0f, y + 3.0f, 12.0f, 1.0f, chrome_.bevel_shadow);
            break;
        }
        case FrameKind::Win2x:
        {
            // A hollow 20 x 4 bar with the art's drop shadow.
            const int top = static_cast<int>(std::floor((box.height - 5.0f) * 0.5f));
            Pixels    bar;
            bar.row(2, 21, top);
            bar.row(2, 21, top + 3);
            bar.set(2, top + 1);
            bar.set(2, top + 2);
            bar.set(21, top + 1);
            bar.set(21, top + 2);
            bar.with_shadow(2).draw(renderer, Point{x, box.y}, chrome_.glyph);
            break;
        }
        case FrameKind::Win1x:
        {
            // Three 9 x 2 bars.
            const float y = box.y + std::floor((box.height - 10.0f) * 0.5f);
            for (int i = 0; i < 3; ++i)
            {
                fill_px(renderer, x + 2.0f, y + 4.0f * static_cast<float>(i), 9.0f, 2.0f, chrome_.glyph);
            }
            break;
        }
        case FrameKind::Classic:
        case FrameKind::Luna:
        case FrameKind::AeroBasic:
        case FrameKind::Metro:
            break;
    }
}

void WindowFrameElement::render_win3x_button(Renderer& renderer, Part part) const
{
    const Rect r = part_rect(part);
    if (r.width <= 0.0f)
    {
        return;
    }
    const bool  pushed = pressed_ == part && hovered_ == part;
    const float x      = r.x;
    const float y      = r.y;
    const float w      = r.width;
    const float h      = r.height;
    // The frame line on its left, the face, and the era's 1 px highlight over a 2 px shadow; held,
    // a 1 px shadow on the top and left only.
    fill_px(renderer, x, y, 1.0f, h, chrome_.frame_line);
    fill_px(renderer, x + 1.0f, y, w - 1.0f, h, chrome_.button_face);
    if (pushed)
    {
        fill_px(renderer, x + 1.0f, y, w - 1.0f, 1.0f, chrome_.bevel_shadow);
        fill_px(renderer, x + 1.0f, y + 1.0f, 1.0f, h - 1.0f, chrome_.bevel_shadow);
    }
    else
    {
        fill_px(renderer, x + 1.0f, y, w - 2.0f, 1.0f, chrome_.bevel_highlight);
        fill_px(renderer, x + 1.0f, y + 1.0f, 1.0f, h - 2.0f, chrome_.bevel_highlight);
        fill_px(renderer, x + w - 1.0f, y, 1.0f, h, chrome_.bevel_shadow);
        fill_px(renderer, x + w - 2.0f, y + 1.0f, 1.0f, h - 1.0f, chrome_.bevel_shadow);
        fill_px(renderer, x + 2.0f, y + h - 2.0f, w - 2.0f, 1.0f, chrome_.bevel_shadow);
        fill_px(renderer, x + 1.0f, y + h - 1.0f, w - 1.0f, 1.0f, chrome_.bevel_shadow);
    }

    // Solid triangles 7 pixels wide, down for minimize, up for maximize, both for restore; a held
    // button's glyph moves one right and two down.
    const float gx       = x + 6.0f + (pushed ? 1.0f : 0.0f);
    const float gy       = y + (pushed ? 2.0f : 0.0f);
    const auto  triangle = [&](float top, bool up)
    {
        for (int i = 0; i < 4; ++i)
        {
            const int width = up ? 1 + 2 * i : 7 - 2 * i;
            fill_px(renderer,
                    gx + static_cast<float>((7 - width) / 2),
                    gy + top + static_cast<float>(i),
                    static_cast<float>(width),
                    1.0f,
                    chrome_.frame_line);
        }
    };
    if (part == Part::Minimize)
    {
        triangle(7.0f, false);
    }
    else if (window_.state().get() == WindowState::Maximized)
    {
        triangle(4.0f, true);
        triangle(10.0f, false);
    }
    else
    {
        triangle(6.0f, true);
    }
}

void WindowFrameElement::render_win2x_button(Renderer& renderer, Part part) const
{
    const Rect r = part_rect(part);
    if (r.width <= 0.0f)
    {
        return;
    }
    // Held, the box shows inverted; the era's art has no pressed state of its own.
    const bool  pushed = pressed_ == part && hovered_ == part;
    const Color paper  = pushed ? chrome_.glyph : chrome_.button_face;
    const Color ink    = pushed ? chrome_.button_face : chrome_.glyph;
    fill_px(renderer, r.x, r.y, 1.0f, r.height, chrome_.frame_line);
    fill_px(renderer, r.x + 1.0f, r.y, r.width - 1.0f, r.height, paper);

    // Outline arrows with the art's drop shadow, centered in a 25 x 19 box.
    const Point origin{r.x + 1.0f, r.y + std::floor((r.height - 19.0f) * 0.5f)};
    if (part == Part::Minimize)
    {
        outline_up_arrow(12, 2, 15, 6, 2).flipped(2, 15).with_shadow(12).draw(renderer, origin, ink);
    }
    else if (window_.state().get() == WindowState::Maximized)
    {
        outline_up_arrow(7, 2, 15, 4, 1).flipped(2, 15).with_shadow(7).draw(renderer, origin, ink);
        outline_up_arrow(17, 2, 15, 4, 1).with_shadow(17).draw(renderer, origin, ink);
    }
    else
    {
        outline_up_arrow(12, 2, 15, 6, 2).with_shadow(12).draw(renderer, origin, ink);
    }
}

void WindowFrameElement::render_button(Renderer& renderer, Part part) const
{
    const Rect r = part_rect(part);
    if (r.width <= 0.0f)
    {
        return;
    }
    // Drawn pushed while the press that started on it is still over it, as a held button is.
    const bool pushed = pressed_ == part && hovered_ == part;
    renderer.fill_rounded_rect(r, 0.0f, chrome_.button_face);
    // DrawFrameControl(DFC_CAPTION): a soft raised push button, sunken while held.
    draw_bevel_border(renderer,
                      r,
                      pushed ? BorderStyle::Sunken : BorderStyle::Raised,
                      chrome_.bevel_light,
                      chrome_.bevel_highlight,
                      chrome_.bevel_shadow,
                      chrome_.bevel_dark_shadow,
                      1.0f,
                      BevelSides::All,
                      !pushed);

    FrameGlyph glyph{};
    switch (part)
    {
        case Part::Minimize:
            glyph = {4.0f, 9.0f, MinimizeRows};
            break;
        case Part::Maximize:
            glyph = window_.state().get() == WindowState::Maximized
                        ? FrameGlyph{3.0f, 2.0f, RestoreRows}
                        : FrameGlyph{3.0f, 2.0f, MaximizeRows};
            break;
        case Part::Close:
            glyph = {4.0f, 3.0f, CloseRows};
            break;
        case Part::Icon:
        case Part::None:
            return;
    }
    // A held button's glyph moves down and right by a pixel with the face.
    const Point origin{r.x + (pushed ? 1.0f : 0.0f), r.y + (pushed ? 1.0f : 0.0f)};
    if (part_enabled(part))
    {
        fill_glyph(renderer, glyph, origin, chrome_.glyph);
    }
    else
    {
        // DrawState's DSS_DISABLED: embossed, the highlight a pixel down and right of the shadow.
        fill_glyph(renderer, glyph, Point{origin.x + 1.0f, origin.y + 1.0f}, chrome_.glyph_disabled);
        fill_glyph(renderer, glyph, origin, chrome_.glyph_disabled_shadow);
    }
}

} // namespace ui::detail
