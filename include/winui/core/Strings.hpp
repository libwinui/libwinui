// SPDX-License-Identifier: MIT
#pragma once

// The text libwinui itself shows: command labels, context menu items, key names, the names
// screen readers read for built-in buttons. One replaceable set, English by default.

#include "winui/core/Property.hpp"

#include <functional>
#include <string>

#include <cstddef>

namespace ui
{

/// The text libwinui itself shows or gives screen readers, English by default. An app that
/// shows another language replaces it with set_strings(), and every control follows at once.
///
/// A field holding `{0}` or `{1}` is a pattern: the control puts a value in each slot, so a
/// translation can place them where its language wants them. `{{` and `}}` write a brace. A
/// field whose wording depends on a number is a function of that number, so a translation
/// can apply its own plural rules.
///
/// A control's own text wins over these: a ToggleSwitch with `.checked_text` shows that, and
/// a Pager with `.prefix_text` that. Field names stay as they are; new fields are appended.
///
/// Example:
/// @code
/// ui::Strings german = ui::strings(); // starts from the current set: what is not set stays
/// german.commands.copy        = {"Kopieren", "Die Auswahl in die Zwischenablage kopieren"};
/// german.commands.select_all  = {"Alles auswählen", "Den ganzen Inhalt auswählen"};
/// german.keys.ctrl            = "Strg";
/// german.toggle_switch.on     = "Ein";
/// german.toggle_switch.off    = "Aus";
/// german.pager.page_number    = "Seite {0}";
/// german.person_picture.badge = [](std::size_t n) { return n == 1 ? "1 Element" : std::format("{} Elemente", n); };
/// ui::set_strings(std::move(german));
/// @endcode
struct Strings
{
    /// A standard command's text and the description shown as its tooltip.
    struct CommandText
    {
        /// The label on a button or menu item.
        std::string text;
        /// The tooltip, a sentence saying what the command does.
        std::string description;
    };

    /// The sixteen standard commands (standard_command()). The text menus of the text controls
    /// use the same labels for Cut, Copy, Paste, Select All, Undo and Redo.
    struct StandardCommandsText
    {
        CommandText cut{"Cut", "Remove the selected content and put it on the clipboard"};          ///< Cut.
        CommandText copy{"Copy", "Copy the selected content to the clipboard"};                     ///< Copy.
        CommandText paste{"Paste", "Insert the contents of the clipboard at the current location"}; ///< Paste.
        CommandText select_all{"Select All", "Select all content"};                                 ///< Select All.
        CommandText delete_selection{"Delete", "Delete the selected content"};                      ///< Delete.
        CommandText share{"Share", "Share the selected content"};                                   ///< Share.
        CommandText save{"Save", "Save your changes"};                                              ///< Save.
        CommandText open{"Open", "Open"};                                                           ///< Open.
        CommandText close{"Close", "Close"};                                                        ///< Close.
        CommandText pause{"Pause", "Pause"};                                                        ///< Pause.
        CommandText play{"Play", "Play"};                                                           ///< Play.
        CommandText stop{"Stop", "Stop"};                                                           ///< Stop.
        CommandText forward{"Forward", "Go to the next item"};                                      ///< Forward.
        CommandText backward{"Backward", "Go to the previous item"};                                ///< Backward.
        CommandText undo{"Undo", "Reverse the most recent action"};                                 ///< Undo.
        CommandText redo{"Redo", "Repeat the most recently undone action"};                         ///< Redo.
    };

    /// The key names in shortcut text ("Ctrl+S"). Letters, digits and F1 to F12 are written as
    /// they are.
    struct KeysText
    {
        std::string ctrl      = "Ctrl";     ///< The Ctrl modifier.
        std::string alt       = "Alt";      ///< The Alt modifier.
        std::string shift     = "Shift";    ///< The Shift modifier.
        std::string win       = "Win";      ///< The Windows key modifier.
        std::string enter     = "Enter";    ///< Enter.
        std::string space     = "Space";    ///< The space bar.
        std::string tab       = "Tab";      ///< Tab.
        std::string escape    = "Esc";      ///< Escape.
        std::string up        = "Up";       ///< The up arrow.
        std::string down      = "Down";     ///< The down arrow.
        std::string left      = "Left";     ///< The left arrow.
        std::string right     = "Right";    ///< The right arrow.
        std::string backspace = "Back";     ///< Backspace.
        std::string del       = "Delete";   ///< Delete.
        std::string insert    = "Insert";   ///< Insert.
        std::string home      = "Home";     ///< Home.
        std::string end       = "End";      ///< End.
        std::string page_up   = "PageUp";   ///< Page Up.
        std::string page_down = "PageDown"; ///< Page Down.
    };

    /// Wording several controls share.
    struct CommonText
    {
        /// The name of a close button (InfoBar, TeachingTip).
        std::string close = "Close";
        /// A tooltip with its shortcut: {0} the text, {1} the shortcut ("Save (Ctrl+S)").
        std::string with_shortcut = "{0} ({1})";
    };

    /// The names of the caption buttons a themed window frame draws.
    struct WindowFrameText
    {
        std::string minimize = "Minimize"; ///< The minimize button.
        std::string maximize = "Maximize"; ///< The maximize button while the window is restored.
        std::string restore  = "Restore";  ///< The maximize button while the window is maximized.
        std::string close    = "Close";    ///< The close button.
    };

    /// The text controls' context menus, besides the standard command labels.
    struct TextMenuText
    {
        std::string delete_repeated_word    = "Delete repeated word";      ///< Over a word written twice.
        std::string no_spelling_suggestions = "(No spelling suggestions)"; ///< Over a misspelling with none.
        std::string add_to_dictionary       = "Add to dictionary";         ///< Over a misspelling.
        std::string ignore                  = "Ignore";                    ///< Over a misspelling.
        std::string bold                    = "Bold";                      ///< RichEditBox only.
        std::string italic                  = "Italic";                    ///< RichEditBox only.
        std::string underline               = "Underline";                 ///< RichEditBox only.
    };

    /// ColorPicker.
    struct ColorPickerText
    {
        std::string red        = "Red";        ///< The red channel's label.
        std::string green      = "Green";      ///< The green channel's label.
        std::string blue       = "Blue";       ///< The blue channel's label.
        std::string hue        = "Hue";        ///< The hue channel's label.
        std::string saturation = "Saturation"; ///< The saturation channel's label.
        std::string value      = "Value";      ///< The value channel's label.
        std::string opacity    = "Opacity";    ///< The opacity box's label.
        std::string rgb        = "RGB";        ///< The RGB choice in the representation box.
        std::string hsv        = "HSV";        ///< The HSV choice in the representation box.
        std::string more       = "More";       ///< The button that shows the channel boxes.
        std::string less       = "Less";       ///< The same button while they show.
        /// The opacity box's text: {0} the opacity, 0 to 100.
        std::string percent = "{0}%";
    };

    /// ToggleSwitch, where it has no text of its own.
    struct ToggleSwitchText
    {
        std::string on  = "On";  ///< The label while on.
        std::string off = "Off"; ///< The label while off.
    };

    /// Pager.
    struct PagerText
    {
        std::string first_page    = "First page";    ///< The First button's name and tooltip.
        std::string previous_page = "Previous page"; ///< The Previous button's name and tooltip.
        std::string next_page     = "Next page";     ///< The Next button's name and tooltip.
        std::string last_page     = "Last page";     ///< The Last button's name and tooltip.
        /// The text before the page box, where the Pager has none of its own.
        std::string prefix = "Page";
        /// The text between the page box and the page count, where the Pager has none of its own.
        std::string suffix = "of";
        /// The page box's name.
        std::string page = "Page";
        /// A page button's name: {0} the page number.
        std::string page_number = "Page {0}";
    };

    /// Tabs.
    struct TabsText
    {
        std::string close_tab       = "Close tab";                ///< A tab's close button tooltip, with its shortcut.
        std::string add_tab_name    = "Add New Tab";              ///< The add button's name.
        std::string add_tab         = "Add new tab";              ///< The add button's tooltip.
        std::string scroll_backward = "Scroll tab list backward"; ///< The backward scroll button's tooltip.
        std::string scroll_forward  = "Scroll tab list forward";  ///< The forward scroll button's tooltip.
    };

    /// NavigationShell.
    struct NavigationShellText
    {
        std::string settings         = "Settings";         ///< The settings item.
        std::string open_navigation  = "Open Navigation";  ///< The pane button while the pane is closed.
        std::string close_navigation = "Close Navigation"; ///< The pane button while the pane is open.
        std::string back             = "Back";             ///< The back button.
        std::string close            = "Close";            ///< The close button of an overlaid pane.
        std::string search           = "Click to search";  ///< The search button of a compact pane.
        std::string more             = "More";             ///< The overflow button of a top pane.
    };

    /// The More button of CommandBar and CommandBarFlyout.
    struct CommandBarText
    {
        std::string see_more     = "See more";     ///< The tooltip while closed.
        std::string see_less     = "See less";     ///< The tooltip while open.
        std::string more_options = "More options"; ///< The name while closed.
        std::string less_options = "Less app bar"; ///< The name while open.
    };

    /// InkToolbar.
    struct InkToolbarText
    {
        std::string ballpoint_pen  = "Ballpoint pen";  ///< The ballpoint pen tool.
        std::string pencil         = "Pencil";         ///< The pencil tool.
        std::string highlighter    = "Highlighter";    ///< The highlighter tool.
        std::string custom_pen     = "Custom pen";     ///< A custom pen with no name of its own.
        std::string custom_tool    = "Custom tool";    ///< A custom tool with no name of its own.
        std::string eraser         = "Eraser";         ///< The eraser tool.
        std::string colors         = "Colors";         ///< A pen flyout's color heading and palette.
        std::string size           = "Size";           ///< A pen flyout's size heading.
        std::string pen_size       = "Pen Size";       ///< A pen flyout's size slider.
        std::string stroke_preview = "Stroke preview"; ///< A pen flyout's preview stroke.
        std::string stroke_eraser  = "Stroke eraser";  ///< The eraser flyout's stroke choice.
        std::string point_eraser   = "Point eraser";   ///< The eraser flyout's point choice.
        std::string erase_all_ink  = "Erase all ink";  ///< The eraser flyout's erase-all item.
        std::string ruler          = "Ruler";          ///< The ruler stencil.
        std::string protractor     = "Protractor";     ///< The protractor stencil.
        /// A selected tool's name: {0} the tool.
        std::string selected = "{0}, selected";
    };

    /// PersonPicture's name for screen readers.
    struct PersonPictureText
    {
        std::string group  = "Group";  ///< A group picture.
        std::string person = "Person"; ///< A person with no name or initials.
        std::string icon   = "icon";   ///< A badge icon with no text.
        /// A badge number with no text of its own: "1 item", "3 items".
        std::function<std::string(std::size_t)> badge = [](std::size_t count)
        { return std::to_string(count) + (count == 1 ? " item" : " items"); };
        /// The picture's name with its badge: {0} the name, {1} the badge.
        std::string with_badge = "{0}, {1}";
    };

    /// CalendarView, CalendarDatePicker, DatePicker and TimePicker.
    struct DateTimeText
    {
        std::string previous = "Previous"; ///< The calendar's button to the previous month, year or decade.
        std::string next     = "Next";     ///< The calendar's button to the next month, year or decade.
        /// A calendar day's name when it is today: {0} the day's own name.
        std::string today = "{0}, today";
        /// What a CalendarDatePicker shows while no date is picked.
        std::string pick_a_date = "Pick a date";
        /// The line under a classic month calendar: {0} today's date.
        std::string today_footer = "Today: {0}";
        /// A DatePicker's and TimePicker's parts: what the field shows in each while there is no
        /// value, and what automation calls each column.
        std::string day     = "day";
        std::string month   = "month";   ///< See `day`.
        std::string year    = "year";    ///< See `day`.
        std::string hour    = "hour";    ///< See `day`.
        std::string minute  = "minute";  ///< See `day`.
        std::string period  = "period";  ///< What automation calls the AM/PM column.
        std::string accept  = "Accept";  ///< The picker flyout's button that keeps the choice.
        std::string dismiss = "Dismiss"; ///< The picker flyout's button that drops it.
    };

    /// The file dialogs.
    struct FilePickerText
    {
        /// The file type entry that lists every type.
        std::string all_files = "All Files";
    };

    StandardCommandsText commands{};         ///< The standard commands.
    KeysText             keys{};             ///< The key names in shortcut text.
    CommonText           common{};           ///< Wording several controls share.
    WindowFrameText      window_frame{};     ///< The caption buttons.
    TextMenuText         text_menu{};        ///< The text controls' context menus.
    ColorPickerText      color_picker{};     ///< ColorPicker.
    ToggleSwitchText     toggle_switch{};    ///< ToggleSwitch.
    PagerText            pager{};            ///< Pager.
    TabsText             tabs{};             ///< Tabs.
    NavigationShellText  navigation_shell{}; ///< NavigationShell.
    CommandBarText       command_bar{};      ///< CommandBar and CommandBarFlyout.
    InkToolbarText       ink_toolbar{};      ///< InkToolbar.
    PersonPictureText    person_picture{};   ///< PersonPicture.
    FilePickerText       file_picker{};      ///< The file dialogs.
    DateTimeText         date_time{};        ///< The date and time controls.
};

/// The text libwinui shows now: English until set_strings() replaces it. Process-wide; UI
/// thread only. The reference stays valid until the next set_strings().
[[nodiscard]] const Strings& strings();
/// Replaces the text libwinui shows and raises strings_changed(): every window lays out and
/// draws again, and the controls that hold text of their own refresh it.
void set_strings(Strings replacement);
/// Raised by set_strings(). An app that caches text from strings() subscribes here and keeps the
/// Subscription.
[[nodiscard]] const Signal& strings_changed();

} // namespace ui
