// SPDX-License-Identifier: MIT
// The paragraph under each page's description: what else is worth knowing about the control, and
// what to try on the page.

#include "gallery.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace gallery
{

namespace
{
struct About
{
    std::string_view page;
    const char*      text;
};

// Sorted by page name, so a lookup can binary search.
constexpr std::array AboutTexts{
    About{"Acrylic",
          "The brush blurs what was drawn before it in the same frame and puts a tint over the blur, so it "
          "needs something behind it to show. Popups and flyouts use it under Fluent 2. Change the tint, "
          "opacity and luminosity on the custom card and watch the shapes behind it."},
    About{"AnimatedIcon",
          "The icon plays one segment of its animation for each change of state, like a hover or a press. The "
          "control that owns it, or your own code, sets the state. Hover and press the button, and select items "
          "in the navigation view."},
    About{"AnimatedVisualPlayer",
          "Its source is plain code that draws a frame for a given progress, and the player drives that "
          "progress over time. It can loop, pause, rewind and play backwards. Play, pause, stop and reverse it."},
    About{"Animation Interop",
          "Springs, expressions and size-driven offsets run next to the layout and move an element's "
          "translation, scale or opacity without touching its layout. Hover the springs and drag the sliders "
          "the expressions read."},
    About{"AnnotatedScrollBar",
          "Labels along the rail let the user jump through long, grouped content. Attached to a scroll view, it "
          "hides that view's own bar and scrolls it directly. Drag along the rail and lower the maximum height "
          "to see the labels collapse."},
    About{"AppBarButton",
          "It picks its shape from where it sits: full size on its own, compact in a closed command bar and a "
          "menu row in the overflow. Try the icon kinds and the shortcut, which fires from anywhere "
          "in the window."},
    About{"AppBarSeparator",
          "In a command bar's overflow menu the same separator turns into a horizontal line. Open the bar's "
          "overflow to see both."},
    About{"AppBarToggleButton",
          "Checked, it fills with the accent color in the bar and shows a check mark in the overflow menu. "
          "Click it a few times with three states turned on."},
    About{"AutoSuggestBox",
          "The app supplies the matches as the user types, the box itself doesn't filter anything. Type a "
          "control name, step through the list with the arrow keys and press Enter to go to that page."},
    About{"Backdrops",
          "They need Windows 11, and the era themes keep an opaque page. Switch the gallery window's own "
          "backdrop."},
    About{"Border",
          "It wraps exactly one child with a background, a border and padding. Input and focus pass straight "
          "through to the child. Change the border's thickness and color and the background."},
    About{"Breadcrumb",
          "A click on an item is only a request, the app decides what happens, usually cutting the trail back "
          "to that item. Narrow the window until the leading items fold into the ellipsis, then pick one from "
          "its list."},
    About{"Button",
          "Enter and Space press it too, and its content can be any element in place of the text. Its look "
          "comes from its style and the theme. Try the built-in styles and switch the theme in the header to "
          "see each era's button."},
    About{"CalendarDatePicker",
          "The field shows the date in the short date format of the user's region. Picking a day closes the "
          "calendar, and picking the same day again clears the date. Open it with a click, Enter, Space or "
          "Alt+Down."},
    About{"CalendarView",
          "A click selects a day and a second click clears it. The heading zooms out to the months and the "
          "years, and Ctrl+Up and Ctrl+Down do the same from the keyboard. Switch the theme to see how each "
          "Windows era drew its month calendar."},
    About{"Canvas",
          "Each child sits at an exact offset from the top-left corner, with a z-order. The canvas asks for no "
          "room and doesn't clip, so a child placed past its edge still draws. Move a child with Canvas.Left "
          "and Canvas.Top and change its z-index."},
    About{"Carousel",
          "Besides the buttons, the wheel and the arrow keys flip too. The buttons show up while the mouse "
          "moves over the view or when it has keyboard focus. Flip with the arrow keys, then try the vertical "
          "one."},
    About{"CheckBox",
          "The value is an optional bool, and no value at all is the third, indeterminate state. Use the "
          "select-all box to see that state follow its options."},
    About{"Clipboard",
          "Text, images and files go through the system clipboard, one for the whole process, which a test can "
          "swap for a fake one. Copy here, paste into another app, and the other way round."},
    About{"ColorPicker",
          "Hue, saturation, value and alpha each get a slider, next to channel and hex entry fields. Turn the "
          "parts on and off and type a hex value."},
    About{"ComboBox",
          "The list floats above the page, so opening it never moves the layout. Type the first letters of an "
          "item to jump to it, or change the selection with the arrow keys while the list is closed. The "
          "editable box completes what you type, and text that matches no item is reported as submitted."},
    About{"Command",
          "Every control pointed at the command fills in what it wasn't given and follows the command as it "
          "changes. Fire it, then turn the command off and watch the button gray out."},
    About{"CommandBar",
          "Primary commands that don't fit move into the overflow behind the More button and come back once "
          "there's room. Narrow the window, open the overflow, and add or remove secondary commands."},
    About{"CommandBarFlyout",
          "Opened by a click it starts with only the row of icons, as a context menu it starts with the menu "
          "open as well. Click the image, then right-click it."},
    About{"Connected Animation",
          "The source page prepares the animation for an element and the destination page starts it on its own "
          "copy, so the element seems to fly across. Navigate and go back with each configuration."},
    About{"DatePicker",
          "The parts follow the order the user's region writes a date in. Turn a column with the wheel, the "
          "arrow keys or a click, and keep the choice with Enter or the check mark. Switch the theme to see "
          "Windows 8.1's drop-down lists and the classic eras' date picker."},
    About{"DensityScope",
          "Text fields, combo boxes and drop-downs inside the compact scope get smaller, everything else keeps "
          "its size. Compare the two columns."},
    About{"Dialog",
          "The window shows it on top of everything, sends all input to it and gives focus back when it closes. "
          "It has a title, content and up to three buttons, and a coroutine can wait for the answer. Open each one "
          "and try Enter and Escape."},
    About{"Drag and Drop",
          "Files dragged in from another app reach the same drop events as a drag inside the app, at the "
          "point where they are released. Drag a few files from the file manager onto the box."},
    About{"DropdownButton",
          "The chevron is all it adds, the menu is whichever flyout uses it as its anchor. Open both menus "
          "with the mouse and with the keyboard."},
    About{"Easing Functions",
          "Each card plays the same move with a different curve and mode. Pick an easing and a mode, and press "
          "Animate."},
    About{"EditBox",
          "It paints itself, so it follows the window's light or dark mode but none of the other theme colors. "
          "Try the class's own styles: read only, numbers only, casing and alignment."},
    About{"Element Transitions",
          "Set once on the element or its container, they play when an element enters, moves or leaves. Add and "
          "remove items and watch them come and go."},
    About{"Expander",
          "While collapsed, the content isn't in the tree at all, so it costs nothing and takes no Tab stop. "
          "Toggle it with a click or Space and change the direction."},
    About{"Flyout",
          "It sits next to the element that opened it and closes on a click outside or Escape. Open it and Tab "
          "through its content."},
    About{"ForEach",
          "An edit to the collection builds or destroys only the rows it touches. With a key, rows follow their "
          "items even when the whole vector gets replaced. Add, remove and shuffle, and watch what each row "
          "keeps."},
    About{"Grid",
          "Rows and columns can be fixed, sized to their content or share what's left by weight. Build a Grid "
          "that sits inside another one into a named spec first, MSVC compiles it a lot faster that way. Move "
          "the red block between cells and change the spacing."},
    About{"GroupBox",
          "It groups only what you see. It takes no focus, so Tab goes straight to the controls inside, and radio "
          "buttons still need a RadioButtons or a RadioGroup to be exclusive. Edit the header, then switch the "
          "theme to see each era's frame."},
    About{"Icon",
          "The glyph can come from an icon font, a bitmap, an SVG or a path. Icons take no input, put one in a "
          "button's content for that. Switch the theme to see the glyph font each era uses."},
    About{"Image",
          "Pictures, animated GIFs and SVG documents are stretched into the slot, and a nine-grid keeps the "
          "corners unscaled while the middle stretches. Try the stretch modes and the GIF playback."},
    About{"Implicit Transitions",
          "Position, size, opacity and scale animate on their own when the property changes, the app only sets "
          "the new value. Change the values and watch the element move."},
    About{"InfoBadge",
          "It shows a number, an icon or just a dot, drawn over another control, and takes no input. Change the "
          "value and the style."},
    About{"InfoBar",
          "The severity picks the color and the icon, and an optional action button sits next to the message. A "
          "closed bar takes no room, and closing can be canceled. Change the severity and the message length, "
          "and close it."},
    About{"InkCanvas",
          "Strokes follow the pen's pressure, the mouse draws as well, and the ink toolbar picks pens and "
          "erasers. Strokes can be saved and loaded again. Draw, erase, and save the strokes."},
    About{"Item Layouts",
          "Only the items in view are built. ForEach brings no chrome or selection of its own, so put it in a "
          "scroll view to scroll. Switch layouts, filter and sort the large collection."},
    About{"LayoutPanel",
          "Measuring and arranging are handed to the layout object, which suits a fixed set of children with a "
          "layout none of the built-in panels offers. Switch between the two layouts and change the spacing."},
    About{"Line",
          "The page has polylines and paths as well. A line has no inside, so only its stroke draws. Move the "
          "line's ends, change its thickness, and show the points of the polyline and the path."},
    About{"ListView",
          "Rows are built from a template, and only while they're in view. Selection, groups, drag to reorder "
          "and scrolling come with it. Select with the mouse and the keyboard, drag rows to reorder them and "
          "right-click a row."},
    About{"Match",
          "Show or hide part of a page, build it the first time it's needed, or switch between whole subtrees. "
          "Nothing gets diffed, each node rebuilds only itself. Type into a field, hide it and show it again, "
          "and switch pages with and without keep."},
    About{"MenuBar",
          "Every item's shortcut works from anywhere in the window, whether the menu is open or not. Open it "
          "with the mouse, then move along it with the arrow keys."},
    About{"MenuFlyout",
          "It cascades into submenus and closes on a pick, a click outside or Escape. Try the toggle, radio and "
          "split items and the shortcuts."},
    About{"Message Box",
          "The operating system draws it in its own style, whatever theme the app uses, and the call waits "
          "for the answer. Open both boxes and answer with the keyboard as well."},
    About{"Native",
          "This is the way in for system controls libwinui doesn't draw itself. The hosted window is clipped "
          "and hidden along with the tree around it and answers its own notifications. Scroll the edit control "
          "out of view, and select the first tab to hide the one in the second."},
    About{"NavigationShell",
          "The pane sits on the left, compact, minimal or as a top bar, and in Auto mode it adapts to the "
          "window's width. Resize the window and open the hierarchy."},
    About{"Notifications",
          "The app needs no tray icon. The first notification registers the program under its own name and "
          "icon, and do-not-disturb holds them back. Send one, then click it."},
    About{"NumberBox",
          "The value is checked and formatted when committed, and the box can evaluate arithmetic such as "
          "2*(3+4). Type an expression and press Enter, then step with the arrow keys and the wheel."},
    About{"Page Transitions",
          "Each kind of transition tells the user how the two pages relate. Navigate with each one."},
    About{"Pager",
          "The page selector can be a number box, a drop-down or a row of buttons, next to first, previous, "
          "next and last. Switch the display mode and try a count that isn't known."},
    About{"Parallax",
          "Its child moves at a different rate than the list or scroll view it follows. It wires itself to that "
          "source once the tree is built and never takes focus. Scroll the list and watch the background."},
    About{"PasswordBox",
          "A password can only be peeked at when it was typed since the field was last empty. Type a password, "
          "check Show password, and try typing Password itself."},
    About{"PersonPicture",
          "It shows a photo, the initials or a placeholder, with an optional badge, and takes no input. Switch "
          "between the image, the name and the initials."},
    About{"PipsPager", "The selected page's dot is drawn larger. Step with the arrow keys and turn on wrapping."},
    About{"Popup",
          "Its child floats above the page at an offset from where the popup sits. The popup itself draws "
          "nothing and takes no room in the layout. Open it and change the offsets."},
    About{"Printing",
          "The app says how many pages there are and draws each one when the preview or the printer asks for "
          "it. The dialog picks the printer, the pages and the paper. Print to Microsoft Print to PDF to try it "
          "without paper."},
    About{"ProgressBar",
          "Without a known length it shows an endless sweep, and the paused and error states tint it. Switch "
          "the sweep between running, paused and error, and set the other bar's value."},
    About{"ProgressRing",
          "With a value it draws an arc for how far the task has come. It takes no input and hides while "
          "inactive. Turn the spinning one off and on, and set the other ring's progress."},
    About{"PullToRefresh",
          "On the desktop the pull is a mouse drag, made while the content sits at the edge the pull comes "
          "from. Drag the list down past its top, then try the other directions."},
    About{"RadialGradientBrush",
          "Center, origin and radii are either fractions of the shape or absolute positions. Move the center "
          "and change the spread method."},
    About{"RadioButton",
          "A group keeps its buttons exclusive, and a lone button can be turned on but never off by the user. "
          "Move through a group with the arrow keys."},
    About{"Rating",
          "A click, a drag or the keyboard sets it. Until the user rates, a placeholder value such as the "
          "average of other ratings shows. Click a star, drag across them, and click the same star again to "
          "clear."},
    About{"RelativePanel",
          "Children are placed relative to each other and to the panel's edges, resolved in dependency order "
          "whatever order they were added in. Open the source to see which constraint places each rectangle."},
    About{"ResizeGripper",
          "It reports both axes while dragged and resizes nothing itself, the app applies each change. Drag the "
          "grip in the corner."},
    About{"RichEdit",
          "It paints itself and only follows the window's light or dark mode, and RTF goes in and out. Format "
          "text, search with Find, and right-click for the Share item."},
    About{"RichEditBox",
          "libwinui draws this editor itself, without a child window. Ctrl+B, Ctrl+I, Ctrl+U and the context "
          "menu format text, and RTF goes in and out. Format some text, undo it, search with Find, and right- "
          "click for the Share item."},
    About{"RichText",
          "Text can flow on into overflow containers, and selection is on by default. Select text, copy it, and "
          "narrow the window to watch the overflow."},
    About{"ScaleToFit",
          "The child is laid out at its natural size and then scaled, so text scales like a picture and input "
          "still lands in the right place. Resize it and change the stretch."},
    About{"Scroll",
          "The scroll bars sit over the content and fade when idle, under Aero they get a gutter of their own "
          "and stay. Turn zooming on and use Ctrl+wheel, and run the animated scrolls."},
    About{"Sections",
          "Headers that don't fit wrap round as a carousel. Click the headers, or move between them with Left "
          "and Right."},
    About{"SelectorBar",
          "A pill under the text marks the selected segment. The segments own no content, the app swaps what "
          "shows below. Switch segments with the mouse and the arrow keys."},
    About{"Shadow",
          "The higher an element is raised, the deeper its shadow. Each era casts its own kind of shadow, or "
          "none at all. Change the elevation and switch the theme."},
    About{"Shape",
          "Each shape is one geometry with a fill and a stroke, and takes no input. Change the size, the stroke "
          "thickness and the rectangle's corner radii."},
    About{"SidePane",
          "The pane opens, closes or collapses to a strip. In the overlay modes a click on the content or "
          "Escape closes it. Switch the display mode and the placement."},
    About{"Single Instance",
          "The first copy to claim a name keeps it until it exits, and every later start forwards its command "
          "line and quits. A jump list task starts the program again with its own arguments, which reach the "
          "running copy the same way. Claim the name, then start the gallery a second time or pick a task "
          "from its taskbar button."},
    About{"Slider",
          "A click on the track jumps there, the arrow keys step, and Home and End jump to the ends. Try the "
          "tick marks and the vertical slider."},
    About{"SplitButton",
          "The main part repeats the current action, the chevron offers the choices. Pick a color, then click "
          "the main part to apply it again."},
    About{"Splitter",
          "In a grid it resizes the two tracks beside it and stops at their limits. Drag the dividers, or Tab "
          "to one and use the arrow keys."},
    About{"Stack",
          "Each child gets unlimited room along the stack and is placed across it by its own alignment. Change "
          "the orientation and the spacing."},
    About{"StandardCommand",
          "Label, icon and shortcut come filled in for Cut, Copy, Delete and the like. Every control pointed at "
          "it takes those over and is disabled while the command is. Delete items from the list with their "
          "Delete buttons."},
    About{"Storage Pickers",
          "The results are paths, and file names outside the system code page come through intact. Pick files, "
          "a folder, and save a file."},
    About{"Swipe",
          "Dragging right reveals the left items, dragging left the right ones, and the row's own content stays "
          "interactive. Swipe the rows in both directions."},
    About{"TableView",
          "Columns sort, resize and freeze, cells can be edited and rows grouped. Only the rows in view are "
          "built, so a large table costs about as much as a screenful. Sort by a column, resize one, and edit a "
          "cell."},
    About{"Tabs",
          "Closing a tab is a request, the app answers it by removing the tab. Add, close and drag tabs, and "
          "use Ctrl+T, Ctrl+W and Ctrl+1 to Ctrl+8."},
    About{"TeachingTip",
          "It points at a control or sits against the window, floats above the page and can light-dismiss. Open "
          "each tip and try the buttons on the one with buttons."},
    About{"Text",
          "Wrapping, trimming, alignment, formatted runs and hyperlinks are all there. It takes no input unless "
          "selection is turned on. Narrow the window to see wrapping and trimming, and select the selectable "
          "one."},
    About{"TextBox",
          "Spell checking and a placeholder come with it. Edits by the user raise a change, text set by the app "
          "doesn't. Type a misspelled word and right-click it."},
    About{"TilePanel",
          "Cells are all the same size, and smaller children fill the gaps larger ones leave. Switch the "
          "orientation."},
    About{"TileView",
          "Rows wrap with the width, only the items in view are built, and every cell takes the first item's "
          "size unless you set one. The layout can be swapped for a stack or a lined flow at any time. Resize "
          "the window, select in each mode, swap the layouts and invoke an item with Enter or a double click."},
    About{"TimePicker",
          "The clock follows the user's region unless the picker sets its own. With a minute increment the "
          "minutes step by it, and a time in between rounds down."},
    About{"TitleBar",
          "As the window's caption it tells the host which parts drag the window, while the system keeps its "
          "own caption buttons. Next to the icon and title it has back and pane buttons and three content "
          "slots. Drag the sample window by its title bar and by the content in it."},
    About{"ToggleButton",
          "Checked, it fills with the accent color, and eras without an accent draw it sunken. Click it, then "
          "disable it."},
    About{"ToggleSplitButton",
          "The main part turns something on and off, the chevron still opens the choices. Toggle the bullets, "
          "then pick Roman numerals."},
    About{"ToggleSwitch",
          "It reserves room for the wider of its two labels, so switching never moves its row. Click it and "
          "press Space."},
    About{"ToolTip",
          "It shows after the pointer rests on the element. Any element can take a plain-text tip without a "
          "wrapper. Hover the samples and try the offsets."},
    About{"Toolbar", "It has no overflow, a toolbar wider than its slot just runs past it. Tab along its buttons."},
    About{"TreeView",
          "Single or multiple selection, and rows can be dragged onto another row or between rows. Expand nodes "
          "with the arrow keys and drag a row into another folder."},
    About{"TwoPane",
          "With enough width the panes sit side by side, with enough height they stack, and otherwise only one "
          "shows. The mode is picked on every layout. Resize the window across the thresholds."},
    About{"Window",
          "Each card opens a real window of its own, centered, sized within limits, modal, full screen or "
          "always on top in a compact overlay. Try the presenters and watch how the window's frame changes."},
    About{"Window States",
          "The label shows the state the window is actually in, which isn't always the one asked for. Ask for "
          "each state."},
    About{"Window Title Bar",
          "You can change the title bar's colors, height and theme, or draw your own content into the caption "
          "area, while the minimize, maximize and close buttons stay the system's. Open each sample window and "
          "drag it by the extended area."},
    About{"Windowing",
          "The app owns each window, and a closed one stays a valid object. Open a window and resize it against "
          "its limits."},
    About{"WrapPanel",
          "It suits tag chips and cards that reflow with the window. Change the spacing and stretch the last "
          "item."},
};
} // namespace

const char* page_about(std::string_view page)
{
    static_assert(std::ranges::is_sorted(AboutTexts, {}, &About::page));
    const auto it = std::ranges::lower_bound(AboutTexts, page, {}, &About::page);
    return it != AboutTexts.end() && it->page == page ? it->text : nullptr;
}

} // namespace gallery
