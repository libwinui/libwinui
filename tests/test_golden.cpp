// SPDX-License-Identifier: MIT
#include "ui/LayoutSnapshot.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Scheduler.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/media/Image.hpp"
#include "winui/ui/media/Shape.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/EarlyPalette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdlib>

#ifdef _WIN32
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/OffscreenCapture.hpp"
#include "winui/ui/containers/Border.hpp"
#endif

#ifndef LIBWINUI_GOLDEN_DIR
#define LIBWINUI_GOLDEN_DIR "."
#endif

using namespace ui;
using namespace ui::testing;

namespace
{

constexpr Size ButtonSize{240, 90};
constexpr Size SceneSize{320, 200};
constexpr Size ControlsSceneSize{320, 280};
constexpr char Label[] = "Save";

struct State
{
    const char*                         name;
    std::function<void(ButtonControl&)> mutate;
};

const std::vector<State>& states()
{
    static const std::vector<State> s = {
        {"normal", nullptr},
        {"hover", [](ButtonControl& b) { b.handle_pointer({{120, 45}, PointerButton::None, PointerAction::Move}); }},
        {"pressed",
         [](ButtonControl& b)
         {
             b.handle_pointer({{120, 45}, PointerButton::None, PointerAction::Move});
             b.handle_pointer({{120, 45}, PointerButton::Left, PointerAction::Down});
         }},
        {"focused", [](ButtonControl& b) { ui::testing::set_focused(b, true); }},
        {"disabled", [](ButtonControl& b) { b.set_enabled(false); }},
    };
    return s;
}

std::unique_ptr<Window> build(const State& st, const TextMeasurer& m)
{
    auto window = std::make_unique<Window>();
    window->set_stretch_content(false); // laid out for the control at its desired size, centered
    ButtonControl* button = window->set_content(std::make_unique<ButtonControl>(Label));
    window->layout(ButtonSize, m);
    if (st.mutate)
    {
        st.mutate(*button);
    }
    return window;
}

std::unique_ptr<Window> build_scene(const TextMeasurer& m, ParallelLayout parallel = {})
{
    auto window = std::make_unique<Window>();
    window->set_parallel_layout(parallel);
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->set_spacing(12.0f);
    stack->add(std::make_unique<TextControl>("Settings"));
    stack->add(std::make_unique<ButtonControl>("Save"));
    stack->add(std::make_unique<ButtonControl>("Cancel"));
    window->set_stretch_content(false); // laid out for the control at its desired size, centered
    window->set_content(std::move(stack));
    window->layout(SceneSize, m);
    return window;
}

std::unique_ptr<Window> build_controls_scene(const TextMeasurer& m, Theme theme = Theme::light(), ParallelLayout parallel = {})
{
    auto window = std::make_unique<Window>();
    window->pin_theme(std::move(theme));
    window->set_parallel_layout(parallel);
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->set_spacing(12.0f);
    stack->add(std::make_unique<CheckBoxControl>("Unchecked"));
    stack->add(std::make_unique<CheckBoxControl>("Checked", true));
    stack->add(std::make_unique<RadioButtonControl>("Radio A", true));
    stack->add(std::make_unique<RadioButtonControl>("Radio B"));
    stack->add(std::make_unique<ToggleSwitchControl>());
    stack->add(std::make_unique<ToggleSwitchControl>(true));
    window->set_stretch_content(false); // laid out for the control at its desired size, centered
    window->set_content(std::move(stack));
    window->layout(ControlsSceneSize, m);
    return window;
}

Theme windows9x_theme()
{
    ThemeRequest req;
    req.variant = ThemeVariant::Windows9x;
    return resolve_theme(req, OsThemeSnapshot{});
}

// Dark mode, because that is where MDL's CheckBox and RadioButton border polarity shows.
Theme mdl_theme()
{
    ThemeRequest req;
    req.mode    = ThemeMode::Dark;
    req.variant = ThemeVariant::Metro;
    return resolve_theme(req, OsThemeSnapshot{});
}

Theme windows2000_theme()
{
    ThemeRequest req;
    req.variant        = ThemeVariant::Windows9x;
    req.classic_scheme = ClassicPalette::windows_standard_2000();
    return resolve_theme(req, OsThemeSnapshot{});
}

Theme windows3x_theme()
{
    return resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
}

Theme windows2x_theme()
{
    return resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});
}

Theme windows1x_theme()
{
    ThemeRequest req;
    req.variant      = ThemeVariant::Windows1x;
    req.early_scheme = EarlyPalette::windows_1x_color();
    return resolve_theme(req, OsThemeSnapshot{});
}

Theme aero_theme()
{
    return resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
}

// A Frame::Themed window with a title and one button, drawn active or inactive.
constexpr Size FrameSceneSize{240.0f, 120.0f};

std::unique_ptr<Window> build_frame_scene(const TextMeasurer& m, Theme theme, bool active, bool glass = false)
{
    auto window = std::make_unique<Window>();
    window->pin_theme(std::move(theme));
    window->set_title("Notepad");
    window->set_style({.frame = Frame::Themed});
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::make_unique<ButtonControl>("OK"));
    window->set_content(std::move(stack));
    ui::testing::seams(*window).report_active(active);
    ui::testing::seams(*window).report_blur_behind(glass);
    window->layout(FrameSceneSize, m);
    return window;
}

std::string frame_scene_path(const std::string& name, const char* ext)
{
    return std::string(LIBWINUI_GOLDEN_DIR) + "/window-frame-" + name + ext;
}

std::string golden_path(const std::string& name, const char* ext)
{
    return std::string(LIBWINUI_GOLDEN_DIR) + "/button-" + name + ext;
}

std::string scene_path(const char* ext)
{
    return std::string(LIBWINUI_GOLDEN_DIR) + "/scene-form" + ext;
}

std::string controls_scene_path(const char* ext)
{
    return std::string(LIBWINUI_GOLDEN_DIR) + "/controls-form" + ext;
}

std::string controls_scene_retro_path(const char* ext)
{
    return std::string(LIBWINUI_GOLDEN_DIR) + "/controls-form-retro95" + ext;
}

std::string controls_scene_metro_path(const char* ext)
{
    return std::string(LIBWINUI_GOLDEN_DIR) + "/controls-form-metro" + ext;
}

Theme luna_theme()
{
    ThemeRequest req;
    req.variant = ThemeVariant::Luna;
    return resolve_theme(req, OsThemeSnapshot{});
}

std::string controls_scene_luna_path(const char* ext)
{
    return std::string(LIBWINUI_GOLDEN_DIR) + "/controls-form-luna" + ext;
}

// Whether environment variable `name` is set to something other than empty or 0.
bool env_flag(const char* name)
{
#ifdef _MSC_VER
    // getenv() is deprecated under MSVC's CRT.
    char*       raw  = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr)
    {
        return false;
    }
    const std::string v{raw};
    std::free(raw);
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr)
    {
        return false;
    }
    const std::string v{raw};
#endif
    return !v.empty() && v != "0";
}

bool updating()
{
    return env_flag("LIBWINUI_UPDATE_GOLDEN");
}

std::string read_file(const std::string& path)
{
    std::ifstream      f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& content)
{
    std::ofstream f(path, std::ios::binary);
    f << content;
}

void check_pixel_golden(const Bitmap& actual, const std::string& path)
{
    // The PNGs are made on a development machine. Another Windows build ships other font and
    // DirectWrite versions, which move anti-aliased edges, so CI skips them.
    if (env_flag("LIBWINUI_SKIP_PIXEL_GOLDENS"))
    {
        SKIP("LIBWINUI_SKIP_PIXEL_GOLDENS is set");
    }
    if (updating())
    {
        actual.write_png(path);
        SUCCEED("updated " << path);
        return;
    }
    Bitmap expected;
    REQUIRE_NOTHROW(expected = Bitmap::read_png(path)); // missing -> regenerate goldens
    const ImageDiff d = compare(actual, expected, /*threshold*/ 1);
    REQUIRE(d.same_size);
    // The software WIC target is deterministic run-to-run; a tiny tolerance guards against
    // incidental rounding across DirectWrite/Direct2D versions. A Windows update can still change a
    // system font: the 2026-09-30 SegoeIcons.ttf moved the check box tick's anti-aliasing. Compare
    // the .actual.png written below, then regenerate.
    CHECK(d.max_channel_delta <= 2);
    CHECK(d.mismatched_pixels == 0);
    if (d.max_channel_delta > 2 || d.mismatched_pixels != 0)
    {
        // Beside the golden and ignored by git, to diff by eye.
        const std::string actual_path = path.substr(0, path.size() - 4) + ".actual.png";
        actual.write_png(actual_path);
        WARN("wrote " << actual_path);
    }
}

} // namespace

TEST_CASE("Semantic render-tree goldens (.txt)", "[golden][semantic]")
{
    HeadlessTextMeasurer m;
    for (const auto& st : states())
    {
        const std::string actual = describe(*build(st, m));
        const std::string path   = golden_path(st.name, ".txt");
        INFO("state=" << st.name << " golden=" << path);

        if (updating())
        {
            write_file(path, actual);
            SUCCEED("updated " << path);
            continue;
        }
        const std::string expected = read_file(path);
        REQUIRE_FALSE(expected.empty()); // missing golden -> run with LIBWINUI_UPDATE_GOLDEN=1
        REQUIRE(actual == expected);
    }
}

TEST_CASE("Slice-2 scene semantic golden (.txt)", "[golden][semantic][scene]")
{
    HeadlessTextMeasurer m;
    const std::string    actual = describe(*build_scene(m));
    const std::string    path   = scene_path(".txt");
    if (updating())
    {
        write_file(path, actual);
        SUCCEED("updated " << path);
    }
    else
    {
        const std::string expected = read_file(path);
        REQUIRE_FALSE(expected.empty());
        REQUIRE(actual == expected);
    }
}

TEST_CASE("Control suite scene semantic golden (.txt)", "[golden][semantic][scene]")
{
    HeadlessTextMeasurer m;
    const std::string    actual = describe(*build_controls_scene(m));
    const std::string    path   = controls_scene_path(".txt");
    if (updating())
    {
        write_file(path, actual);
        SUCCEED("updated " << path);
    }
    else
    {
        const std::string expected = read_file(path);
        REQUIRE_FALSE(expected.empty());
        REQUIRE(actual == expected);
    }
}

TEST_CASE("Control suite scene semantic golden, Windows9x retro theme (.txt)", "[golden][semantic][scene][retro]")
{
    HeadlessTextMeasurer m;
    const std::string    actual = describe(*build_controls_scene(m, windows9x_theme()));
    const std::string    path   = controls_scene_retro_path(".txt");
    if (updating())
    {
        write_file(path, actual);
        SUCCEED("updated " << path);
    }
    else
    {
        const std::string expected = read_file(path);
        REQUIRE_FALSE(expected.empty());
        REQUIRE(actual == expected);
    }
}

TEST_CASE("Control suite scene semantic golden, Luna theme (.txt)", "[golden][semantic][scene][retro][luna]")
{
    HeadlessTextMeasurer m;
    const std::string    actual = describe(*build_controls_scene(m, luna_theme()));
    const std::string    path   = controls_scene_luna_path(".txt");
    if (updating())
    {
        write_file(path, actual);
        SUCCEED("updated " << path);
    }
    else
    {
        const std::string expected = read_file(path);
        REQUIRE_FALSE(expected.empty());
        REQUIRE(actual == expected);
    }
}

TEST_CASE("Control suite scene semantic golden, Metro theme (.txt)", "[golden][semantic][scene][metro]")
{
    HeadlessTextMeasurer m;
    const std::string    actual = describe(*build_controls_scene(m, mdl_theme()));
    const std::string    path   = controls_scene_metro_path(".txt");
    if (updating())
    {
        write_file(path, actual);
        SUCCEED("updated " << path);
    }
    else
    {
        const std::string expected = read_file(path);
        REQUIRE_FALSE(expected.empty());
        REQUIRE(actual == expected);
    }
}

TEST_CASE("Scene semantic goldens are unchanged by parallel layout", "[golden][semantic][scene][parallel-layout]")
{
    if (updating())
    {
        SKIP("the goldens are recorded by the sequential cases");
    }
    // Every batch split, however small, on real workers.
    Scheduler            workers{4};
    const ParallelLayout parallel{.enabled = true, .min_elements = 1, .scheduler = &workers};
    HeadlessTextMeasurer m;
    CHECK(describe(*build_scene(m, parallel)) == read_file(scene_path(".txt")));
    CHECK(describe(*build_controls_scene(m, Theme::light(), parallel)) == read_file(controls_scene_path(".txt")));
    CHECK(describe(*build_controls_scene(m, windows9x_theme(), parallel)) == read_file(controls_scene_retro_path(".tx"
                                                                                                                 "t")));
    CHECK(describe(*build_controls_scene(m, luna_theme(), parallel)) == read_file(controls_scene_luna_path(".txt")));
    CHECK(describe(*build_controls_scene(m, mdl_theme(), parallel)) == read_file(controls_scene_metro_path(".txt")));
}

#ifdef _WIN32

TEST_CASE("Scene pixel goldens are unchanged by parallel layout", "[golden][pixel][scene][parallel-layout]")
{
    if (updating())
    {
        SKIP("the goldens are recorded by the sequential cases");
    }
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    Scheduler            workers{4};
    const ParallelLayout parallel{.enabled = true, .min_elements = 1, .scheduler = &workers};
    check_pixel_golden(d2d.capture(*build_scene(d2d.measurer(), parallel), SceneSize, Theme::light().colors().background),
                       scene_path(".png"));
    check_pixel_golden(d2d.capture(*build_controls_scene(d2d.measurer(), Theme::light(), parallel), ControlsSceneSize),
                       controls_scene_path(".png"));
}

TEST_CASE("Pixel goldens (.png) via Direct2D/DirectWrite", "[golden][pixel]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    // Fluent fills are translucent, so capture over the real window background, not white.
    const Color window_background = Theme::light().colors().background;

    for (const auto& st : states())
    {
        const Bitmap      actual = d2d.capture(*build(st, d2d.measurer()), ButtonSize, window_background);
        const std::string path   = golden_path(st.name, ".png");
        INFO("state=" << st.name << " golden=" << path);
        check_pixel_golden(actual, path);
    }
}

TEST_CASE("Slice-2 scene pixel golden (.png) via Direct2D/DirectWrite", "[golden][pixel][scene]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    const Bitmap actual = d2d.capture(*build_scene(d2d.measurer()), SceneSize, Theme::light().colors().background);
    check_pixel_golden(actual, scene_path(".png"));
}

TEST_CASE("Control suite scene pixel golden (.png) via Direct2D/DirectWrite", "[golden][pixel][scene]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    const Bitmap actual = d2d.capture(*build_controls_scene(d2d.measurer()), ControlsSceneSize);
    check_pixel_golden(actual, controls_scene_path(".png"));
}

TEST_CASE("Control suite scene pixel golden, Windows9x retro theme (.png) via Direct2D/DirectWrite",
          "[golden][pixel][scene][retro]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    const Theme theme = windows9x_theme();
    const Bitmap actual = d2d.capture(*build_controls_scene(d2d.measurer(), theme), ControlsSceneSize, theme.colors().background);
    check_pixel_golden(actual, controls_scene_retro_path(".png"));
}

TEST_CASE("Control suite scene pixel golden, Luna theme (.png) via Direct2D/DirectWrite",
          "[golden][pixel][scene][retro][luna]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    const Theme theme = luna_theme();
    const Bitmap actual = d2d.capture(*build_controls_scene(d2d.measurer(), theme), ControlsSceneSize, theme.colors().background);
    check_pixel_golden(actual, controls_scene_luna_path(".png"));
}

TEST_CASE("Control suite scene pixel golden, Metro theme (.png) via Direct2D/DirectWrite",
          "[golden][pixel][scene][metro]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    const Theme theme = mdl_theme();
    const Bitmap actual = d2d.capture(*build_controls_scene(d2d.measurer(), theme), ControlsSceneSize, theme.colors().background);
    check_pixel_golden(actual, controls_scene_metro_path(".png"));
}

// Guards a real bug: the renderer snapped a 1px frame's path onto a pixel boundary, and aliased
// rasterization (the classic packs') then dropped the frame's top-left corner pixel.
TEST_CASE("A classic one-pixel frame closes at all four corners", "[pixel][retro]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    constexpr Size Scene{120, 50};
    const Theme    theme  = windows9x_theme();
    auto           window = std::make_unique<Window>();
    window->pin_theme(theme);
    auto* box = window->set_content(std::make_unique<TextBoxControl>("Text"));
    window->layout(Scene, d2d.measurer());

    const Bitmap actual = d2d.capture(*window, Scene, theme.colors().background);
    REQUIRE_FALSE(actual.empty());

    const Rect  b      = box->bounds();
    const int   left   = static_cast<int>(b.left());
    const int   top    = static_cast<int>(b.top());
    const int   right  = static_cast<int>(b.right()) - 1;
    const int   bottom = static_cast<int>(b.bottom()) - 1;
    const Color frame  = actual.get((left + right) / 2, top);
    REQUIRE(frame != actual.get(left + 4, top + 4)); // the frame is drawn at all
    CHECK(actual.get(left, top) == frame);
    CHECK(actual.get(right, top) == frame);
    CHECK(actual.get(left, bottom) == frame);
    CHECK(actual.get(right, bottom) == frame);
}

// Guards a real bug: D2D rejected D2D1_ALPHA_MODE_STRAIGHT bitmaps, so nothing drew. The
// recorded DrawImage command looked right, only a real capture shows the pixels.
TEST_CASE("An Icon actually paints its bitmap through the real Direct2D backend", "[pixel][image-icon]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    constexpr Size IconScene{40, 40};
    const Color    photo_color = Color::from_rgba(220, 20, 60);
    Bitmap         source(20, 20, photo_color);

    auto window = std::make_unique<Window>();
    window->set_stretch_content(false); // laid out for the control at its desired size, centered
    window->set_content(std::make_unique<IconControl>(source));
    window->layout(IconScene, d2d.measurer());

    const Bitmap actual = d2d.capture(*window, IconScene, colors::White);
    REQUIRE_FALSE(actual.empty());

    const Color center = actual.get(20, 20); // inside the 20x20 icon, centered in the 40x40 scene
    CHECK_THAT(center.r, Catch::Matchers::WithinAbs(photo_color.r, 0.01));
    CHECK_THAT(center.g, Catch::Matchers::WithinAbs(photo_color.g, 0.01));
    CHECK_THAT(center.b, Catch::Matchers::WithinAbs(photo_color.b, 0.01));

    const Color corner = actual.get(1, 1); // outside the icon, still the white background
    CHECK_THAT(corner.r, Catch::Matchers::WithinAbs(1.0, 0.01));
}

TEST_CASE("ImageControl paints a source rect and an SVG through the real Direct2D backend", "[pixel][image-view]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    constexpr Size Scene{40, 40};

    Bitmap picture(20, 10, Color::from_rgba(255, 0, 0));
    for (int y = 0; y < 10; ++y)
    {
        for (int x = 10; x < 20; ++x)
        {
            picture.set(x, y, Color::from_rgba(0, 0, 255));
        }
    }
    auto bitmap = std::make_unique<ImageControl>(picture);
    bitmap->set_stretch(Stretch::UniformToFill); // 80x40 ink, clipped to its left 40x40...
    bitmap->set_width(40.0f);
    bitmap->set_height(40.0f);
    auto window = std::make_unique<Window>();
    window->set_content(std::move(bitmap));
    window->layout(Scene, d2d.measurer());
    const Bitmap cropped = d2d.capture(*window, Scene, colors::White);
    REQUIRE_FALSE(cropped.empty());
    CHECK(cropped.get(5, 20).r > 0.9f); // ...which is the red half
    CHECK(cropped.get(35, 20).r > 0.9f);
    CHECK(cropped.get(35, 20).b < 0.1f);

    auto svg = std::make_unique<ImageControl>(SvgImage{
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><rect width="20" height="20" fill="#0000FF"/></svg>)"});
    window->set_content(std::move(svg));
    window->layout(Scene, d2d.measurer());
    const Bitmap drawn = d2d.capture(*window, Scene, colors::White);
    REQUIRE_FALSE(drawn.empty());
    const Color middle = drawn.get(20, 20); // Uniform: the square SVG fills the 40x40 scene
    CHECK(middle.b > 0.9f);
    CHECK(middle.r < 0.1f);
}

TEST_CASE("Shapes, a radial gradient and acrylic paint through the real Direct2D backend", "[pixel][shape]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    constexpr Size Scene{100, 100};

    auto window  = std::make_unique<Window>();
    auto ellipse = std::make_unique<EllipseControl>();
    ellipse->set_fill(Color::from_rgba(0, 0, 255));
    ellipse->set_stroke(Color::from_rgba(255, 0, 0));
    ellipse->set_stroke_thickness(10.0f);
    ellipse->set_width(100.0f); // a Window centers its content at its desired size
    ellipse->set_height(100.0f);
    window->set_content(std::move(ellipse));
    window->layout(Scene, d2d.measurer());
    const Bitmap shapes = d2d.capture(*window, Scene, colors::White);
    REQUIRE_FALSE(shapes.empty());
    CHECK(shapes.get(50, 50).b > 0.9f); // the fill
    CHECK(shapes.get(50, 2).r > 0.9f);  // the stroke, inside the bounds at the top
    CHECK(shapes.get(2, 2).r > 0.9f);   // a corner outside the ellipse stays white
    CHECK(shapes.get(2, 2).g > 0.9f);

    auto rect = std::make_unique<RectangleControl>();
    rect->set_fill(RadialGradientBrush{{{0.0f, Color::from_rgba(255, 255, 0)}, {1.0f, Color::from_rgba(0, 0, 255)}}});
    rect->set_width(100.0f);
    rect->set_height(100.0f);
    window->set_content(std::move(rect));
    window->layout(Scene, d2d.measurer());
    const Bitmap radial = d2d.capture(*window, Scene, colors::White);
    CHECK(radial.get(50, 50).r > 0.9f); // yellow at the center
    CHECK(radial.get(50, 50).b < 0.1f);
    CHECK(radial.get(1, 1).b > 0.9f); // padded blue past the rim
    CHECK(radial.get(1, 1).r < 0.1f);

    // The white fallback is also what the blur pulls in past the window edge. Blue tells red from white.
    auto grid = std::make_unique<GridControl>();
    grid->add_row(GridLength::fraction());
    grid->add_column(GridLength::fraction());
    grid->add_column(GridLength::fraction());
    grid->set_width(100.0f);
    grid->set_height(100.0f);
    auto red = std::make_unique<RectangleControl>();
    red->set_fill(Color::from_rgba(255, 0, 0));
    grid->add(std::move(red), 0, 0);
    auto         glass = std::make_unique<RectangleControl>();
    AcrylicBrush brush;
    brush.tint_color              = colors::White;
    brush.tint_opacity            = 0.0f;
    brush.tint_luminosity_opacity = 0.0f;
    brush.fallback_color          = colors::White;
    glass->set_fill(brush);
    grid->add(std::move(glass), 0, 0, 1, 2);
    window->set_content(std::move(grid));
    window->layout(Scene, d2d.measurer());
    const Bitmap acrylic = d2d.capture(*window, Scene, colors::White);
    const Color  left    = acrylic.get(25, 50);
    const Color  edge    = acrylic.get(50, 50);
    const Color  right   = acrylic.get(75, 50);
    CHECK(left.b < 0.6f);   // mostly the red under it, not a white fill
    CHECK(left.b < edge.b); // a ramp across the old hard edge
    CHECK(edge.b < right.b);
    CHECK(right.b < 0.99f); // and some red reaching into the white half
}


namespace
{
// A red left half, then an acrylic panel slid 40 DIPs right of where it was laid out, the way a
// menu or a drop-down list draws mid-slide: inside a clip and a translation.
class SlidingAcrylic final : public Element
{
protected:
    Size measure_override(Size, const MeasureContext&) const override
    {
        return {100.0f, 100.0f};
    }
    void arrange_override(const Rect&, const ArrangeContext&) override
    {
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        renderer.fill_rounded_rect(Rect{bounds().x, bounds().y, 50.0f, 100.0f}, 0.0f, Color::from_rgba(255, 0, 0));
        Theme        glass = theme;
        AcrylicBrush brush;
        brush.tint_color              = colors::White;
        brush.tint_opacity            = 0.0f;
        brush.tint_luminosity_opacity = 0.0f;
        glass.overlay_material        = brush;
        renderer.push_clip(bounds());
        renderer.push_transform(1.0f, Point{40.0f, 0.0f});
        fill_overlay_surface(renderer, glass, Rect{bounds().x, bounds().y, 40.0f, 100.0f}, 0.0f, colors::White);
        renderer.pop_transform();
        renderer.pop_clip();
    }
};
} // namespace

TEST_CASE("Acrylic under a slide blurs what is under its current position", "[pixel][shape][acrylic]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    const Size scene{100.0f, 100.0f};
    auto       window = std::make_unique<Window>();
    window->set_content(std::make_unique<SlidingAcrylic>());
    window->layout(scene, d2d.measurer());
    const Bitmap image = d2d.capture(*window, scene, colors::White);
    REQUIRE_FALSE(image.empty());

    // The panel covers 40..80. Laid out at 0..40 it would sample solid red; slid, it straddles the
    // red edge at 50, so its left end is pinker than its right, and its right end is near white.
    CHECK(image.get(20, 50).b < 0.05f); // red the panel does not cover
    const Color near_red = image.get(42, 50);
    const Color far      = image.get(78, 50);
    CHECK(near_red.b < far.b);
    CHECK(far.b > 0.75f);
    CHECK(near_red.b < 0.75f);
}

TEST_CASE("Light overlay acrylic keeps the hue of what is under it", "[pixel][shape][acrylic]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    constexpr Size Scene{300.0f, 300.0f};

    // The light overlay material over a dark bluish block, far enough from the edges that the
    // blur sees mostly the block.
    const auto capture_over = [&](const Color& under)
    {
        auto grid = std::make_unique<GridControl>();
        grid->set_width(Scene.width);
        grid->set_height(Scene.height);
        auto block = std::make_unique<RectangleControl>();
        block->set_fill(under);
        grid->add(std::move(block), 0, 0);
        auto glass = std::make_unique<RectangleControl>();
        glass->set_fill(Theme::light().palette.acrylic_in_app_default());
        grid->add(std::move(glass), 0, 0);
        auto window = std::make_unique<Window>();
        window->set_content(std::move(grid));
        window->layout(Scene, d2d.measurer());
        return d2d.capture(*window, Scene, colors::White).get(150, 150);
    };

    // A luminosity layer of 0xFC at 85 % lifts dark gray to about 0.87, not to white.
    const Color gray = capture_over(Color::from_rgba(51, 51, 51));
    CHECK(gray.r > 0.82f);
    CHECK(gray.r < 0.92f);
    // A bluish backdrop stays bluish; a hue turned around comes out yellow.
    const Color blue = capture_over(Color::from_rgba(51, 51, 77));
    CHECK(blue.b > blue.r);
    CHECK(blue.b > blue.g);
}

TEST_CASE("Direct2D draws an emoji in its own colors", "[pixel][text]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    auto window = std::make_unique<Window>();
    window->set_content(std::make_unique<TextControl>("\xF0\x9F\x98\x80"));
    const Size scene{80.0f, 40.0f};
    window->layout(scene, d2d.measurer());
    const Bitmap image = d2d.capture(*window, scene, colors::White);
    REQUIRE_FALSE(image.empty());

    // Black text on white has r == g == b; the face's yellow does not.
    int colored = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const Color c = image.get(x, y);
            if (c.r - c.b > 0.3f && c.g - c.b > 0.2f)
            {
                ++colored;
            }
        }
    }
    CHECK(colored > 10);
}

TEST_CASE("A Border's clip_content cuts its child to the rounded corners through Direct2D", "[pixel][border][clip]")
{
    OffscreenCapture offscreen;
    REQUIRE(offscreen.ok());

    Window         window;
    BorderControl* border = window.set_content(std::make_unique<BorderControl>());
    border->set_corner_radius(20.0f);
    auto fill = std::make_unique<BorderControl>();
    fill->set_background(Color::from_rgb(255, 0, 0));
    border->set_child(std::move(fill));
    const Size client{120.0f, 80.0f};
    window.layout(client, offscreen.measurer());

    const auto is_red   = [](const Color& c) { return c.r > 0.9f && c.g < 0.1f && c.b < 0.1f; };
    const auto is_white = [](const Color& c) { return c.r > 0.9f && c.g > 0.9f && c.b > 0.9f; };

    const Bitmap square = offscreen.capture(window, client);
    CHECK(is_red(square.get(1, 1))); // off: the child fills the corner

    border->set_clip_content(true);
    const Bitmap rounded = offscreen.capture(window, client);
    CHECK(is_white(rounded.get(1, 1))); // the corner is cut away
    CHECK(is_white(rounded.get(118, 78)));
    CHECK(is_red(rounded.get(60, 1))); // the straight edges stay
    CHECK(is_red(rounded.get(1, 40)));
    CHECK(is_red(rounded.get(60, 40)));
}

#endif // _WIN32

TEST_CASE("Era window frame semantic goldens (.txt)", "[golden][semantic][window-frame]")
{
    HeadlessTextMeasurer m;
    const auto           check = [&](const std::string& name, const Theme& theme)
    {
        const std::string actual = std::string{"active:"} + '\n' + describe(*build_frame_scene(m, theme, true)) +
                                   "inactive:" + '\n' + describe(*build_frame_scene(m, theme, false));
        const std::string path   = frame_scene_path(name, ".txt");
        if (updating())
        {
            write_file(path, actual);
            SUCCEED("updated " << path);
            return;
        }
        const std::string expected = read_file(path);
        REQUIRE_FALSE(expected.empty());
        REQUIRE(actual == expected);
    };
    check("win95", windows9x_theme());
    check("win2000", windows2000_theme());
    check("win3x", windows3x_theme());
    check("win2x", windows2x_theme());
    check("win1x", windows1x_theme());
    check("luna", luna_theme());
    check("aero", aero_theme());
    check("mdl", mdl_theme());

    // Aero over a blurred desktop.
    const std::string actual = std::string{"active:"} + '\n' + describe(*build_frame_scene(m, aero_theme(), true, true)) +
                               "inactive:" + '\n' + describe(*build_frame_scene(m, aero_theme(), false, true));
    const std::string path = frame_scene_path("aero-glass", ".txt");
    if (updating())
    {
        write_file(path, actual);
        return;
    }
    REQUIRE(actual == read_file(path));
}

#ifdef _WIN32
TEST_CASE("Era window frame pixel goldens (.png) via Direct2D/DirectWrite", "[golden][pixel][window-frame]")
{
    ui::testing::OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    for (const auto& [name, theme] :
         {std::pair{std::string{"win95"}, windows9x_theme()},
          std::pair{std::string{"win2000"}, windows2000_theme()},
          std::pair{std::string{"win3x"}, windows3x_theme()},
          std::pair{std::string{"win2x"}, windows2x_theme()},
          std::pair{std::string{"win1x"}, windows1x_theme()},
          std::pair{std::string{"luna"}, luna_theme()},
          std::pair{std::string{"aero"}, aero_theme()},
          std::pair{std::string{"mdl"}, mdl_theme()}})
    {
        for (const bool active : {true, false})
        {
            const Bitmap actual = d2d.capture(*build_frame_scene(d2d.measurer(), theme, active),
                                              FrameSceneSize,
                                              theme.colors().background);
            check_pixel_golden(actual, frame_scene_path(name + (active ? "" : "-inactive"), ".png"));
        }
    }
    // Aero glass over the capture's plain backdrop (the window background), where the desktop
    // would be.
    for (const bool active : {true, false})
    {
        const Theme  theme  = aero_theme();
        const Bitmap actual = d2d.capture(*build_frame_scene(d2d.measurer(), theme, active, true),
                                          FrameSceneSize,
                                          theme.colors().background);
        check_pixel_golden(actual, frame_scene_path(std::string{"aero-glass"} + (active ? "" : "-inactive"), ".png"));
    }
}
#endif // _WIN32
