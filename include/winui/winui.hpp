// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// The umbrella header, and the one include an app needs: core value types and `Property<T>`,
/// the render abstraction, every spec and control, `Window` and the app loop, all in namespace
/// `ui`.
///
/// It names nothing of the operating system, so it compiles on any platform and an app needs
/// neither `<windows.h>` nor a platform guard. The usual shape of an app: create a `Window`,
/// describe its content as a tree of specs whose displays are derived from `Property<T>` state
/// with `bind()`, and hand the window to `ui::run()`, which borrows it until it is closed.
///
/// Example, linked against `winui::main`:
/// @code
/// #include <winui/winui.hpp>
///
/// #include <format>
///
/// int main()
/// {
///     ui::Window window{{.title = "My first window", .size = {480, 320}}};
///     ui::Property<int> clicks{0};
///
///     window.set_content(ui::VerticalStack{
///         .spacing  = 12.0f,
///         .content  = {
///             ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
///             ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
///             ui::Button{.text = "Reset", .on_click = [&] { clicks = 0; }},
///         },
///     });
///
///     return ui::run(window);
/// }
/// @endcode

// core: foundational value types
#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/CommandLine.hpp"
#include "winui/core/DateFormat.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/NativeHandle.hpp"
#include "winui/core/NumberFormat.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/ThreadCheck.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/core/Utf8Path.hpp"
#include "winui/core/Version.hpp"
#include "winui/core/VisualId.hpp"

// render: the render abstraction
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/JumpList.hpp"
#include "winui/ui/Launcher.hpp"
#include "winui/ui/MessageBox.hpp"
#include "winui/ui/Notification.hpp"
#include "winui/ui/Printing.hpp"

// ui: elements, controls, containers and theming
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/App.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/ConnectedAnimation.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Coroutine.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/LayoutContext.hpp"
#include "winui/ui/ParallelLayout.hpp"
#include "winui/ui/Pickers.hpp"
#include "winui/ui/SingleInstance.hpp"
#include "winui/ui/TrayIcon.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowOptions.hpp"
#include "winui/ui/WindowRef.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/collections/ForEach.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/collections/TileView.hpp"
#include "winui/ui/collections/TreeView.hpp"
#include "winui/ui/collections/VirtualListControl.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/containers/DensityScope.hpp"
#include "winui/ui/containers/Expander.hpp"
#include "winui/ui/containers/GroupBox.hpp"
#include "winui/ui/containers/ScaleToFit.hpp"
#include "winui/ui/containers/Shortcuts.hpp"
#include "winui/ui/containers/Toolbar.hpp"
#include "winui/ui/datetime/CalendarDatePicker.hpp"
#include "winui/ui/datetime/CalendarView.hpp"
#include "winui/ui/datetime/DatePicker.hpp"
#include "winui/ui/datetime/TimePicker.hpp"
#include "winui/ui/dialogs/Dialog.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/dialogs/Popup.hpp"
#include "winui/ui/dialogs/TeachingTip.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/hosting/DragArea.hpp"
#include "winui/ui/hosting/NativeControl.hpp"
#include "winui/ui/hosting/TitleBar.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ColorPicker.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/DropdownButton.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/RadioButtons.hpp"
#include "winui/ui/input/RadioGroup.hpp"
#include "winui/ui/input/Rating.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/input/SplitButton.hpp"
#include "winui/ui/input/ToggleButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/LinedFlowLayout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/layouts/TileLayout.hpp"
#include "winui/ui/layouts/WrapLayout.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/media/AnimatedVisualPlayer.hpp"
#include "winui/ui/media/AnimatedVisuals.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/media/Image.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/media/InkToolbar.hpp"
#include "winui/ui/media/PersonPicture.hpp"
#include "winui/ui/media/Shape.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/menus/CommandBar.hpp"
#include "winui/ui/menus/CommandBarFlyout.hpp"
#include "winui/ui/menus/ContextMenu.hpp"
#include "winui/ui/menus/MenuBar.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/menus/MenuItem.hpp"
#include "winui/ui/navigation/Breadcrumb.hpp"
#include "winui/ui/navigation/Carousel.hpp"
#include "winui/ui/navigation/NavigationFrame.hpp"
#include "winui/ui/navigation/NavigationShell.hpp"
#include "winui/ui/navigation/Pager.hpp"
#include "winui/ui/navigation/PipsPager.hpp"
#include "winui/ui/navigation/Sections.hpp"
#include "winui/ui/navigation/SelectorBar.hpp"
#include "winui/ui/navigation/Tabs.hpp"
#include "winui/ui/panels/Canvas.hpp"
#include "winui/ui/panels/Fill.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/LayoutPanel.hpp"
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/panels/RelativePanel.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/panels/TilePanel.hpp"
#include "winui/ui/panels/WrapPanel.hpp"
#include "winui/ui/panes/ResizeGripper.hpp"
#include "winui/ui/panes/SidePane.hpp"
#include "winui/ui/panes/Splitter.hpp"
#include "winui/ui/panes/TwoPane.hpp"
#include "winui/ui/scrolling/AnnotatedScrollBar.hpp"
#include "winui/ui/scrolling/Parallax.hpp"
#include "winui/ui/scrolling/PullToRefresh.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
#include "winui/ui/scrolling/Swipe.hpp"
#include "winui/ui/status/InfoBadge.hpp"
#include "winui/ui/status/InfoBar.hpp"
#include "winui/ui/status/ProgressBar.hpp"
#include "winui/ui/status/ProgressRing.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"
#include "winui/ui/text/Inlines.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/text/PasswordBox.hpp"
#include "winui/ui/text/RichEditBox.hpp"
#include "winui/ui/text/RichText.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/EarlyPalette.hpp"
#include "winui/ui/themes/Mdl2Palette.hpp"
#include "winui/ui/themes/MetroPalette.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

// ui/spec: what every spec shares. Each spec sits in its control's header above, after the
// control it builds.
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Dynamic.hpp"
#include "winui/ui/spec/ItemsModel.hpp"
#include "winui/ui/spec/LayoutSpec.hpp"
#include "winui/ui/spec/Spec.hpp"
