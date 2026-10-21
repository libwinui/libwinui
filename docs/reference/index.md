# Reference

The public API, generated from the headers: one page per control, grouped by what it is for, and one page per type or namespace everywhere else.

## How a Reference Page Is Laid Out

A control page puts the description and the control it builds side by side, in this order:

1. **Purpose.** The header to include, the spec's header, and what the control is.
2. **Example.** A short snippet showing the control in use, often both declared and hand-built.
3. **The spec** (`ui::Button`). Its fields in declaration order, which is the order designators have to follow, as a table of name, type, default and description. The common fields every spec shares are folded into one line that links to [Conventions](conventions.md#common-fields-on-every-spec). Then `build()`.
4. **The control** (`ui::ButtonControl`). What it derives from, its description, then its public members: constructors, getters and setters, event hooks, and the `Element` overrides it implements.
5. **Related types.** Helper classes and enums that belong to the control, such as `ButtonVariant` on the Button page or `RadioGroup` on the RadioButton page.
6. **Example and tests.** A snippet from a tutorial, where one exists, and the test files that cover the control. The tests are the most complete usage examples there are.

Every other page is a single class or struct, laid out the same way as the control part above, or a namespace page listing its free functions, enumerations, aliases and constants.

## Namespaces

| Namespace | What is in it | Where it shows up |
| --- | --- | --- |
| `ui` | Everything an app writes. The specs, one aggregate per control (`ui::Button`, `ui::VerticalStack`, `ui::Text`, ...), with `Spec`, `Control<>`, `Style` and `ui::build()`. The retained tree they build, with the controls (`ui::ButtonControl`), `Element`, `Window`, `Property` and `bind()`, geometry and color, the `Renderer` interface and `Theme`. | Controls and Layout for the specs and controls, the other sections for the rest. |
| `ui::testing` | The testing tier, opt-in through `<winui/testing.hpp>`: `HeadlessUI`, `TestRenderer`, the test doubles for the platform seams, `OffscreenCapture` and `seams()`. | Testing. |

Smaller namespaces hold constants: `ui::colors`, `ui::glyphs`, `ui::styles` and `ui::easing`.

The library is libwinui and its headers are under `winui/`. The namespace is `ui`.

## Specs Versus Controls

`ui::Button` describes a button. It is a plain aggregate you fill in with designated initializers, hand to a parent or a window, and never see again. `ui::ButtonControl` is the control that `build()` makes from it: a node in the tree, with state, setters and invariants. When you need to talk to a built control later, the spec's `.control` field fills a `ui::Control<ui::Button>` handle that reaches it.

The two are documented on the same page because you use them together. The control's name is the spec's name plus `Control` (`ui::Text` builds a `ui::TextControl`, `ui::Rating` a `ui::RatingControl`); [Conventions](conventions.md#naming) lists the few pairs that differ, such as `ui::VerticalStack` and `ui::HorizontalStack` both building a `ui::StackControl`.

Every control can also be built by hand, and every spec field has a matching setter on the control. [Mixing Imperative and Declarative](../tutorials/extending-the-tree/03-mixing-imperative-and-declarative.md) shows both together.

## Coming From WinUI or WPF

If you know a control by its XAML name, this table gives the libwinui spec for it. A spec keeps the short name and its control is that name plus `Control`.

| You are looking for | In libwinui |
| --- | --- |
| `StackPanel` | [`ui::VerticalStack`, `ui::HorizontalStack`](layout/stack.md) |
| `TextBlock`, `RichTextBlock` | [`ui::Text`](controls/text.md), [`ui::RichText`](controls/rich-text.md) |
| `ContentDialog` | [`ui::Dialog`](controls/dialog.md) |
| `GridView` | [`ui::TileView`](controls/tile-view.md) |
| `DataGrid` | [`ui::TableView`](controls/table-view.md) |
| `VariableSizedWrapGrid` | [`ui::TilePanel`](layout/tile-panel.md) |
| `ItemsRepeater` | [`ui::ForEach`](controls/for-each.md) |
| `UniformGridLayout`, `FlowLayout` | `ui::TileLayout`, `ui::WrapLayout` ([item layouts](controls/item-layouts.md)) |
| `ScrollViewer`, `ScrollView` | [`ui::Scroll`](layout/scroll.md) |
| `SplitView` | [`ui::SidePane`](layout/side-pane.md) |
| `TwoPaneView`, `Viewbox`, `ParallaxView` | [`ui::TwoPane`](layout/two-pane.md), [`ui::ScaleToFit`](layout/scale-to-fit.md), [`ui::Parallax`](layout/parallax.md) |
| `NavigationView` | [`ui::NavigationShell`](controls/navigation-shell.md) |
| `Frame` | [`ui::NavigationFrame`](controls/navigation-frame.md) |
| `TabView`, `Pivot` | [`ui::Tabs`](controls/tabs.md), [`ui::Sections`](controls/sections.md) |
| `FlipView` | [`ui::Carousel`](controls/carousel.md) |
| `RatingControl`, `PagerControl`, `SwipeControl` | [`ui::Rating`](controls/rating.md), [`ui::Pager`](controls/pager.md), [`ui::Swipe`](layout/swipe.md) |
| `BreadcrumbBar` | [`ui::Breadcrumb`](controls/breadcrumb.md) |
| `ContextFlyout` | [`ui::ContextMenu`](controls/context-menu.md) |
| `FontIcon`, `SymbolIcon`, `BitmapIcon`, `PathIcon` | [`ui::Icon`](controls/icon.md) |
| `XamlUICommand`, `KeyboardAccelerator` | `ui::Command`, `ui::Shortcut` |
| `*NavigationTransitionInfo` | `ui::PageTransition` (`ui::ZoomPageTransition`, `ui::SlidePageTransition`, ...) |
| `EntranceThemeTransition` and the other theme transitions | `ui::EntranceTransition`, `ui::RepositionTransition`, ... |
| `x:Name` and a field in code-behind | a `ui::Control<...>` handle and the spec's `.control` |
| `{x:Bind ..., Mode=TwoWay}` | `ui::bind(prop)` on an editable field |
| `FileOpenPicker`, `FileSavePicker`, `FolderPicker` | `ui::pick_file()`, `ui::pick_save_file()`, `ui::pick_folder()` |
| `Window.Activate()`, `Application.Start()` | `window.show()`, `ui::run(window)` |

## Generated Pages

Everything in this section except three pages comes from the headers, through Doxygen and `docs/tools/gen_reference.py`. This page, [Conventions](conventions.md) and the [Themes](themes/index.md) overview are written by hand.

| Section | Holds |
| --- | --- |
| [Controls](controls/index.md) | One page per control and its spec, grouped by purpose: basic input, text, collections, date and time, navigation, menus and toolbars, dialogs and flyouts, status and info, media and icons, windowing and hosting. |
| [Layout](layout/index.md) | Panels, containers, scrolling and gestures, panes and splitters. |
| [Tree and window](tree/index.md) | `Element`, `Window`, focus, automation and the other types under `winui/ui/`. |
| [Core](core/index.md) | Geometry, color, input events, `Property`, `Binding`, the scheduler, UTF-8 helpers. |
| [Rendering](rendering/index.md) | The `Renderer` interface and its commands, `TestRenderer`, `describe()`, images, text styles, brushes and the seam interfaces. |
| [Testing](testing/index.md) | The opt-in testing tier: `HeadlessUI`, `TestRenderer`, the test doubles for the platform seams and `OffscreenCapture`. |
| [Themes](themes/index.md) | `Theme`, the palettes, `ThemeRequest` and the theme sources. |

Outside Controls and Layout, a section lists its topic pages first (small types bundled on one page), then Classes, Structs and Namespaces. A namespace page holds that namespace's free functions, enumerations, aliases and constants for the section.

If something is missing or wrong on a generated page, the fix goes into the header's `///` comment, not into the page. See [Documentation](../contributing/development/docs.md#the-reference-generator).
