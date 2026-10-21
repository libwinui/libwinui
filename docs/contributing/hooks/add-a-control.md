# Add a Control

From an empty header to a gallery page.

This page walks the files a new control touches, in order, using `InfoBadge` as the running example. It is a small leaf control (a dot, a number or an icon on a colored pill) with a spec, a test file, a gallery page and a reference page, so every step shows up once. A control that owns children works the same way, plus what [The Tree](../how-it-works/the-tree.md) says about adopting them.

| Step | File |
| --- | --- |
| 1. Read the WinUI source | the control's `.idl`, `.xaml` and `_themeresources.xaml` |
| 2. Header | `include/winui/ui/status/InfoBadge.hpp` |
| 3. Source | `src/ui/status/InfoBadge.cpp`, plus `LIBWINUI_CORE_SOURCES` in `CMakeLists.txt` |
| 4. Theme chrome, if needed | `src/ui/themes/ControlChrome.hpp` and every era's derivation |
| 5. Umbrella header | `include/winui/winui.hpp` |
| 6. Spec aggregate | `include/winui/ui/status/InfoBadge.hpp`, after the control |
| 7. `build()` | `src/ui/status/InfoBadge.cpp` |
| 8. Tests | `tests/test_info_badge.cpp`, `tests/test_spec.cpp`, `tests/CMakeLists.txt` |
| 9. Golden, if worth it | `tests/test_golden.cpp`, `tests/golden/` |
| 10. Gallery page | `examples/gallery/pages_status_and_info.cpp` |
| 11. Reference page | `docs/tools/control_map.toml` |

## Start From WinUI

WinUI is the reference design, so start by reading how WinUI builds the control: its `.idl` for the public surface, its `.xaml` template for the parts, and its `_themeresources.xaml` for sizes, paddings and color keys. Keep WinUI's defaults and metrics. Name the spec after what the control is: a common name stays (`InfoBadge`, `Slider`), and a name that only makes sense in WinUI gets a plain one (`TextBlock` is `Text`, `RatingControl` is `Rating`). The code itself never mentions WinUI or its names, resource keys included: a doc comment describes each member in libwinui's own terms.

Where libwinui has no equivalent of a WinUI mechanism, map it onto what exists and describe that in the class comment. `InfoBadge`'s colored looks are XAML `Style` resources in WinUI; here they are an `InfoBadgeSeverity` enum, because `ui::Style` holds layout fields only.

## Header and .cpp

The header goes in `include/winui/ui/<group>/`, the source in `src/ui/<group>/`, mirrored. A control derives from `Element` and implements at least the two pure hooks:

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include "winui/render/Renderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

namespace ui
{

/// A small non-interactive badge showing a number, an icon or a plain dot.
class InfoBadgeControl final : public Element
{
public:
    /// A dot badge in the Default severity.
    InfoBadgeControl() = default;
    /// A badge showing `value` (clamped to >= 0).
    explicit InfoBadgeControl(int value);

    /// The number shown; unset (the default) shows the icon, or a dot when there is none.
    [[nodiscard]] std::optional<int> value() const
    {
        return value_;
    }
    /// Sets the number (clamped to >= 0) or clears it.
    void set_value(std::optional<int> value);

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Custom;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return value_ ? std::to_string(*value_) : std::string{};
    }

protected:
    /// The badge's size: a 4x4 dot, a 16x16 icon, or the text plus its margin, never narrower
    /// than tall.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// A fully rounded pill, then the number or the icon on top.
    void               render_override(Renderer& renderer, const RenderContext& context) const override;

private:
    std::optional<int> value_;
};

} // namespace ui
```

What `Element` expects:

| Hook | Required | Does |
| --- | --- | --- |
| `measure_override(available, context)` | yes | Returns the desired size. Margin, min/max size and alignment are already handled by `Element::measure()`. `ui::Unbounded` (+infinity) on an axis of `available` means unbounded, and 0 means no room; the result must be finite. Measure text only through `context.measurer`, and measure children with `child->measure(size, context)`. |
| `render_override(renderer, context)` | yes | Draws in layout coordinates, inside `bounds()`, with colors and metrics from `context.theme` (`theme.colors()` for role colors). A container draws its children with `child->render(renderer, context)`. The library draws the focus ring afterwards. |
| `arrange_override(rect, context)` | no | Positions children. A leaf needs nothing, since `bounds()` is already set. `context` holds the measurer and theme of the last measure. |
| `pointer_override(event)`, `wheel_override(event)`, `key_override(event)`, `text_override(event)` | no | Routed input: the element under the pointer (or the focused one) first, then each parent until one returns true. A handled press captures the pointer until the release. Positions are already mapped into layout coordinates. |
| `focusable()`, `focus_changed(bool)`, `enabled_changed()` | no | Takes part in the tab ring; `is_focused()` says whether it has focus and `is_enabled()` whether it and every ancestor are enabled. `focus_ring_rect(theme)`, `focus_ring_inside()` and `focus_ring_corner_radius(theme)` place the ring the window draws; `draws_focus_ring()` false leaves it out. |
| `automation_role()`, `automation_providers()`, `automation_name_fallback()` | no | What UI Automation sees and can do: the role, the provider interfaces the control inherits privately, and a name. A control with visible text returns it from `automation_name_fallback()`. |
| `tick_override(delta)` | no | Advances animations and waits; called on every tick of the window. A change marks the control dirty and keeps the window ticking. A wait that changes nothing until it ends calls `request_tick()`, or `request_tick_in(delay)` when its length is known: the window then sleeps until the wait ends and wakes once. |
| `on_screen_changed(on_screen)` | no | Called after a frame that scrolled the control into or out of view, clipped it away or stopped drawing it, once it called `set_screen_tracking(true)`; `on_screen()` holds the answer. A control whose drawing moves on its own pauses while it is off screen. |
| `for_each_child(visit)` | containers | Lists the children; every tree-wide walk (focus, popups, dirty clearing, ticks, automation, hit-testing) reaches them through it. A popup the control floats goes through `open_popup()`. |

In the `.cpp`, keep the metrics as named constants with a short comment on what each one sizes, and give every setter the equality gate and the narrowest dirty flags:

```cpp
namespace
{
constexpr float     MinSize        = 4.0f;                                               // the minimum width and height
constexpr float     BadgeMaxHeight = 16.0f;                                              // the maximum height
constexpr float     ValueFontSize  = 11.0f;                                              // the value's font size
constexpr Thickness ValueMargin    = Edges{.left = 4.0f, .right = 4.0f, .bottom = 2.0f}; // the value's margin
} // namespace

void InfoBadgeControl::set_value(std::optional<int> value)
{
    const std::optional<int> clamped = value ? std::optional<int>{std::max(0, *value)} : std::nullopt;
    if (clamped == value_)
    {
        return;
    }
    value_ = clamped;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBadgeControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const float radius = bounds().height * 0.5f;
    renderer.fill_rounded_rect(bounds(), radius, badge_background(theme, severity_));
    if (value_)
    {
        renderer.draw_text(std::to_string(*value_), bounds().inset(ValueMargin), badge_foreground(theme, severity_),
                           value_style(theme));
    }
}
```

A panel that owns a list of children derives from `Panel` (`include/winui/ui/panels/Panel.hpp`) instead, which brings the child storage, the background and `for_each_child()`. `Panel`'s border and padding setters are protected. A derived panel publishes them with `using Panel::set_padding;` and friends only when the reference design's panel has them, so `Canvas` has no padding. `Panel` itself is children plus a background. `Grid`, `StackControl`, `RelativePanel` and `LayoutPanel` add a border brush, border thickness, corner radius and padding, and `WrapPanel` the same apart from its own spacing fields. `StackControl` and `Grid` predate `Panel` and keep their own child storage.

Add the `.cpp` to `LIBWINUI_CORE_SOURCES` in the top-level `CMakeLists.txt`. A control that wraps a real Win32 window is the exception: its source goes into `LIBWINUI_WIN32_SOURCES` and its header next to it in the private `src/platform/windows/`.

## Theme Tokens

A control never hard-codes a color. It reads the `Theme` it is handed, so every era pack draws it in its own colors. If the tokens you need exist already (`InfoBadge` uses the role colors in `theme.colors()` and `theme.severity`), use them.

If not, the new field is the control's own chrome, not a design token: add it to `detail::ControlChrome` in `src/ui/themes/ControlChrome.hpp` (private, next to the slider, toggle and field records), read it with `detail::controls(theme)` and write it in the packs through `detail::edit_controls(theme)`. `Theme` in `include/winui/ui/themes/Theme.hpp` takes only what an app or a custom control would use too (a color role, a metric, an era-wide shape); a field one built-in control reads does not belong there, and keeping it out means touching it rebuilds only the files that include `ControlChrome.hpp`. Either way, fill it in for every era. That means `Theme::fluent()` and `Theme::classic()` in `src/ui/themes/Theme.cpp`, `Theme::metro()`, `Theme::mdl2()` and `Theme::early()` in their own `src/ui/themes/Theme<Era>.cpp`, the hand-built Luna and Aero resolvers and the high contrast resolvers in `src/ui/themes/ThemeSource.cpp`. A field that one era forgets silently keeps its default, which is usually Fluent Light, and that is exactly the kind of bug `tests/test_theme.cpp`'s cross-era cases ("No era pack sits on Fluent Light's ...") exist to catch. See [Theming](../how-it-works/theming.md) and [Add a Theme Era](add-a-theme-era.md).

## Umbrella Header

Add the header to `include/winui/winui.hpp`, in alphabetical order with the other `ui/` headers. Consumers usually include only that file.

## The Spec Aggregate

Every control gets its spec in the same pass. It goes into the control's own header, after the control class, and the header is named after the spec: `ui/status/InfoBadge.hpp` holds `ui::InfoBadge` and `ui::InfoBadgeControl`. The header includes the spec machinery it uses from `include/winui/ui/spec/` (`Common.hpp`, `Control.hpp`, `Spec.hpp`); that machinery never includes a control header back. The field order is fixed, because designated initializers must follow declaration order:

1. properties: the texts that label the control (`header`, `title`, a button's `text`, `description`, `placeholder_text`), the main value (`value`, `checked`, `items`, `selected_index`, `is_open`), the values that limit it (`min`, `max`, `step`, `max_length`), the other properties, then the appearance properties (`role`, `variant`, colors, fonts, `padding`),
2. appearance and layout: `style` and the eleven common fields, then `tool_tip`, then `enabled` if the control takes input, then `accessibility`,
3. behavior: `command`, `command_parameter`, the control's main event (`on_click`, `on_value_changed`), its other change events, a context event if it has one, then the generic events every spec ends its behavior block with (`on_pointer_entered`, `on_pointer_exited`, `on_got_focus`, `on_lost_focus`, `on_key_down`, `on_wheel`), which `ui::apply_common()` installs,
4. `control`,
5. children, then the template callables (`item`, `cell`, the `content` of `Match`, `Lazy` and `State`), always last.

```cpp
/// A small count, icon or dot badge. Builds a ui::InfoBadgeControl.
struct InfoBadge
{
    // Every spec starts with it: it keeps the spec from being initialized without designators.
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<InfoBadge>` points at.
    using control_type = InfoBadgeControl;

    // ---- properties ----
    /// The number; unset renders the icon, or the dot form when there is none.
    Binding<std::optional<int>> value{};
    /// The severity the background color comes from.
    Binding<InfoBadgeSeverity> severity{InfoBadgeSeverity::Default};
    /// The icon shown while `value` is unset; empty shows the plain dot.
    Binding<IconSource> icon{};
    /// A local background over the severity's color: a fixed color or a theme resource. Unset
    /// keeps the severity's.
    Binding<Paint> background{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};             ///< Shown when the pointer rests on it.
    Accessibility                     accessibility{};        ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there.
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there.
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<InfoBadge> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::InfoBadgeControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};
```

The common fields are copied flat into every spec, never inherited: an aggregate with a base class has to be initialized positionally, which is exactly what the spec layer exists to avoid. Copy the block from a neighboring spec so the comments match.

A property the caller may want to keep in sync with state is a `ui::Binding<T>`. A value that only makes sense at build time (a style picked once, a maximum fixed at construction) is a plain `T`. A child is `ui::Spec`, a list of children `ui::SpecList`. Pick the name callers would use; it does not have to be the control's (`ui::Rating` builds a `RatingControl`).

## build()

`build()` lives in the control's `.cpp`, after the control's own members, and always runs in the same order: allocate, apply values and bindings, install handlers, apply the common fields, publish `.control`.

```cpp
std::unique_ptr<ui::Element> InfoBadge::build() &&
{
    auto node = std::make_unique<ui::InfoBadgeControl>();

    apply(value, *node, [raw = node.get()](const std::optional<int>& v) { raw->set_value(v); });
    apply(severity, *node, [raw = node.get()](const ui::InfoBadgeSeverity& v) { raw->set_severity(v); });
    apply(icon, *node, [raw = node.get()](const ui::IconSource& v) { raw->set_icon(v); });
    apply(background,
          *node,
          [raw = node.get()](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}
```

`apply()` calls the setter once with the initial value, and for a `bind(...)` subscribes the node so the setter runs again on every change. The subscription belongs to the node and dies with it. Handlers are installed after the values, so seeding a value never fires a change handler. A control with an editable field installs its change handler after the `apply()` calls, through `ui::write_back()`, which writes a two-way property and skips the spec's handler for a change that came from a binding:

```cpp
const auto write = ui::shared_writer(value);
// ... apply(value, ...) and the other values ...
if (auto changed = ui::write_back(write, std::move(on_value_changed)))
{
    node->on_value_changed(std::move(changed));
}
```

## Tests

Create `tests/test_info_badge.cpp` and add it to the `add_executable(winui_tests ...)` list in `tests/CMakeLists.txt`. Tag every case with the control's name in kebab case plus the area. Cover:

- **Defaults and setters**: the WinUI defaults, clamping, and that a setter with an unchanged value leaves the control clean (`clear_dirty()`, set, `REQUIRE_FALSE(is_dirty())`).
- **Layout**: `measure()` with `HeadlessTextMeasurer` and `Theme::light()`, checked against WinUI's metrics.
- **Rendering**: the recorded commands from `TestRenderer`, per state.
- **Input and focus**: through `HeadlessUI` and `Window::handle_pointer()` / `handle_key()` for an interactive control.
- **Automation**: role, patterns, name.

```cpp
TEST_CASE("Dot mode renders exactly one filled pill and no text", "[info-badge][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl badge;
    const Size       desired = badge.measure({100.0f, Unbounded}, m, theme);
    badge.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    badge.render(r, theme);
    REQUIRE(r.count() == 1);
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
}
```

Then add a case to `tests/test_spec.cpp` that builds the spec with a `bind(...)` for each bindable field and a `.control`, and checks that each value arrives and follows a change. [Write Tests](../development/write-tests.md) has the details.

## Golden

Most controls need no golden of their own. The semantic goldens exist for scenes, and a control's rendering is already pinned by its `TestRenderer` tests. Add one when a control is visually central and many states are easier to review as a dump than as assertions, or when the control uses a Direct2D path nothing else exercises. In that case add it to `tests/test_golden.cpp` as described in [Write Tests](../development/write-tests.md#adding-a-golden).

## Gallery Page

The gallery has one page per control, in the `pages_<group>.cpp` file of the navigation group it belongs to. A page is a function that takes the shared `Context&` and returns a `ui::Spec`, made of example cards. Every card carries the code that makes its control in `.code`, which the card shows in a "Source code" expander:

```cpp
ui::Spec info_badge_page(Context&)
{
    return examples({
        example({
            .title    = "InfoBadge with a value",
            .code   = R"cpp(ui::InfoBadge{.value = 7})cpp",
            .output = ui::InfoBadge{.value = 7},
        }),
        example({
            .title    = "Severity styles",
            .code   = R"cpp(ui::InfoBadge{.severity = ui::InfoBadgeSeverity::Critical, .icon = ...})cpp",
            .output = ui::InfoBadge{
                .severity = ui::InfoBadgeSeverity::Critical,
                .icon     = ui::InfoBadgeControl::severity_glyph(ui::InfoBadgeSeverity::Critical)},
        }),
    });
}
```

No compiler sees `.code`. The `gallery_snippets` test (`tools/check_snippets.cpp`) checks that every field a snippet names exists on its spec and comes in declaration order, that a snippet uses none of the gallery's own helpers or page state, and that every page has its paragraph in `page_about.cpp`.

A card with knobs keeps its state in a struct through `with_state<S>(...)`, so the properties live exactly as long as the page. `with_state` returns a `StateHost` that inherits the state slot as its first base and `ContentHost` as its second. Bases are destroyed in reverse order, so the subtree goes first and the state last; a member would die before the bases and leave controls pointing at freed state for a moment. That is why a non-owning observer like a `RadioGroup` can simply be a member of `S`.

How the gallery is put together, so a page fits in:

- **It is laid out like the WinUI 3 Gallery app, in libwinui's names.** A grouped navigation pane on the left (itself a `NavigationShell`, built by `build_nav_pane()` in `examples/gallery/main.cpp`), one page per control on the right, each page a title, a one-line description and a stack of example cards. WinUI-Gallery keeps its navigation in `SampleSupport/Data/ControlInfoData.json`; here each navigation group is one `pages_<group>.cpp` with a static table of `Page`s, and `groups()` in `gallery.cpp` lists them in that file's group order. Groups libwinui has no control for (Fundamentals, Design, Accessibility, Shell) are left out. "Native Controls" is libwinui's own group for the `HWND`-hosting controls. Group and page names are in Title Case ("Basic Input", "Window States"); a page named after a type keeps the type's spelling (`TableView`), and card titles stay sentence case.
- **A page copies its WinUI-Gallery sample folder** (`WinUIGallery/Samples/<Control>/`) one for one, in WinUI's order, with the same cards (the sample `.txt` headers) and the same data from the sample's code-behind (the Contacts.txt people, the photos, the recipes). The description in the page table is the control's subtitle from `ControlInfoData.json`. Everything a user reads names the control as libwinui does: a page is named after its spec (`Text`, `Dialog`, `Stack`, `TileView`, `Rating`), not after the WinUI class (`TextBlock`, `ContentDialog`, `StackPanel`, `GridView`, `RatingControl`), and card titles and sample text follow. Titles that name a XAML concept (DataTemplate, ItemsSource) are reworded into libwinui terms too. A WinUI example or option libwinui cannot express is left out, not faked, and libwinui's own controls with no WinUI counterpart (`Splitter`, `TableView`, ...) are appended at the end of their group.
- **Pages are built on demand.** The content pane is a `ui::Match` over the selected page index, so a page is built when you navigate to it and destroyed when you leave. A handler on one page can never reach a control on another, so everything a page needs, it builds. State that has to survive navigation (the status strip, the theme request, the window) lives in `gallery::Context`, which is created in `main()` and outlives the window, so page handlers may capture references into it. Page factories are plain function pointers and capture nothing.
- **Navigation is derived both ways.** The pane writes the selected page index, and any other writer (a "see the X sample" link through `Context::navigate`) moves the pane. Group rows use `selects_on_invoke = false`, so pane item indices and page indices differ, and `NavMaps` maps between them.
- **The title bar searches the pages.** An `AutoSuggestBox` in the header, focused with Ctrl+F, lists the pages whose name, description or group holds every typed word (`search_pages()` in `gallery.cpp`); picking one goes to it, and Enter on typed text shows a results page with the hits filtered by group. A clear one-line description makes a new page easy to find.
- **No shipped media.** libwinui ships only originally authored assets, so every sample photo, GIF, SVG or Lottie logo is drawn in code (`placeholder_photo()`, `sample_photo(i)`, ...), sized like the asset it replaces so the page lays out the same.
- **Card layout.** A card with a single control does not put it in a horizontal row: a horizontal `StackControl` offers unbounded width, so an `InfoBar` or a wrapping label could never wrap. The control is measured against the card width instead. A row of several controls centers them on the cross axis, since the Stretch default would grow each to the tallest. The controls cell clips to its bounds, so a control wider than its column does not paint over the options panel.
- **Windows opened from a page outlive it.** A Windowing card creates a `ui::Window`, which the gallery's `Context` owns, and shows it with `Window::show()` on the gallery's message loop; navigating away leaves that window open. Nothing in its content may capture page state by reference.

Register the page in the group's table at the bottom of the file:

```cpp
std::span<const Page> status_and_info_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"InfoBadge", "A non-intrusive UI to display notifications or bring focus to an area.", info_badge_page},
        // ...
    });
    return Pages;
}
```

## Reference Page

Add a `[[page]]` to `docs/tools/control_map.toml` so the control and its spec share one page in the right group:

```toml
[[page]]
section = "controls"
slug = "info-badge"
title = "InfoBadge"
group = "Status and Info"
specs = ["ui::InfoBadge"]
control = "ui::InfoBadgeControl"
headers = ["ui/status/InfoBadge.hpp"]
tests = ["tests/test_info_badge.cpp"]
```

Controls that are really containers go into `section = "layout"` with one of its groups. Add `related` for helper types that belong on the same page (`RadioButton` lists `ui::RadioGroup`), and `example = "path:region"` to show a tutorial snippet. Then regenerate with `python docs/tools/gen_reference.py --skip-doxygen` after a Doxygen run, or the `docs-reference` target; see [Documentation](../development/docs.md#the-reference-generator).
