// SPDX-License-Identifier: MIT
// libwinui ships no media, so every picture on these pages is drawn in code, sized like the
// asset it replaces so the samples lay out the same.
#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cstdint>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- drawing into an Image ----

void paint(ui::Bitmap& image, int x, int y, ui::Color color, float coverage)
{
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height() || coverage <= 0.0f)
    {
        return;
    }
    image.blend(x, y, color, static_cast<std::uint8_t>(std::lround(std::clamp(coverage, 0.0f, 1.0f) * 255.0f)));
}

void fill_ellipse(ui::Bitmap& image, float cx, float cy, float rx, float ry, ui::Color color)
{
    const int x0 = static_cast<int>(std::floor(cx - rx - 1.0f));
    const int x1 = static_cast<int>(std::ceil(cx + rx + 1.0f));
    const int y0 = static_cast<int>(std::floor(cy - ry - 1.0f));
    const int y1 = static_cast<int>(std::ceil(cy + ry + 1.0f));
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const float dx = (static_cast<float>(x) + 0.5f - cx) / rx;
            const float dy = (static_cast<float>(y) + 0.5f - cy) / ry;
            const float d  = std::sqrt(dx * dx + dy * dy);
            paint(image, x, y, color, (1.0f - d) * std::min(rx, ry) + 0.5f);
        }
    }
}

void fill_rect(ui::Bitmap& image, int x, int y, int w, int h, ui::Color color)
{
    for (int yy = y; yy < y + h; ++yy)
    {
        for (int xx = x; xx < x + w; ++xx)
        {
            paint(image, xx, yy, color, 1.0f);
        }
    }
}

// ---- AnimatedVisualPlayer ----

// A stand-in logo animation, 375x667 and 5.967 s long. It is a
// pure function of progress, as a source must be.
class LogoVisual final : public ui::AnimatedVisualSource
{
public:
    [[nodiscard]] ui::Size natural_size() const override
    {
        return ui::Size{375.0f, 667.0f};
    }
    [[nodiscard]] ui::Duration duration() const override
    {
        return ui::Duration{5967.0};
    }

    void render_frame(ui::Renderer& renderer, const ui::Rect& bounds, double progress) const override
    {
        const float scale = bounds.width / 375.0f;
        const auto  at    = [&](float x, float y) { return ui::Point{bounds.x + x * scale, bounds.y + y * scale}; };
        const float t     = static_cast<float>(std::clamp(progress, 0.0, 1.0));
        const float fade  = 1.0f - phase(t, 0.86f, 1.0f);
        if (fade <= 0.0f)
        {
            return;
        }
        constexpr ui::Point Center{187.5f, 300.0f};

        const float swell = ease_out_back(phase(t, 0.0f, 0.18f));
        const float split = ease_in_out(phase(t, 0.18f, 0.42f));
        if (split <= 0.0f)
        {
            const float r = 64.0f * swell * scale;
            if (r > 0.0f)
            {
                const ui::Point c = at(Center.x, Center.y);
                renderer.fill_ellipse(c, r, r, with_alpha(Tiles[0], fade));
            }
        }
        else
        {
            constexpr std::array<ui::Point, 4>
                Corners{ui::Point{-1.0f, -1.0f}, ui::Point{1.0f, -1.0f}, ui::Point{-1.0f, 1.0f}, ui::Point{1.0f, 1.0f}};
            const float size   = (40.0f + 18.0f * split) * scale;
            const float spread = 36.0f * split;
            for (std::size_t i = 0; i < Corners.size(); ++i)
            {
                const ui::Point c     = at(Center.x + Corners[i].x * spread, Center.y + Corners[i].y * spread);
                const ui::Color color = mix(Tiles[0], Tiles[i], split);
                renderer.fill_rounded_rect(ui::Rect{c.x - size * 0.5f, c.y - size * 0.5f, size, size},
                                           (1.0f - split) * size * 0.5f + split * 8.0f * scale,
                                           with_alpha(color, fade));
            }
        }

        const float ring = ease_in_out(phase(t, 0.36f, 0.64f));
        if (ring > 0.0f)
        {
            constexpr int Segments = 96;
            const int     drawn    = static_cast<int>(std::ceil(ring * Segments));
            const float   radius   = 104.0f;
            for (int i = 0; i < drawn; ++i)
            {
                const float a0 = angle(static_cast<float>(i) / Segments);
                const float a1 = angle(std::min(ring, static_cast<float>(i + 1) / Segments));
                renderer.stroke_line(at(Center.x + radius * std::sin(a0), Center.y - radius * std::cos(a0)),
                                     at(Center.x + radius * std::sin(a1), Center.y - radius * std::cos(a1)),
                                     with_alpha(Tiles[0], fade),
                                     6.0f * scale);
            }
        }

        const float typing = phase(t, 0.55f, 0.78f);
        if (typing > 0.0f)
        {
            constexpr std::string_view Name  = "libwinui";
            const std::size_t          shown = static_cast<std::size_t>(std::ceil(typing * Name.size()));
            ui::TextStyle              style;
            style.font_size  = 44.0f * scale;
            style.weight     = ui::FontWeight::SemiBold;
            style.horizontal = ui::TextAlign::Center;
            // Centered across the whole frame, so it lines up whatever the font's width.
            const ui::Point origin = at(0.0f, 440.0f);
            renderer.draw_text(Name.substr(0, shown),
                               ui::Rect{origin.x, origin.y, 375.0f * scale, 60.0f * scale},
                               with_alpha(Tiles[0], fade),
                               style);
        }
    }

private:
    static constexpr std::array<ui::Color, 4> Tiles{ui::Color::from_rgba(0, 120, 212),
                                                    ui::Color::from_rgba(0, 178, 148),
                                                    ui::Color::from_rgba(16, 124, 16),
                                                    ui::Color::from_rgba(255, 185, 0)};

    static float phase(float t, float from, float to)
    {
        return std::clamp((t - from) / (to - from), 0.0f, 1.0f);
    }
    static float ease_in_out(float x)
    {
        return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) * 0.5f;
    }
    static float ease_out_back(float x)
    {
        constexpr float C1 = 1.70158f;
        constexpr float C3 = C1 + 1.0f;
        return 1.0f + C3 * std::pow(x - 1.0f, 3.0f) + C1 * std::pow(x - 1.0f, 2.0f);
    }
    static float angle(float turn)
    {
        return turn * 2.0f * std::numbers::pi_v<float>;
    }
    static ui::Color with_alpha(ui::Color c, float alpha)
    {
        c.a *= alpha;
        return c;
    }
    static ui::Color mix(ui::Color a, ui::Color b, float t)
    {
        return ui::Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }
};

const LogoVisual& logo_visual()
{
    static const LogoVisual visual;
    return visual;
}

// Segoe Fluent Icons codepoints of the sample's Symbol values, a Symbol's 20px.
constexpr ui::IconGlyph PlayGlyph{0xE768, "Segoe Fluent Icons", 20.0f};     // Symbol.Play
constexpr ui::IconGlyph PauseGlyph{0xE769, "Segoe Fluent Icons", 20.0f};    // Symbol.Pause
constexpr ui::IconGlyph StopGlyph{0xE71A, "Segoe Fluent Icons", 20.0f};     // Symbol.Stop
constexpr ui::IconGlyph PreviousGlyph{0xE892, "Segoe Fluent Icons", 20.0f}; // Symbol.Previous

constexpr const char* PlayerIntro =
    "This AnimatedVisualPlayer plays an AnimatedVisualSource: a class that draws the animation's "
    "frame for any progress between 0 and 1, through the same renderer every control draws with. "
    "libwinui has no Lottie pipeline, so this logo animation is drawn in code. When its source "
    "cannot be shown, the AnimatedVisualPlayer shows its fallback content instead.";

struct PlayerState
{
    ui::Control<ui::AnimatedVisualPlayer> player;
    ui::Control<ui::ToggleButton>         pause;
};

// Resume a paused play, or start one when nothing is playing.
void ensure_playing(PlayerState& s)
{
    if (s.pause->checked() == true)
    {
        s.pause->set_checked(false); // its unchecked handler resumes
    }
    else if (!s.player->is_playing())
    {
        s.player->play(0.0, 1.0, /*looped=*/false);
    }
}

ui::Spec tool_tipped(const char* tip, ui::Spec control)
{
    return ui::ToolTip{.anchor = std::move(control), .content = ui::Text{.text = tip}};
}

ui::Spec animated_visual_player_page(Context&)
{
    return with_state<PlayerState>(
        [](PlayerState& s)
        {
            // A 400x400 frame, 20 above and below, in the card background under a 1px card stroke.
            std::unique_ptr<ui::Element> frame = themed_box(BoxStyle::Surface,
                                                            ui::Thickness{1.0f},
                                                            ui::AnimatedVisualPlayer{.source    = &logo_visual(),
                                                                                     .auto_play = false,
                                                                                     .control   = s.player});
            frame->set_width(400.0f);
            frame->set_height(400.0f);
            frame->set_margin(ui::Symmetric{.horizontal = 0.0f, .vertical = 20.0f});

            return examples({
                example({
                    .title = "Playback of an animated visual.",
                    .code  = R"cpp(
                        // LogoVisual derives from ui::AnimatedVisualSource: natural_size(), duration()
                        // and render_frame(), which draws the frame for a progress from 0 to 1.
                        static const LogoVisual logo;

                        ui::Control<ui::AnimatedVisualPlayer> player;
                        ui::Control<ui::ToggleButton>         pause;

                        ui::VerticalStack{
                            .content = {
                                ui::AnimatedVisualPlayer{.source = &logo, .auto_play = false, .control = player},
                                ui::HorizontalStack{
                                    .spacing = 8,
                                    .content = {
                                        ui::Button{.text     = "Play",
                                                   .on_click = [&]
                                                   {
                                                       player->set_playback_rate(1.0);
                                                       player->play(0.0, 1.0, /*looped=*/false);
                                                   }},
                                        ui::ToggleButton{.text               = "Pause",
                                                         .on_checked_changed = [&](std::optional<bool> on)
                                                         { on == true ? player->pause() : player->resume(); },
                                                         .control            = pause},
                                        ui::Button{.text     = "Stop",
                                                   .on_click = [&]
                                                   {
                                                       player->stop();
                                                       pause->set_checked(false);
                                                   }},
                                        ui::Button{.text     = "Reverse",
                                                   .on_click = [&]
                                                   {
                                                       player->set_playback_rate(-1.0);
                                                       player->play(0.0, 1.0, /*looped=*/false);
                                                   }},
                                    },
                                },
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::VerticalStack{
                                .horizontal_alignment = ui::Alignment::Center,
                                .content =
                                    {
                                        ui::Text{.text = PlayerIntro, .wrapping = ui::TextWrapping::WrapWholeWords},
                                        std::move(frame),
                                        ui::Grid{
                                            .columns        = {1_fr, 1_fr, 1_fr, 1_fr},
                                            .rows           = {1_fr},
                                            .column_spacing = 8.0f,
                                            .margin         = ui::Thickness{12.0f},
                                            .width          = 400.0f,
                                            .vertical_alignment        = ui::Alignment::Center,
                                            .content =
                                                {
                                                    ui::Cell{.column  = std::size_t{0},
                                                     .content = tool_tipped("Play",
                                                                            ui::Button{.accessibility = {.name = "Play"},
                                                                                       .on_click =
                                                                                           [&s]
                                                                                       {
                                                                                           s.player->set_playback_rate(1.0);
                                                                                           ensure_playing(s);
                                                                                       },
                                                                                       .content = ui::Icon{.source = PlayGlyph}})},
                                                    ui::Cell{.column  = std::size_t{1},
                                                     .content = tool_tipped(
                                                         "Pause",
                                                         ui::ToggleButton{.accessibility      = {.name = "Pause"},
                                                                          .on_checked_changed = [&s](std::optional<bool> on)
                                                                          { on == true ? s.player->pause() : s.player->resume(); },
                                                                          .control                = s.pause,
                                                                          .content            = ui::Icon{.source = PauseGlyph}})},
                                                    ui::Cell{.column  = std::size_t{2},
                                                     .content = tool_tipped("Stop",
                                                                            ui::Button{.accessibility = {.name = "Stop"},
                                                                                       .on_click =
                                                                                           [&s]
                                                                                       {
                                                                                           s.player->stop();
                                                                                           s.pause->set_checked(false);
                                                                                       },
                                                                                       .content = ui::Icon{.source = StopGlyph}})},
                                                    ui::Cell{.column  = std::size_t{3},
                                                     .content = tool_tipped("Reverse",
                                                                            ui::Button{.accessibility = {.name = "Reverse"},
                                                                                       .on_click =
                                                                                           [&s]
                                                                                       {
                                                                                           s.player->set_playback_rate(-1.0);
                                                                                           ensure_playing(s);
                                                                                       },
                                                                                       .content = ui::Icon{.source = PreviousGlyph}})},
                                                },
                                        },
                                    },
                            },
                        },
                }),
            });
        });
}

// ---- Image ----

// ninegrid.gif's stand-in: 82x82, a 3x3 grid of numbered white cells ruled with 3px gray lines.
// A nine_grid of 3,3,3,3 keeps just the outer frame crisp; 30,20,30,20 cuts through the cells.
const ui::Bitmap& nine_grid_picture()
{
    static const ui::Bitmap picture = []
    {
        static constexpr std::array<std::array<const char*, 7>, 9> Digits{{
            {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
            {"01110", "10001", "00001", "00010", "00100", "01000", "11111"},
            {"11110", "00001", "00001", "01110", "00001", "00001", "11110"},
            {"00010", "00110", "01010", "10010", "11111", "00010", "00010"},
            {"11111", "10000", "11110", "00001", "00001", "10001", "01110"},
            {"00110", "01000", "10000", "11110", "10001", "10001", "01110"},
            {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
            {"01110", "10001", "10001", "01110", "10001", "10001", "01110"},
            {"01110", "10001", "10001", "01111", "00001", "00010", "01100"},
        }};
        constexpr std::array<int, 4> Lines{0, 26, 53, 79}; // 3px rules; cells 23-24px between them
        ui::Bitmap                   img(82, 82, ui::colors::White);
        const ui::Color              rule = ui::Color::from_rgba(128, 128, 128);
        for (int line : Lines)
        {
            fill_rect(img, line, 0, 3, 82, rule);
            fill_rect(img, 0, line, 82, 3, rule);
        }
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const auto& glyph = Digits[(static_cast<std::size_t>(row) * 3) + static_cast<std::size_t>(col)];
                const int   left  = Lines[static_cast<std::size_t>(col)] + 3;
                const int   top   = Lines[static_cast<std::size_t>(row)] + 3;
                const int   cell  = Lines[static_cast<std::size_t>(col) + 1] - left;
                const int   x0    = left + (cell - 10) / 2;
                const int   y0    = top + (cell - 14) / 2;
                for (int gy = 0; gy < 7; ++gy)
                {
                    for (int gx = 0; gx < 5; ++gx)
                    {
                        if (glyph[static_cast<std::size_t>(gy)][gx] == '1')
                        {
                            fill_rect(img, x0 + gx * 2, y0 + gy * 2, 2, 2, ui::colors::Black);
                        }
                    }
                }
            }
        }
        return img;
    }();
    return picture;
}

// animated.gif's stand-in: an 88x31 bevelled web badge, three bars with a highlight sweeping
// along them and a spinning globe, 30 frames of 100 ms, 1000 loops like the original's.
const ui::AnimatedImage& web_badge()
{
    static const ui::AnimatedImage badge = []
    {
        constexpr int     Frames = 30;
        ui::AnimatedImage animation;
        animation.loop_count = 1000;
        for (int f = 0; f < Frames; ++f)
        {
            ui::Bitmap img(88, 31, ui::Color::from_rgba(192, 192, 192));
            fill_rect(img, 0, 0, 88, 1, ui::colors::White);
            fill_rect(img, 0, 0, 1, 31, ui::colors::White);
            fill_rect(img, 0, 30, 88, 1, ui::Color::from_rgba(64, 64, 64));
            fill_rect(img, 87, 0, 1, 31, ui::Color::from_rgba(64, 64, 64));

            const float phase = static_cast<float>(f) / Frames;
            const int   sweep = static_cast<int>(-14.0f + phase * 70.0f);
            for (int bar = 0; bar < 3; ++bar)
            {
                const int y = 7 + bar * 6;
                for (int x = 5; x < 55; ++x)
                {
                    const bool lit = x >= sweep + bar * 4 && x < sweep + bar * 4 + 12;
                    fill_rect(img, x, y, 1, 4, lit ? ui::Color::from_rgba(255, 215, 0) : ui::Color::from_rgba(0, 0, 128));
                }
            }

            constexpr float Cx = 71.0f, Cy = 15.5f, R = 11.0f;
            fill_ellipse(img, Cx, Cy, R, R, ui::Color::from_rgba(0, 90, 200));
            ui::Bitmap land(88, 31);
            for (int k = 0; k < 2; ++k)
            {
                const float drift = std::fmod(phase + static_cast<float>(k) * 0.5f, 1.0f);
                const float x     = Cx - R - 6.0f + drift * (2.0f * R + 12.0f);
                fill_ellipse(land, x, Cy - 3.0f + static_cast<float>(k) * 6.0f, 6.0f, 4.0f, ui::Color::from_rgba(40, 170, 60));
            }
            for (int y = 0; y < 31; ++y)
            {
                for (int x = 60; x < 83; ++x)
                {
                    const float     dx = static_cast<float>(x) + 0.5f - Cx;
                    const float     dy = static_cast<float>(y) + 0.5f - Cy;
                    const ui::Color c  = land.get(x, y);
                    paint(img,
                          x,
                          y,
                          ui::Color{c.r, c.g, c.b, 1.0f},
                          c.a * std::clamp(R - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f));
                }
            }
            animation.frames.push_back(std::move(img));
            animation.delays.push_back(ui::Duration{100.0});
        }
        return animation;
    }();
    return badge;
}

ui::AnimatedImage paused_badge()
{
    ui::AnimatedImage copy = web_badge();
    copy.auto_play         = false;
    return copy;
}

// A laptop mirroring its screen to a phone, 240x120.
constexpr const char* DevicesSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="240" height="120" viewBox="0 0 240 120">
  <rect x="20" y="14" width="124" height="80" rx="6" fill="#2B579A"/>
  <rect x="28" y="22" width="108" height="64" rx="2" fill="#DEECF9"/>
  <rect x="6" y="96" width="152" height="8" rx="4" fill="#5C6B7A"/>
  <circle cx="82" cy="54" r="16" fill="#0078D4"/>
  <path d="M76 46 L90 54 L76 62 Z" fill="#FFFFFF"/>
  <rect x="180" y="26" width="50" height="88" rx="8" fill="#2B579A"/>
  <rect x="185" y="36" width="40" height="64" rx="2" fill="#DEECF9"/>
  <circle cx="205" cy="68" r="10" fill="#0078D4"/>
  <path d="M201 63 L210 68 L201 73 Z" fill="#FFFFFF"/>
  <path d="M150 40 Q162 30 174 40" stroke="#0078D4" stroke-width="3" fill="none" stroke-linecap="round"/>
  <path d="M154 48 Q162 42 170 48" stroke="#0078D4" stroke-width="3" fill="none" stroke-linecap="round"/>
  <circle cx="162" cy="55" r="3" fill="#0078D4"/>
</svg>)";

const ui::SvgImage& devices_svg()
{
    static const ui::SvgImage svg{DevicesSvg};
    return svg;
}

constexpr std::array<ui::Stretch, 4> Stretches{ui::Stretch::None, ui::Stretch::Fill, ui::Stretch::Uniform, ui::Stretch::UniformToFill};

struct ImageState
{
    ui::Property<int>      stretch{0};
    ui::Control<ui::Image> click_to_play;
};

ui::Spec image_page(Context&)
{
    return with_state<ImageState>(
        [](ImageState& s)
        {
            return examples({
                example({
                    .title    = "A basic image.",
                    .code     = R"cpp(
                        const ui::Bitmap photo = ui::Bitmap::read("photo.jpg");

                        ui::Image{.source = photo, .height = 100}
                    )cpp",
                    .controls = {ui::Image{.source = sample_photo(5), .height = 100.0f}},
                }),
                example({
                    .title = "An image decoded to the rendering size",
                    .code  = R"cpp(
                        // Decoded no taller than 100 pixels, so the full picture is never held.
                        const ui::Bitmap photo = ui::Bitmap::read("photo.jpg", {.max_height = 100});

                        ui::Image{.source = photo, .height = 100}

                        // A bitmap that is already in memory is made smaller with scaled_to_fit().
                        ui::Image{.source = photo.scaled_to_fit(0, 100), .height = 100}
                    )cpp",
                    // The bitmap itself is made 100 pixels
                    // tall, which Bitmap::decode()'s max_height does when reading a file.
                    .controls = {ui::Image{.source = sample_photo(5).scaled_to_fit(0, 100), .height = 100.0f}},
                }),
                example({
                    .title   = "Image stretching.",
                    .code    = R"cpp(
                        constexpr std::array<ui::Stretch, 4> Stretches{ui::Stretch::None,
                                                                       ui::Stretch::Fill,
                                                                       ui::Stretch::Uniform,
                                                                       ui::Stretch::UniformToFill};
                        ui::Property<std::optional<std::size_t>> stretch{0};

                        ui::RadioButtons{.header               = "Image stretch mode",
                                         .items                = {"None", "Fill", "Uniform", "UniformToFill"},
                                         .selected_index       = stretch}

                        ui::Image{.source  = photo,
                                  .stretch = ui::bind(stretch, [](std::optional<std::size_t> i) { return Stretches[i.value_or(0)]; }),
                                  .width   = 100,
                                  .height  = 100}
                    )cpp",
                    .options = ui::RadioButtons{.header               = "Image stretch mode",
                                                .items                = {"None", "Fill", "Uniform", "UniformToFill"},
                                                .selected_index       = 0,
                                                .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                { s.stretch = static_cast<int>(index.value_or(0)); }},
                    .controls =
                        {
                            ui::Image{.source  = sample_photo(6),
                                      .stretch = ui::bind(s.stretch,
                                                          [](int i) { return Stretches[static_cast<std::size_t>(i)]; }),
                                      .width   = 100.0f,
                                      .height  = 100.0f},
                        },
                }),
                example({
                    .title = "Nine grid images.",
                    .code  = R"cpp(
                        // picture is an 82x82 ui::Bitmap with a 3 pixel frame.
                        ui::VerticalStack{
                            .content = {
                                ui::Text{.text = "The normal image"},
                                ui::Image{.source = picture, .height = 82},
                                ui::Text{.text = "Image stretched evenly"},
                                // The margins are in the picture's pixels: the corners keep their size.
                                ui::Image{.source = picture, .nine_grid = ui::Thickness{3.0f}, .height = 164},
                                ui::Text{.text = "Image stretched using nine grid"},
                                ui::Image{.source    = picture,
                                          .nine_grid = ui::Symmetric{.horizontal = 30.0f, .vertical = 20.0f},
                                          .height    = 164},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::VerticalStack{
                                .content =
                                    {
                                        ui::Text{.text = "The normal image"},
                                        ui::Image{.source = nine_grid_picture(), .height = 82.0f},
                                        ui::Text{.text = "Image stretched evenly"},
                                        ui::Image{.source = nine_grid_picture(), .nine_grid = ui::Thickness{3.0f}, .height = 164.0f},
                                        ui::Text{.text = "Image stretched using nine grid"},
                                        ui::Image{.source    = nine_grid_picture(),
                                                  .nine_grid = ui::Symmetric{.horizontal = 30.0f, .vertical = 20.0f},
                                                  .height    = 164.0f},
                                    },
                            },
                        },
                }),
                example({
                    .title    = "An SVG image.",
                    .code     = R"cpp(
                        // The markup of an SVG document, here a string in the program.
                        const ui::SvgImage devices{svg_markup};

                        ui::Image{.source = devices, .height = 100}
                    )cpp",
                    .controls = {ui::Image{.source = devices_svg(), .height = 100.0f}},
                }),
                example({
                    .title = "Animated GIF playback.",
                    .code  = R"cpp(
                            const ui::AnimatedImage badge = ui::AnimatedImage::read("badge.gif");

                            ui::AnimatedImage paused = badge;
                            paused.auto_play         = false;

                            ui::Control<ui::Image> click_to_play;

                            ui::VerticalStack{
                                .spacing = 12,
                                .content = {
                                    // An animated source plays as soon as it is shown.
                                    ui::Image{.source = badge, .height = 40},
                                    // With auto_play off it stays on its first frame.
                                    ui::Image{.source = paused, .height = 40},
                                    // Played and stopped through the control.
                                    ui::Image{.source = paused, .height = 40, .control = click_to_play},
                                    ui::Button{.text = "Play", .on_click = [&] { click_to_play->play(); }},
                                    ui::Button{.text = "Stop", .on_click = [&] { click_to_play->stop(); }},
                                },
                            }
                        )cpp",
                    // ClickToPlaySource_ImageOpened shows the buttons once the source turns out to
                    // be animated, which this one always is.
                    .options = ui::VerticalStack{.spacing = 8.0f,
                                                 .content =
                                                     {
                                                         ui::Button{.text                 = "Play",
                                                                    .horizontal_alignment = ui::Alignment::Leading,
                                                                    .on_click = [&s] { s.click_to_play->play(); }},
                                                         ui::Button{.text                 = "Stop",
                                                                    .horizontal_alignment = ui::Alignment::Leading,
                                                                    .on_click = [&s] { s.click_to_play->stop(); }},
                                                     }},
                    .controls =
                        {
                            ui::VerticalStack{
                                .spacing = 12.0f,
                                .content =
                                    {
                                        ui::Text{.text     = "An Image element automatically plays an animated "
                                                             "source.",
                                                 .wrapping = ui::TextWrapping::Wrap},
                                        ui::Image{.source = web_badge(), .height = 40.0f, .horizontal_alignment = ui::Alignment::Leading},
                                        ui::Text{.text = "Turn off auto play to keep the animation from starting on "
                                                         "its own.",
                                                 .wrapping = ui::TextWrapping::Wrap},
                                        ui::Image{.source = paused_badge(), .height = 40.0f, .horizontal_alignment = ui::Alignment::Leading},
                                        ui::Text{.text     = "Control playback manually using play() and stop().",
                                                 .wrapping = ui::TextWrapping::Wrap},
                                        ui::Image{.source               = paused_badge(),
                                                  .height               = 40.0f,
                                                  .horizontal_alignment = ui::Alignment::Leading,
                                                  .control              = s.click_to_play},
                                    },
                            },
                        },
                }),
            });
        });
}

// ---- PersonPicture ----

// Stands in for the sample's profile photo, 240x240.
const ui::Bitmap& portrait()
{
    static const ui::Bitmap picture = []
    {
        constexpr int Size = 240;
        ui::Bitmap    img(Size, Size);
        for (int y = 0; y < Size; ++y)
        {
            const float t = static_cast<float>(y) / (Size - 1);
            for (int x = 0; x < Size; ++x)
            {
                img.set(x,
                        y,
                        ui::Color::from_rgba(static_cast<std::uint8_t>(250 - t * 40),
                                             static_cast<std::uint8_t>(206 - t * 50),
                                             static_cast<std::uint8_t>(150 + t * 30)));
            }
        }
        const ui::Color hair = ui::Color::from_rgba(70, 44, 30);
        fill_ellipse(img, 120.0f, 104.0f, 58.0f, 66.0f, hair);                                // hair behind the head
        fill_ellipse(img, 120.0f, 250.0f, 96.0f, 78.0f, ui::Color::from_rgba(38, 76, 140));   // shoulders
        fill_ellipse(img, 120.0f, 176.0f, 20.0f, 24.0f, ui::Color::from_rgba(214, 160, 120)); // neck
        fill_ellipse(img, 120.0f, 118.0f, 44.0f, 52.0f, ui::Color::from_rgba(232, 180, 140)); // face
        fill_ellipse(img, 104.0f, 76.0f, 36.0f, 17.0f, hair);                                 // fringe, to one side
        fill_ellipse(img, 142.0f, 72.0f, 24.0f, 13.0f, hair);
        fill_ellipse(img, 103.0f, 116.0f, 4.0f, 5.0f, ui::Color::from_rgba(50, 36, 30)); // eyes
        fill_ellipse(img, 137.0f, 116.0f, 4.0f, 5.0f, ui::Color::from_rgba(50, 36, 30));
        fill_ellipse(img, 120.0f, 142.0f, 12.0f, 4.0f, ui::Color::from_rgba(190, 90, 90)); // mouth
        return img;
    }();
    return picture;
}

// The profile type the radio buttons pick: exactly one of the three is set.
enum class ProfileType
{
    ProfileImage,
    DisplayName,
    Initials,
};

struct PersonPictureState
{
    ui::Property<int> profile{static_cast<int>(ProfileType::ProfileImage)};
};

ui::Spec person_picture_page(Context&)
{
    return with_state<PersonPictureState>(
        [](PersonPictureState& s)
        {
            const auto is = [](ProfileType type) { return [type](int p) { return p == static_cast<int>(type); }; };
            return examples({
                example({
                    .title   = "Select different looks for the  person picture.",
                    .code    = R"cpp(
                        // 0 is the profile image, 1 the display name, 2 the initials: only one is set.
                        ui::Property<std::optional<std::size_t>> profile{0};

                        ui::RadioButtons{.header         = "Profile type",
                                         .items          = {"Profile Image", "Display Name", "Initials"},
                                         .selected_index = profile}

                        ui::PersonPicture{
                            .display_name    = ui::bind(profile, [](std::optional<std::size_t> p) { return p == 1 ? "Jane Doe" : ""; }),
                            .initials        = ui::bind(profile, [](std::optional<std::size_t> p) { return p == 2 ? "SB" : ""; }),
                            .profile_picture = ui::bind(profile, [&](std::optional<std::size_t> p) { return p == 0 ? portrait : ui::Bitmap{}; }),
                            .height          = 300,
                        }
                    )cpp",
                    .options = ui::RadioButtons{.header               = "Profile type",
                                                .items                = {"Profile Image", "Display Name", "Initials"},
                                                .selected_index       = 0,
                                                .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                { s.profile = static_cast<int>(index.value_or(0)); }},
                    .controls =
                        {
                            ui::PersonPicture{
                                .display_name       = ui::bind(s.profile,
                                                               [is](int p)
                                                               {
                                                             return is(ProfileType::DisplayName)(p)
                                                                        ? std::string{"Jane Doe"}
                                                                        : std::string{};
                                                               }),
                                .initials           = ui::bind(s.profile,
                                                               [is](int p)
                                                               {
                                                         return is(ProfileType::Initials)(p) ? std::string{"SB"}
                                                                                             : std::string{};
                                                               }),
                                .profile_picture    = ui::bind(s.profile,
                                                               [is](int p)
                                                               {
                                                                return is(ProfileType::ProfileImage)(p) ? portrait()
                                                                                                        : ui::Bitmap{};
                                                               }),
                                .height             = 300.0f,
                                .vertical_alignment = ui::Alignment::Leading,
                            },
                        },
                }),
                // libwinui's own card: the badge and group looks the card above never shows.
                example({
                    .title = "Badges, and a group.",
                    .code  = R"cpp(
                        ui::HorizontalStack{
                            .spacing = 24,
                            .content = {
                                ui::PersonPicture{.display_name = "Jane Doe", .badge_number = 5, .width = 72},
                                // Past 99 the badge reads "99+".
                                ui::PersonPicture{.display_name = "Jane Doe", .badge_number = 120, .width = 72},
                                ui::PersonPicture{.display_name = "Jane Doe",
                                                  .badge_icon   = ui::IconGlyph{0xE8BE}, // Chat
                                                  .badge_text   = "new message",
                                                  .width        = 72},
                                ui::PersonPicture{.profile_picture = portrait, .badge_icon = photo, .width = 72},
                                ui::PersonPicture{.group           = true, .badge_number = 3, .width = 72},
                            },
                        }
                    )cpp",
                    .controls =
                        {
                            ui::HorizontalStack{
                                .spacing = 24.0f,
                                .content =
                                    {
                                        ui::PersonPicture{.display_name = "Jane Doe", .badge_number = 5, .width = 72.0f},
                                        ui::PersonPicture{.display_name = "Jane Doe", .badge_number = 120, .width = 72.0f},
                                        ui::PersonPicture{.display_name = "Jane Doe",
                                                          .badge_icon   = ui::IconGlyph{0xE8BE}, // Chat
                                                          .badge_text   = "new message",
                                                          .width        = 72.0f},
                                        ui::PersonPicture{.profile_picture = portrait(), .badge_icon = sample_photo(2), .width = 72.0f},
                                        ui::PersonPicture{.group = true, .badge_number = 3, .width = 72.0f},
                                    },
                            },
                        },
                }),
            });
        });
}

// ---- InkCanvas ------------------------------------------------------------------------------

// The paper: white, framed in light gray.
constexpr ui::Color InkPaperEdge = ui::Color::from_argb(0xFFCCCCCC);

constexpr std::array<ui::Color, 3> InkColors{ui::colors::Black,
                                             ui::Color::from_rgba(255, 0, 0),
                                             ui::Color::from_rgba(0, 0, 255)};

constexpr std::array<ui::InkToolbarTool, 4> InkTools{ui::InkToolbarTool::BallpointPen,
                                                     ui::InkToolbarTool::Pencil,
                                                     ui::InkToolbarTool::Highlighter,
                                                     ui::InkToolbarTool::Eraser};

constexpr std::array<ui::InkToolbarButtonFlyoutPlacement, 5> InkPlacements{ui::InkToolbarButtonFlyoutPlacement::Auto,
                                                                           ui::InkToolbarButtonFlyoutPlacement::Top,
                                                                           ui::InkToolbarButtonFlyoutPlacement::Bottom,
                                                                           ui::InkToolbarButtonFlyoutPlacement::Left,
                                                                           ui::InkToolbarButtonFlyoutPlacement::Right};

std::size_t ink_tool_index(ui::InkToolbarTool tool)
{
    return static_cast<std::size_t>(std::ranges::find(InkTools, tool) - InkTools.begin());
}

ui::Spec ink_paper(ui::Spec canvas)
{
    return ui::Border{.background       = ui::colors::White,
                      .border_brush     = InkPaperEdge,
                      .border_thickness = ui::Thickness{1.0f},
                      .height           = 280.0f,
                      .max_width        = 600.0f,
                      .content          = std::move(canvas)};
}

struct InkCanvasState
{
    // Draw with an InkCanvas.
    ui::Control<ui::InkCanvas>               drawing;
    ui::Property<std::optional<std::size_t>> color{0};
    ui::Property<double>                     width{4.0};
    ui::Property<bool>                       inking{true};
    ui::Property<bool>                       pen{true};
    ui::Property<bool>                       mouse{true};
    ui::Property<bool>                       touch{true};

    // Customize an InkToolbar.
    ui::Control<ui::InkCanvas>               toolbar_canvas;
    ui::Control<ui::InkToolbar>              toolbar;
    ui::Property<std::optional<std::size_t>> tool{0};
    ui::Property<std::optional<std::size_t>> placement{0};
    ui::Property<bool>                       show_pencil{true};
    ui::Property<bool>                       show_highlighter{true};
    ui::Property<bool>                       show_eraser{true};

    // Select ink with a lasso.
    ui::Control<ui::InkCanvas>  lasso_canvas;
    ui::Control<ui::InkToolbar> lasso_toolbar;
    std::vector<ui::Point>      lasso;
    bool                        moving = false;
    ui::Point                   last{};
    ui::Property<bool>          selected{false};

    // The Lasso tool's input: a drag outlines the strokes to select; one that starts inside the
    // selection moves it.
    void lasso_input(const ui::PointerEvent& e)
    {
        ui::InkStrokeContainer& strokes = lasso_canvas->strokes();
        switch (e.action)
        {
            case ui::PointerAction::Down:
                if (e.button == ui::PointerButton::Left)
                {
                    moving = strokes.has_selection() && strokes.selection_bounding_rect().contains(e.position);
                    last   = e.position;
                    lasso  = {e.position};
                }
                return;
            case ui::PointerAction::Move:
                if (moving)
                {
                    strokes.move_selected({e.position.x - last.x, e.position.y - last.y});
                    last = e.position;
                }
                else if (!lasso.empty())
                {
                    lasso.push_back(e.position);
                    lasso_canvas->set_lasso_outline(lasso);
                }
                return;
            case ui::PointerAction::Up:
                if (!moving)
                {
                    (void)strokes.select_with_polyline(lasso);
                }
                lasso.clear();
                lasso_canvas->set_lasso_outline({});
                moving   = false;
                selected = strokes.has_selection();
                return;
            case ui::PointerAction::Leave:
                lasso.clear();
                lasso_canvas->set_lasso_outline({});
                moving = false;
                return;
            case ui::PointerAction::Wheel:
                return;
        }
    }

    // Save and restore ink strokes.
    ui::Control<ui::InkCanvas> strokes_canvas;
    ui::Property<std::size_t>  count{0};
    ui::Property<bool>         has_saved{false};
    ui::Property<std::string>  status{"Draw something, then save it."};
    std::vector<ui::InkStroke> saved;

    ui::Property<bool>& shown(ui::InkToolbarTool t)
    {
        return t == ui::InkToolbarTool::Pencil ? show_pencil
               : t == ui::InkToolbarTool::Highlighter
                   ? show_highlighter
                   : show_eraser;
    }

    // The ballpoint pen is always there; a hidden tool hands the selection back to it first.
    void show_tool(ui::InkToolbarTool t, bool on)
    {
        shown(t) = on;
        if (!on && toolbar->active_tool() == t)
        {
            toolbar->set_active_tool(ui::InkToolbarTool::BallpointPen);
        }
        toolbar->set_tool_visible(t, on);
    }
};

ui::Spec ink_tool_check(InkCanvasState& s, const char* label, ui::InkToolbarTool tool)
{
    return ui::CheckBox{.text               = label,
                        .checked            = ui::bind_one_way(s.shown(tool)),
                        .accessibility      = {.name = std::string{"Show "} + label},
                        .on_checked_changed = [&s, tool](std::optional<bool> on)
                        { s.show_tool(tool, on.value_or(false)); }};
}

ui::Spec ink_device_check(const char* label, ui::Property<bool>& on)
{
    return ui::CheckBox{.text               = label,
                        .checked            = ui::bind_one_way(on),
                        .accessibility      = {.name = std::string{label} + " input"},
                        .on_checked_changed = [&on](std::optional<bool> v) { on = v.value_or(false); }};
}

ui::PointerDeviceTypes ink_devices(bool pen, bool mouse, bool touch)
{
    ui::PointerDeviceTypes devices = ui::PointerDeviceTypes::None;
    if (pen)
    {
        devices = devices | ui::PointerDeviceTypes::Pen;
    }
    if (mouse)
    {
        devices = devices | ui::PointerDeviceTypes::Mouse;
    }
    if (touch)
    {
        devices = devices | ui::PointerDeviceTypes::Touch;
    }
    return devices;
}

ui::Spec ink_canvas_page(Context&)
{
    return with_state<InkCanvasState>(
        [](InkCanvasState& s) -> ui::Spec
        {
            std::unique_ptr<ui::Element> page = ui::build(examples({
                example({
                    .title   = "Draw with an InkCanvas",
                    .code    = R"cpp(
                        ui::Control<ui::InkCanvas> canvas;
                        ui::Property<std::optional<std::size_t>>  color{0}; // Black, Red, Blue
                        ui::Property<double>       width{4.0};
                        ui::Property<bool>         inking{true};
                        ui::Property<bool>         pen{true}, mouse{true}, touch{true};

                        ui::Border{
                            .background       = ui::colors::White,
                            .border_brush     = paper_edge,
                            .border_thickness = ui::Thickness{1.0f},
                            .height           = 280.0f,
                            .max_width        = 600.0f,
                            .content          = ui::InkCanvas{
                                .drawing_attributes = ui::bind([](std::size_t c, double w)
                                                               { return ui::InkDrawingAttributes{.color = colors[c], .size = {w, w}}; },
                                                               color, width),
                                .input_device_types = ui::bind(ink_devices, pen, mouse, touch), // the checked ones, or-ed
                                .enabled            = inking,
                                .accessibility      = {.name = "Drawing canvas"},
                                .control            = canvas,
                            },
                        }

                        ui::Button{.text = "Clear drawing", .on_click = [&] { canvas->strokes().clear(); }}
                    )cpp",
                    .stretch = true,
                    .options = ui::VerticalStack{
                        .spacing = 8.0f,
                        .width   = 240.0f,
                        .content =
                            {
                                ui::Text{.text = "Input devices", .role = ui::TextRole::BodyStrong},
                                ui::VerticalStack{.spacing = 4.0f,
                                                  .content = {ink_device_check("Pen", s.pen),
                                                              ink_device_check("Mouse", s.mouse),
                                                              ink_device_check("Touch", s.touch)}},
                                ui::ComboBox{.header         = "Ink color",
                                             .items          = {"Black", "Red", "Blue"},
                                             .selected_index = s.color,
                                             .accessibility  = {.name = "Ink color"}},
                                ui::Slider{.header        = "Stroke width",
                                           .value         = s.width,
                                           .min           = 1.0,
                                           .max           = 12.0,
                                           .accessibility = {.name = "Stroke width"}},
                                ui::ToggleSwitch{.header        = "Inking enabled",
                                                 .checked       = s.inking,
                                                 .accessibility = {.name = "Inking enabled"}},
                                ui::Button{.text                 = "Clear drawing",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .accessibility        = {.name = "Clear drawing"},
                                           .on_click             = [&s] { s.drawing->strokes().clear(); }},
                            },
                    },
                    .controls = {ink_paper(ui::InkCanvas{
                        .drawing_attributes = ui::bind([](std::optional<std::size_t> c, double w)
                                                       {
                                                           const float size = static_cast<float>(w);
                                                           return ui::InkDrawingAttributes{.color = InkColors.at(c.value_or(0)), .size = {size, size}};
                                                       },
                                                       s.color,
                                                       s.width),
                        .input_device_types = ui::bind(ink_devices, s.pen, s.mouse, s.touch),
                        .enabled            = s.inking,
                        .accessibility      = {.name = "Drawing canvas"},
                        .control            = s.drawing,
                    })},
                }),
                example({
                    .title   = "Customize an InkToolbar",
                    .code    = R"cpp(
                        ui::Control<ui::InkCanvas>  canvas;
                        ui::Control<ui::InkToolbar> toolbar;

                        ui::VerticalStack{.spacing = 8, .content = {
                            ui::InkToolbar{
                                .stencil_button          = true, // a ruler and a protractor
                                .active_tool            = ui::InkToolbarTool::BallpointPen,
                                .button_flyout_placement = ui::InkToolbarButtonFlyoutPlacement::Auto,
                                .target                  = canvas, // the tools drive the canvas
                                .horizontal_alignment    = ui::Alignment::Leading,
                                .accessibility           = {.name = "Drawing tools"},
                                .control                 = toolbar,
                            },
                            ui::Border{.background = ui::colors::White,
                                       .height     = 280.0f,
                                       .content    = ui::InkCanvas{.control = canvas}},
                        }}

                        // The Pencil check box:
                        toolbar->set_tool_visible(ui::InkToolbarTool::Pencil, false);
                    )cpp",
                    .stretch = true,
                    .options = ui::VerticalStack{
                        .spacing = 8.0f,
                        .width   = 240.0f,
                        .content =
                            {
                                ui::Text{.text = "Available tools", .role = ui::TextRole::BodyStrong},
                                ui::Text{.text     = "Ballpoint pen is always available.",
                                         .role     = ui::TextRole::Caption,
                                         .wrapping = ui::TextWrapping::WrapWholeWords},
                                ink_tool_check(s, "Pencil", ui::InkToolbarTool::Pencil),
                                ink_tool_check(s, "Highlighter", ui::InkToolbarTool::Highlighter),
                                ink_tool_check(s, "Eraser", ui::InkToolbarTool::Eraser),
                                ui::ComboBox{.header               = "Active tool",
                                             .items                = {"Ballpoint pen", "Pencil", "Highlighter", "Eraser"},
                                             .selected_index       = s.tool,
                                             .accessibility        = {.name = "Active tool"},
                                             .on_selection_changed = [&s](std::optional<std::size_t> index)
                                             {
                                                 if (index.value_or(0) < InkTools.size() && InkTools[index.value_or(0)] != ui::InkToolbarTool::BallpointPen)
                                                 {
                                                     s.show_tool(InkTools[index.value_or(0)], true);
                                                 }
                                             }},
                                ui::ComboBox{.header         = "Button flyout placement",
                                             .items          = {"Auto", "Top", "Bottom", "Left", "Right"},
                                             .selected_index = s.placement,
                                             .accessibility  = {.name = "Button flyout placement"}},
                                ui::Button{.text                 = "Clear drawing",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .accessibility        = {.name = "Clear toolbar drawing"},
                                           .on_click             = [&s] { s.toolbar_canvas->strokes().clear(); }},
                            },
                    },
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::InkToolbar{
                                    .stencil_button         = true,
                                    .active_tool            = ui::bind(s.tool, [](std::optional<std::size_t> i) { return InkTools.at(i.value_or(0)); }),
                                    .button_flyout_placement = ui::bind(s.placement, [](std::optional<std::size_t> i) { return InkPlacements.at(i.value_or(0)); }),
                                    .target                  = s.toolbar_canvas,
                                    .horizontal_alignment    = ui::Alignment::Leading,
                                    .accessibility           = {.name = "Drawing tools"},
                                    .on_active_tool_changed  = [&s](ui::InkToolbarTool tool) { s.tool = ink_tool_index(tool); },
                                    .control                 = s.toolbar,
                                },
                                ink_paper(ui::InkCanvas{.accessibility = {.name = "Toolbar drawing canvas"},
                                                        .control       = s.toolbar_canvas}),
                            },
                    }},
                }),
                example({
                    .title   = "Select ink with a lasso",
                    .code    = R"cpp(
                        ui::Control<ui::InkCanvas>  canvas;
                        ui::Control<ui::InkToolbar> toolbar;
                        std::vector<ui::Point>      lasso;

                        ui::InkToolbar{
                            .custom_tools = {{.name = "Lasso", .icon = ui::IconGlyph{0xF1BE}}},
                            .target       = canvas,
                            .control      = toolbar,
                        }

                        // While Lasso is the active tool the canvas inks nothing and hands its input over.
                        ui::InkCanvas{
                            .on_unprocessed_input = [&](const ui::PointerEvent& e)
                            {
                                if (e.action == ui::PointerAction::Down)
                                {
                                    lasso = {e.position};
                                }
                                else if (e.action == ui::PointerAction::Move && !lasso.empty())
                                {
                                    lasso.push_back(e.position);
                                    canvas->set_lasso_outline(lasso); // drawn dashed
                                }
                                else if (e.action == ui::PointerAction::Up)
                                {
                                    canvas->strokes().select_with_polyline(lasso);
                                    lasso.clear();
                                    canvas->set_lasso_outline({});
                                }
                            },
                            .control = canvas,
                        }

                        // The Delete selection button.
                        canvas->strokes().delete_selected();
                    )cpp",
                    .stretch = true,
                    .options = ui::VerticalStack{
                        .spacing = 8.0f,
                        .width   = 240.0f,
                        .content =
                            {
                                ui::Button{.text                 = "Delete selection",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .enabled              = s.selected,
                                           .accessibility        = {.name = "Delete selection"},
                                           .on_click             = [&s]
                                           {
                                               (void)s.lasso_canvas->strokes().delete_selected();
                                               s.selected = false;
                                           }},
                                ui::Text{.text     = "Selected strokes are drawn hollow. Drag inside the selection to move it.",
                                         .role     = ui::TextRole::Caption,
                                         .wrapping = ui::TextWrapping::WrapWholeWords},
                            },
                    },
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Text{.text     = "Draw some strokes, pick the Lasso tool at the end of the toolbar and drag around "
                                                     "strokes to select them.",
                                         .wrapping = ui::TextWrapping::WrapWholeWords},
                                ui::InkToolbar{
                                    .custom_tools         = {{.name = "Lasso", .icon = ui::IconGlyph{0xF1BE}}},
                                    .target               = s.lasso_canvas,
                                    .horizontal_alignment = ui::Alignment::Leading,
                                    .accessibility        = {.name = "Lasso drawing tools"},
                                    .control              = s.lasso_toolbar,
                                },
                                ink_paper(ui::InkCanvas{
                                    .accessibility        = {.name = "Lasso drawing canvas"},
                                    .on_unprocessed_input = [&s](const ui::PointerEvent& e) { s.lasso_input(e); },
                                    .control              = s.lasso_canvas,
                                }),
                            },
                    }},
                }),
                example({
                    .title   = "Save and restore ink strokes",
                    .code    = R"cpp(
                        ui::Control<ui::InkCanvas> canvas;
                        std::vector<ui::InkStroke> saved;

                        ui::InkCanvas{.on_strokes_changed = [&] { count = canvas->strokes().size(); },
                                      .control            = canvas}

                        // Save: the strokes are plain values.
                        const auto strokes = canvas->strokes().strokes();
                        saved.assign(strokes.begin(), strokes.end());

                        canvas->strokes().clear();

                        // Restore: each stroke comes back with a fresh id.
                        canvas->strokes().replace(saved);
                    )cpp",
                    .stretch = true,
                    .options = ui::VerticalStack{
                        .spacing = 8.0f,
                        .width   = 240.0f,
                        .content =
                            {
                                ui::Text{.text = ui::bind(s.count, [](std::size_t n) { return std::format("{} stroke(s) on canvas", n); })},
                                ui::Button{.text                 = "Save drawing",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .accessibility        = {.name = "Save drawing"},
                                           .on_click             = [&s]
                                           {
                                               const auto strokes = s.strokes_canvas->strokes().strokes();
                                               if (strokes.empty())
                                               {
                                                   s.status = std::string{"Draw something before saving."};
                                                   return;
                                               }
                                               s.saved.assign(strokes.begin(), strokes.end());
                                               s.has_saved = true;
                                               s.status    = std::format("Saved {} stroke(s). Clear the canvas to restore them.", s.saved.size());
                                           }},
                                ui::Button{.text                 = "Clear canvas",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .accessibility        = {.name = "Clear saved drawing canvas"},
                                           .on_click             = [&s]
                                           {
                                               s.strokes_canvas->strokes().clear();
                                               s.status = s.has_saved.get()
                                                              ? std::format("Canvas cleared. {} saved stroke(s) are ready to restore.", s.saved.size())
                                                              : std::string{"Canvas cleared. Draw and save something to restore it."};
                                           }},
                                ui::Button{.text                 = "Restore saved drawing",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .enabled              = ui::bind([](bool saved, std::size_t n) { return saved && n == 0; }, s.has_saved, s.count),
                                           .accessibility        = {.name = "Restore saved drawing"},
                                           .on_click             = [&s]
                                           {
                                               s.strokes_canvas->strokes().replace(s.saved);
                                               s.status = std::format("Restored {} stroke(s).", s.strokes_canvas->strokes().size());
                                           }},
                                ui::Text{.text     = s.status,
                                         .role     = ui::TextRole::Caption,
                                         .wrapping = ui::TextWrapping::WrapWholeWords},
                            },
                    },
                    .controls = {ink_paper(ui::InkCanvas{
                        .accessibility      = {.name = "Strokes drawing canvas"},
                        .on_strokes_changed = [&s] { s.count = s.strokes_canvas->strokes().size(); },
                        .control            = s.strokes_canvas,
                    })},
                }),
            }));

            return page;
        });
}

} // namespace

std::span<const Page> media_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"AnimatedVisualPlayer", "An element to render and control playback of motion graphics.", animated_visual_player_page},
        {"Image", "A control to display image content.", image_page},
        {"InkCanvas", "Draw with pen, touch, or mouse and choose tools with an InkToolbar.", ink_canvas_page},
        {"PersonPicture", "Displays the picture of a person/contact.", person_picture_page},
    });
    return Pages;
}

} // namespace gallery
