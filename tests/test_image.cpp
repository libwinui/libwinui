// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/media/Image.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{

// Lays `view` out in `slot` the way a panel would: measure against the slot, arrange into it.
void layout(ImageControl& view, Rect slot)
{
    HeadlessTextMeasurer measurer;
    (void)view.measure(Size{slot.width, slot.height}, measurer, Theme::default_light());
    view.arrange(slot);
}

std::vector<DrawImageRect> image_draws(const ImageControl& view)
{
    TestRenderer renderer;
    view.render(renderer, Theme::light());
    std::vector<DrawImageRect> draws;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* d = std::get_if<DrawImageRect>(&c))
        {
            draws.push_back(*d);
        }
    }
    return draws;
}

// A 400x200 picture: 2:1, so the Uniform and UniformToFill cases below pick different axes.
Bitmap photo()
{
    return Bitmap(400, 200, colors::Black);
}

// Three 2x2 frames, red, blue and green, with delays of 5, 0 and 12 hundredths and a NETSCAPE2.0
// loop count of 3 (written by Pillow).
constexpr std::array<std::uint8_t, 150>
    AnimatedGif{0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xFF, 0x0B, 0x4E, 0x45, 0x54, 0x53, 0x43, 0x41,
                0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x03, 0x00, 0x00, 0x21, 0xF9, 0x04, 0x04, 0x05, 0x00, 0x00,
                0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x08, 0x06, 0x00, 0x01, 0x08, 0x04,
                0x10, 0x10, 0x00, 0x21, 0xF9, 0x04, 0x05, 0x00, 0x00, 0x01, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02,
                0x00, 0x02, 0x00, 0x81, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
                0x06, 0x00, 0x01, 0x08, 0x04, 0x10, 0x10, 0x00, 0x21, 0xF9, 0x04, 0x05, 0x0C, 0x00, 0x01, 0x00, 0x2C,
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x08, 0x06, 0x00, 0x01, 0x08, 0x04, 0x10, 0x10, 0x00, 0x3B};

// A single-frame GIF87a of the red frame alone: no NETSCAPE block.
constexpr std::array<std::uint8_t, 45> StillGif{0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00,
                                                0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x08,
                                                0x06, 0x00, 0x01, 0x08, 0x04, 0x10, 0x10, 0x00, 0x3B};

AnimatedImage three_frames(int loop_count)
{
    AnimatedImage image;
    image.frames     = {Bitmap(4, 4, colors::White), Bitmap(4, 4, colors::Black), Bitmap(4, 4, colors::White)};
    image.delays     = {100ms, 50ms, 200ms};
    image.loop_count = loop_count;
    return image;
}

} // namespace

// ---- layout ----

TEST_CASE("ImageControl measures its picture's natural size when nothing bounds it", "[image-view][layout]")
{
    ImageControl         view{photo()};
    HeadlessTextMeasurer measurer;
    const Size           desired = view.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light());
    REQUIRE(desired.width == 400.0f);
    REQUIRE(desired.height == 200.0f);
}

TEST_CASE("An empty ImageControl measures and draws nothing", "[image-view][layout]")
{
    ImageControl         view;
    HeadlessTextMeasurer measurer;
    REQUIRE(view.measure(Size{100.0f, 100.0f}, measurer, Theme::default_light()) == Size{});
    view.arrange(Rect{0.0f, 0.0f, 100.0f, 100.0f});
    TestRenderer renderer;
    view.render(renderer, Theme::light());
    REQUIRE(renderer.count() == 0);
}

TEST_CASE("A single unbounded axis follows the other's scale", "[image-view][layout]")
{
    ImageControl         view{photo()};
    HeadlessTextMeasurer measurer;
    // Height=100: 100 tall, the width follows the 0.5 scale.
    view.set_height(100.0f);
    const Size desired = view.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light());
    REQUIRE_THAT(desired.width, WithinAbs(200.0f, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(100.0f, 1e-3));
}

TEST_CASE("Uniform fits and centers the picture in a wide slot", "[image-view][layout]")
{
    ImageControl view{photo()};
    layout(view, Rect{0.0f, 0.0f, 800.0f, 100.0f});
    // Uniform takes the smaller scale (100/200): 200x100, centered in the 800 slot.
    const ImageControl::Placement p = view.placement();
    REQUIRE_THAT(p.content.x, WithinAbs(300.0f, 1e-3));
    REQUIRE_THAT(p.content.width, WithinAbs(200.0f, 1e-3));
    REQUIRE_THAT(p.content.height, WithinAbs(100.0f, 1e-3));

    const auto draws = image_draws(view);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws[0].source == Rect{0.0f, 0.0f, 400.0f, 200.0f});
    REQUIRE_THAT(draws[0].bounds.x, WithinAbs(300.0f, 1e-3));
}

TEST_CASE("Fill stretches both axes to the slot", "[image-view][layout]")
{
    ImageControl view{photo()};
    view.set_stretch(Stretch::Fill);
    layout(view, Rect{10.0f, 20.0f, 100.0f, 100.0f});
    const ImageControl::Placement p = view.placement();
    REQUIRE(p.content == Rect{10.0f, 20.0f, 100.0f, 100.0f});
    REQUIRE(p.visible == Rect{10.0f, 20.0f, 100.0f, 100.0f});
}

TEST_CASE("UniformToFill with an explicit size shows the scaled picture's top-left", "[image-view][layout]")
{
    // Width=100 Height=100 over a 2:1 picture: the ink is 200x100 (scale 0.5), and the max_width clip
    // keeps its top-left 100x100.
    ImageControl view{photo()};
    view.set_stretch(Stretch::UniformToFill);
    view.set_width(100.0f);
    view.set_height(100.0f);
    view.set_horizontal_alignment(Alignment::Leading);
    view.set_vertical_alignment(Alignment::Leading);
    layout(view, Rect{0.0f, 0.0f, 500.0f, 500.0f});

    const ImageControl::Placement p = view.placement();
    REQUIRE(p.content == Rect{0.0f, 0.0f, 200.0f, 100.0f});
    REQUIRE(p.visible == Rect{0.0f, 0.0f, 100.0f, 100.0f});

    const auto draws = image_draws(view);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws[0].source == Rect{0.0f, 0.0f, 200.0f, 200.0f}); // the left half of the picture
    REQUIRE(draws[0].bounds == Rect{0.0f, 0.0f, 100.0f, 100.0f});
}

TEST_CASE("Stretch None draws at natural size and a smaller element crops from the top-left", "[image-view][layout]")
{
    ImageControl view{photo()};
    view.set_stretch(Stretch::None);
    view.set_width(100.0f);
    view.set_height(100.0f);
    view.set_horizontal_alignment(Alignment::Leading);
    view.set_vertical_alignment(Alignment::Leading);
    layout(view, Rect{0.0f, 0.0f, 500.0f, 500.0f});

    const auto draws = image_draws(view);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws[0].source == Rect{0.0f, 0.0f, 100.0f, 100.0f});
    REQUIRE(draws[0].bounds == Rect{0.0f, 0.0f, 100.0f, 100.0f});
}

TEST_CASE("Stretch None in a bigger slot centers the picture", "[image-view][layout]")
{
    ImageControl view{Bitmap(20, 10, colors::Black)};
    view.set_stretch(Stretch::None);
    layout(view, Rect{0.0f, 0.0f, 100.0f, 50.0f});
    const ImageControl::Placement p = view.placement();
    REQUIRE(p.content == Rect{40.0f, 20.0f, 20.0f, 10.0f});
}

TEST_CASE("A nine-grid keeps the corners and stretches the edges and center", "[image-view][render]")
{
    ImageControl view{Bitmap(80, 80, colors::Black)};
    view.set_nine_grid(Symmetric{.horizontal = 30.0f, .vertical = 20.0f});
    view.set_height(160.0f);
    HeadlessTextMeasurer measurer;
    // Unbounded: Height=160 alone sizes it, 160x160.
    REQUIRE(view.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light()) == Size{160.0f, 160.0f});
    view.arrange(Rect{0.0f, 0.0f, 160.0f, 160.0f});

    const auto draws = image_draws(view);
    REQUIRE(draws.size() == 9);
    REQUIRE(draws[0].source == Rect{0.0f, 0.0f, 30.0f, 20.0f});
    REQUIRE(draws[0].bounds == Rect{0.0f, 0.0f, 30.0f, 20.0f});
    REQUIRE(draws[4].source == Rect{30.0f, 20.0f, 20.0f, 40.0f});
    REQUIRE(draws[4].bounds == Rect{30.0f, 20.0f, 100.0f, 120.0f});
    REQUIRE(draws[8].bounds == Rect{130.0f, 140.0f, 30.0f, 20.0f});
}

TEST_CASE("ImageControl draws an SVG through draw_svg, clipped only when it overflows", "[image-view][render]")
{
    ImageControl         view{SvgImage{R"(<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20"></svg>)"}};
    HeadlessTextMeasurer measurer;
    REQUIRE(view.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light()) == Size{40.0f, 20.0f});

    view.arrange(Rect{0.0f, 0.0f, 80.0f, 80.0f});
    TestRenderer renderer;
    view.render(renderer, Theme::light());
    REQUIRE(renderer.count() == 1);
    const auto* svg = std::get_if<DrawSvg>(&renderer.commands[0]);
    REQUIRE(svg != nullptr);
    REQUIRE(svg->natural_size == Size{40.0f, 20.0f});
    REQUIRE(svg->bounds == Rect{0.0f, 20.0f, 80.0f, 40.0f}); // Uniform: 80x40, centered

    view.set_stretch(Stretch::UniformToFill);
    renderer.clear();
    view.render(renderer, Theme::light());
    REQUIRE(renderer.count_of<PushClip>() == 1);
}

// ---- animation ----

TEST_CASE("An auto-playing animated source steps through its frames by their delays", "[image-view][animation]")
{
    ImageControl view{three_frames(0)};
    REQUIRE(view.is_animated());
    REQUIRE(view.is_playing());
    REQUIRE(view.current_frame() == 0);

    view.tick(99ms);
    REQUIRE(view.current_frame() == 0);
    view.tick(1ms);
    REQUIRE(view.current_frame() == 1);
    view.tick(50ms);
    REQUIRE(view.current_frame() == 2);
    view.tick(200ms); // loop_count 0: forever
    REQUIRE(view.current_frame() == 0);
}

TEST_CASE("An animation plays loop_count times and holds its last frame", "[image-view][animation]")
{
    ImageControl view{three_frames(2)};
    view.tick(350ms); // one full pass
    REQUIRE(view.current_frame() == 0);
    view.tick(150ms);
    REQUIRE(view.current_frame() == 2);
    view.tick(Duration{10'000.0}); // the second pass ends on the last frame
    REQUIRE(view.current_frame() == 2);
    REQUIRE(view.is_playing()); // is_playing() is only what play()/stop() last said
}

TEST_CASE("auto_play off waits on the first frame until play(); stop() rewinds", "[image-view][animation]")
{
    AnimatedImage source = three_frames(0);
    source.auto_play     = false;
    ImageControl view{std::move(source)};
    REQUIRE_FALSE(view.is_playing());
    view.tick(Duration{1'000.0});
    REQUIRE(view.current_frame() == 0);

    view.play();
    REQUIRE(view.is_playing());
    view.tick(120ms);
    REQUIRE(view.current_frame() == 1);

    view.stop();
    REQUIRE_FALSE(view.is_playing());
    REQUIRE(view.current_frame() == 0);
    view.tick(Duration{1'000.0});
    REQUIRE(view.current_frame() == 0);
}

namespace
{
constexpr Size Client{400.0f, 300.0f};

// What a host does for one frame: lay out, then the measuring pass that finds what is on screen.
void frame(HeadlessUI& ui)
{
    ui.layout(Client);
    (void)ui.window().take_damage();
}
} // namespace

TEST_CASE("A playing animation wakes an idle window exactly on its frame delays", "[image-view][animation][tick]")
{
    HeadlessUI ui;
    ui.show(Client);
    ImageControl* view = ui.window().set_content(std::make_unique<ImageControl>(three_frames(0)));
    frame(ui);
    (void)ui.tick(30ms);
    REQUIRE(view->current_frame() == 0);
    REQUIRE(ui.window().next_wake() == 70ms); // what is left of the first frame's 100 ms

    // Ticked only when it asks to be: each frame shows for its own delay, however short.
    std::vector<std::size_t> frames;
    for (int i = 0; i < 4; ++i)
    {
        const std::optional<Duration> wait = ui.window().next_wake();
        REQUIRE(wait.has_value());
        (void)ui.tick(Duration{*wait});
        frame(ui);
        frames.push_back(view->current_frame());
    }
    CHECK(frames == std::vector<std::size_t>{1, 2, 0, 1});
    CHECK(ui.window().next_wake() == 50ms);
}

TEST_CASE("An animation that played its loops asks for no more ticks", "[image-view][animation][tick]")
{
    HeadlessUI ui;
    ui.show(Client);
    ImageControl* view = ui.window().set_content(std::make_unique<ImageControl>(three_frames(1)));
    frame(ui);
    (void)ui.tick(350ms);
    REQUIRE(view->current_frame() == 2);
    CHECK_FALSE(ui.window().next_wake().has_value());
}

TEST_CASE("An animation scrolled out of view stands still, and goes on where it was when scrolled back",
          "[image-view][animation][tick]")
{
    HeadlessUI ui;
    ui.show(Client);
    auto* scroll = ui.window().set_content(std::make_unique<ScrollControl>());
    auto* stack  = scroll->set_child(std::make_unique<StackControl>(Orientation::Vertical));
    stack->add(std::make_unique<SpacerControl>())->set_height(1000.0f);
    ImageControl* view = stack->add(std::make_unique<ImageControl>(three_frames(0)));

    frame(ui); // below the fold
    REQUIRE_FALSE(view->on_screen());
    for (int i = 0; i < 3; ++i)
    {
        (void)ui.tick(1000ms);
    }
    CHECK(view->current_frame() == 0);
    CHECK_FALSE(ui.window().next_wake().has_value());
    CHECK_FALSE(ui.window().wants_ticks());

    scroll->scroll_to(0.0f, 1000.0f, {.animation_mode = ScrollAnimationMode::Disabled});
    frame(ui);
    REQUIRE(view->on_screen());
    REQUIRE(ui.window().next_wake() == 0ms); // the frame that shows it wakes it
    (void)ui.tick(5000ms);                   // the time it spent off screen does not count
    CHECK(view->current_frame() == 0);
    CHECK(ui.window().next_wake() == 100ms);
    (void)ui.tick(100ms);
    CHECK(view->current_frame() == 1);
    (void)ui.tick(20ms);

    // Away again on the second frame, 20 ms into its 50.
    scroll->scroll_to(0.0f, 0.0f, {.animation_mode = ScrollAnimationMode::Disabled});
    frame(ui);
    REQUIRE_FALSE(view->on_screen());
    (void)ui.tick(1000ms);
    CHECK(view->current_frame() == 1);
    CHECK_FALSE(ui.window().next_wake().has_value());

    scroll->scroll_to(0.0f, 1000.0f, {.animation_mode = ScrollAnimationMode::Disabled});
    frame(ui);
    (void)ui.tick(1000ms);
    CHECK(view->current_frame() == 1);
    CHECK(ui.window().next_wake() == 30ms);
    (void)ui.tick(30ms);
    CHECK(view->current_frame() == 2);
}

TEST_CASE("play() and stop() do nothing to a still source", "[image-view][animation]")
{
    ImageControl view{photo()};
    REQUIRE_FALSE(view.is_animated());
    view.play();
    REQUIRE_FALSE(view.is_playing());
}

// ---- decoding ----

TEST_CASE("AnimatedImage::decode reads every GIF frame, the delays and the loop count", "[image-view][decode]")
{
    const AnimatedImage image = AnimatedImage::decode(AnimatedGif);
    REQUIRE(image.frames.size() == 3);
    REQUIRE(image.width() == 2);
    REQUIRE(image.height() == 2);
    REQUIRE(image.animated());
    REQUIRE(image.delays == std::vector<Duration>{50ms, 100ms, 120ms}); // 0 hundredths shows for 100 ms
    REQUIRE(image.loop_count == 3);
    REQUIRE(image.frames[0].get(0, 0) == Color::from_rgba(255, 0, 0));
    REQUIRE(image.frames[1].get(1, 1) == Color::from_rgba(0, 0, 255));
    REQUIRE(image.frames[2].get(0, 1) == Color::from_rgba(0, 255, 0));
}

TEST_CASE("A GIF without a NETSCAPE block plays once", "[image-view][decode]")
{
    const AnimatedImage image = AnimatedImage::decode(StillGif);
    REQUIRE(image.frames.size() == 1);
    REQUIRE_FALSE(image.animated());
    REQUIRE(image.loop_count == 1);
}

// ---- SVG ----

TEST_CASE("svg_natural_size reads width and height, falling back to the viewBox", "[image-view][svg]")
{
    REQUIRE(svg_natural_size(R"(<svg width="120" height="80"/>)") == Size{120.0f, 80.0f});
    REQUIRE(svg_natural_size(R"(<svg width="120px" height="1in"/>)") == Size{120.0f, 96.0f});
    REQUIRE(svg_natural_size(R"(<svg viewBox="0 0 300 150"/>)") == Size{300.0f, 150.0f});
    REQUIRE(svg_natural_size(R"(<svg width="60" viewBox="0,0,300,150"/>)") == Size{60.0f, 30.0f});
    REQUIRE(svg_natural_size(R"(<?xml version="1.0"?><!-- a comment --><svg:svg height="10" viewBox="0 0 4 2"/>)") ==
            Size{20.0f, 10.0f});
    REQUIRE(svg_natural_size(R"(<svg width="100%" height="100%"/>)") == Size{});
    REQUIRE(svg_natural_size("not markup") == Size{});
}

// ---- spec ----

TEST_CASE("ui::Image builds an ImageControl and binds its source", "[image-view][spec]")
{
    Property<int>          which{0};
    ui::Control<ui::Image> view;
    auto                   node = ui::build(ui::Image{
        .source  = bind(which, [](int i) -> ImageSource { return Bitmap(i == 0 ? 10 : 20, 10, colors::Black); }),
        .stretch = Stretch::Fill,
        .control = view,
    });
    REQUIRE(view != nullptr);
    REQUIRE(view->stretch() == Stretch::Fill);
    REQUIRE(view->source().natural_size() == Size{10.0f, 10.0f});
    which = 1;
    REQUIRE(view->source().natural_size() == Size{20.0f, 10.0f});
}

TEST_CASE("ImageSource::load() decodes a file in the background", "[image][load]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "winui_test_image_load.png";
    Bitmap{6, 4, colors::Black}.write_png(path);

    const ImageSource source = ImageSource::load(path);
    for (int i = 0; i < 500 && source.load_state() == ImageLoadState::Loading; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    REQUIRE(source.load_state() == ImageLoadState::Loaded);
    REQUIRE(source.bitmap() != nullptr);
    REQUIRE(source.natural_size() == Size{6.0f, 4.0f});
    const ImageSource copy = source; // shares the decode
    REQUIRE(copy == source);
    std::filesystem::remove(path);
}

TEST_CASE("ImageSource::load() of a file that is not there fails with a reason", "[image][load]")
{
    const ImageSource source = ImageSource::load(std::filesystem::temp_directory_path() / "winui_no_such_picture.png");
    for (int i = 0; i < 500 && source.load_state() == ImageLoadState::Loading; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    REQUIRE(source.load_state() == ImageLoadState::Failed);
    REQUIRE_FALSE(source.load_error().empty());
    REQUIRE(source.bitmap() == nullptr);
    REQUIRE(source.empty());
}

TEST_CASE("An Image shows a loaded picture once it is decoded and says so", "[image][load]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "winui_test_image_opened.png";
    Bitmap{8, 8, colors::White}.write_png(path);

    HeadlessUI ui     = create_headless();
    int        opened = 0;
    ui.window().set_content(ui::Image{.source = ImageSource::load(path), .on_image_opened = [&] { ++opened; }});
    ui.layout({200.0f, 200.0f});
    for (int i = 0; i < 500 && opened == 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        (void)ui.tick(10ms);
    }
    REQUIRE(opened == 1);
    REQUIRE(ui.window().content()->is_dirty(DirtyFlags::Measure)); // it lays itself out again
    ui.layout({200.0f, 200.0f});
    REQUIRE(static_cast<ImageControl*>(ui.window().content())->source().bitmap() != nullptr);
    std::filesystem::remove(path);
}

namespace
{
// A picture big enough that its decode is still running when the window first ticks.
std::filesystem::path write_slow_picture(const char* name)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    Bitmap                      big{1500, 1500, colors::White};
    for (int y = 0; y < big.height(); y += 3)
    {
        for (int x = 0; x < big.width(); x += 7)
        {
            big.set(x, y, Color::from_argb(0xFF000000u | static_cast<std::uint32_t>(x * 977 + y * 131)));
        }
    }
    big.write_png(path);
    return path;
}

void wait_for(const std::function<bool()>& done)
{
    for (int i = 0; i < 1000 && !done(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}
} // namespace

TEST_CASE("A loading Image asks for no ticks, and the decode wakes its window once", "[image][load][tick]")
{
    const std::filesystem::path path = write_slow_picture("winui_test_image_wake.png");

    HeadlessUI        ui     = create_headless();
    int               opened = 0;
    const ImageSource source = ImageSource::load(path); // shares the decode with the Image's copy
    ui.window().set_content(ui::Image{.source = source, .on_image_opened = [&] { ++opened; }});
    ui.show({200.0f, 200.0f});
    (void)ui.tick(16ms); // the Image asks the decode to wake the window
    if (source.load_state() != ImageLoadState::Loading)
    {
        wait_for([&] { return source.load_state() != ImageLoadState::Loading; });
        std::filesystem::remove(path);
        SKIP("the picture decoded before the first tick");
    }
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE_FALSE(ui.window().next_wake().has_value());

    const int wakes = ui.host().wake_requests;
    wait_for([&] { return source.load_state() != ImageLoadState::Loading && ui.host().wake_requests > wakes; });
    REQUIRE(source.load_state() == ImageLoadState::Loaded);
    REQUIRE(ui.host().wake_requests == wakes + 1);
    REQUIRE(opened == 0); // nothing runs until the window ticks
    (void)ui.tick(16ms);
    REQUIRE(opened == 1);
    std::filesystem::remove(path);
}

TEST_CASE("A decode that finishes after its Image or its window is gone does no harm", "[image][load][tick]")
{
    const std::filesystem::path path   = write_slow_picture("winui_test_image_gone.png");
    const ImageSource           source = ImageSource::load(path);
    {
        HeadlessUI ui = create_headless();
        ui.window().set_content(ui::Image{.source = source});
        ui.show({200.0f, 200.0f});
        (void)ui.tick(16ms);
        ui.window().set_content(std::make_unique<TextControl>("Gone")); // the Image goes first
        (void)ui.tick(16ms);

        HeadlessUI closed = create_headless();
        closed.window().set_content(ui::Image{.source = source});
        closed.show({200.0f, 200.0f});
        (void)closed.tick(16ms);
    } // both windows go before the decode is done, or at least may
    wait_for([&] { return source.load_state() != ImageLoadState::Loading; });
    std::this_thread::sleep_for(std::chrono::milliseconds{50}); // the worker's posts, dropped
    REQUIRE(source.load_state() == ImageLoadState::Loaded);
    std::filesystem::remove(path);
}
