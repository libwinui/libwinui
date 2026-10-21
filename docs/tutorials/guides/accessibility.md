# Accessibility and UI Automation

Every libwinui window describes itself through UI Automation, the Windows interface that screen readers such as Narrator and test tools use. This guide shows what you get without any work, what you have to add, how to check it in tests and how to drive an app from a UI Automation client.

## What Comes Built In

Each control tells UI Automation three things:

- its **role**: Button, CheckBox, Edit, Slider, TreeItem and so on
- its **name**: the text a screen reader reads out
- what a client **can do** with it, through patterns: Invoke a button, Toggle a check box, set an edit's Value, Expand a menu, Select a list item

The name comes from the control's own text. A `ui::Button{.text = "Save"}` is a button named "Save". A `ui::TextBox`, `ui::Slider`, `ui::ComboBox` or `ui::Rating` takes the name from its `.header`. Items a control draws itself, such as tree rows, tabs, menu items, list items, suggestions and the links in a `ui::Text`, each appear as an element of their own.

The window also reports changes as they happen: which element has keyboard focus, a check box that was toggled, a value that changed, a menu that opened or closed, items that were added or removed. Screen readers announce them, and you write nothing for it.

Keyboard access comes with the controls too. Tab moves focus through the window, Space and Enter activate, arrow keys move inside lists, trees and menus, and Alt or F10 moves to a `ui::MenuBar`.

## Name What Has No Text

A control without visible text has no name, and a screen reader can only say "button". Give it one through `.accessibility`:

```cpp
window.set_content(ui::VerticalStack{
    .content = {
        ui::TextBox{.header = "Name"},
        ui::Slider{.accessibility = {.name = "Volume"}},
        ui::HorizontalStack{
            .accessibility = {.decorative = true},
            .content = {
                ui::Button{.text = "Save"},
                ui::Button{.accessibility = {.name = "Delete"}, .content = ui::Icon{.source = ui::Symbol::Delete}},
            },
        },
    },
});
```

These usually need a name:

- a button, toggle button or app bar button that shows only an icon
- a `ui::Slider`, `ui::TextBox`, `ui::ComboBox`, `ui::NumberBox`, `ui::PasswordBox` or `ui::Rating` without a `.header`
- a `ui::ProgressBar` or `ui::ProgressRing`, so the reader can say what is in progress
- an `ui::Image` or `ui::Icon` that carries meaning

A tool tip is not read as the name. Set `.accessibility.name` even when `.tool_tip` says the same.

`.accessibility` has four fields:

| Field | Use |
| --- | --- |
| `name` | The short name read out. Set it when the control's own text is missing or unclear ("Delete" for an icon, "Delete note" when there are several Delete buttons). |
| `description` | A longer explanation the reader offers after the name. Leave it empty unless the name is not enough. |
| `decorative` | Leaves the element out of the automation tree and lists its children in its place. Use it for an icon next to a label that says the same, a separator, or a stack that only lays other controls out. |
| `automation_id` | A fixed identifier for test tools, see [Give Controls an ID](#give-controls-an-id). Screen readers don't read it. |

A field with an `.error` reports its value as invalid, and the error text becomes its description, so a reader hears both.

For a control you write yourself, the tutorial [Your Own Element](../extending-the-tree/01-your-own-element.md#automation) shows how it reports its role, name and patterns.

## Check It in Tests

`ui::describe_automation()` prints the automation tree the way a client sees it, one line per element. It needs no window on screen, so it works in the headless tests from [Testing Your UI](testing.md):

```cpp
ui::testing::HeadlessUI ui = ui::testing::create_headless();
ui.window().set_content(/* the form above */);
ui.layout({400, 300});

REQUIRE(ui::describe_automation(*ui.window().content()) ==
        "Group\n"
        "  Edit \"Name\" [Value=\"\"]\n"
        "  Slider \"Volume\" [RangeValue=0.00]\n"
        "  Button \"Save\" [Invoke]\n"
        "  Button \"Delete\" [Invoke]\n");
```

The decorative row is gone, and its buttons sit directly under the form. A line that reads `Button [Invoke]` with no name is a control that still needs one.

To act on a control the way a screen reader does, ask for its patterns with `ui::automation_providers()`. Here `remember` is the `ui::Control<ui::CheckBox>` handle of a check box:

```cpp
if (ui::ToggleProvider* toggle = ui::automation_providers(*remember).toggle)
{
    toggle->toggle_next();
}
```

`ui::testing::TestAutomationEvents` records what the window reports, one line per event:

```cpp
ui::testing::TestAutomationEvents events;
ui::testing::seams(ui.window()).set_automation_events(&events);

ui::testing::seams(ui.window()).handle_key({ui::Key::Tab, ui::KeyAction::Down});
ui.layout({400, 300});
REQUIRE(events.log == std::vector<std::string>{"focus Edit \"Name\""});
```

The window reports after each layout, so lay out after the input as the real window does.

## Check It by Hand

- **Narrator** ships with Windows. Ctrl+Win+Enter turns it on and off. Tab through the window and listen to what it says for each control.
- **Accessibility Insights for Windows** is a free Microsoft tool. Its live inspect mode shows the element under the mouse or with focus, with its role, name and patterns, and its automated checks find missing names.

## Drive an App with UI Automation

Because the window is a regular UI Automation tree, any UI Automation client can drive a libwinui app from outside: start the program, find elements, click, type and read values. This is end-to-end testing against the real, rendered app, next to the headless tests above. Common clients are:

- .NET's `System.Windows.Automation`, also usable from PowerShell
- FlaUI for C# tests
- pywinauto with `backend="uia"` for Python
- Appium with a Windows driver

### What a Client Sees

- The app's window is a top-level element of type Window, named after the window title.
- Every control is an element with a control type and a name, as `describe_automation()` prints them.
- A popup, such as a menu, a flyout or a ComboBox's list, appears under the element that opened it while it is open. A submenu appears under its menu item.
- An element's AutomationId is the `.accessibility.automation_id` the app gave it. Elements without one have none, and a test finds them by control type and name.

### Give Controls an ID

A name changes when the text is reworded or the app is translated, and two elements can share one. Give the controls a test reaches an `automation_id`, and the test finds them the same way in every run and every language:

```cpp
ui::TextBox{.header = "Name", .accessibility = {.automation_id = "customer-name"}},
ui::Button{.text = "Save", .accessibility = {.automation_id = "save"}, .on_click = [&] { save(); }},
```

`describe_automation()` prints the id after the name, so a headless test sees it too:

```text
Edit "Name" #customer-name [Value=""]
Button "Save" #save [Invoke]
```

An id only has to be unique among the elements a test searches, usually the window. Items a control draws itself, such as list items and tree rows, have no id. Find them by name under their control, which you find by id.

### Patterns by Control

| Control | Pattern | What a test does with it |
| --- | --- | --- |
| Button, AppBarButton, menu item, link | Invoke | Click it |
| CheckBox, ToggleSwitch, ToggleButton, a checkable menu item | Toggle | Read or flip its state |
| TextBox, PasswordBox, AutoSuggestBox | Value | Read or set the text |
| Slider, NumberBox, Rating, ProgressBar | RangeValue | Read or set the number |
| ComboBox, Expander, menu bar header, TreeView row with children | ExpandCollapse | Open and close it |
| List item, tree row, tab, RadioButton | SelectionItem | Select it |
| ListView, TableView, Tabs | Selection | Read what is selected |
| CalendarDatePicker, DatePicker, TimePicker | Invoke, Value | Open the picker, read the chosen date or time (read only) |
| CalendarView | Grid, Table, Selection, Value | Read the selected dates, find a day by row and column |
| A CalendarView day | SelectionItem, GridItem, TableItem | Select the day |
| Scroll | Scroll | Scroll by a page or to a position |

### An Example in PowerShell

This script starts an app with the ids above, waits for its window, fills in a field, opens a menu and reads the states of its items, then saves. It needs nothing but Windows.

```powershell
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A     = [System.Windows.Automation.AutomationElement]
$CT    = [System.Windows.Automation.ControlType]
$Scope = [System.Windows.Automation.TreeScope]::Descendants

# The first element of `type` named `name` under `root`, waiting up to 5 s for it to appear.
function Find($root, $type, $name) {
    $condition = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, $type)),
        (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $name)))
    for ($i = 0; $i -lt 50; $i++) {
        $found = $root.FindFirst($Scope, $condition)
        if ($found) { return $found }
        Start-Sleep -Milliseconds 100
    }
    throw "No $($type.ProgrammaticName) named '$name'"
}
# The element with this AutomationId under `root`, waiting the same way.
function FindId($root, $id) {
    $condition = New-Object System.Windows.Automation.PropertyCondition($A::AutomationIdProperty, $id)
    for ($i = 0; $i -lt 50; $i++) {
        $found = $root.FindFirst($Scope, $condition)
        if ($found) { return $found }
        Start-Sleep -Milliseconds 100
    }
    throw "No element with AutomationId '$id'"
}
function Pattern($element, $pattern) { $element.GetCurrentPattern($pattern::Pattern) }

$app = Start-Process "my_app.exe" -PassThru
try {
    $app.WaitForInputIdle(10000) | Out-Null
    $window = $A::FromHandle($app.MainWindowHandle)

    $name = FindId $window "customer-name"
    (Pattern $name ([System.Windows.Automation.ValuePattern])).SetValue("Ada")

    $view = Find $window $CT::MenuItem "View"
    (Pattern $view ([System.Windows.Automation.ExpandCollapsePattern])).Expand()
    $wrap = Find $view $CT::MenuItem "Wrap lines"
    "Wrap lines: " + (Pattern $wrap ([System.Windows.Automation.TogglePattern])).Current.ToggleState
    (Pattern $view ([System.Windows.Automation.ExpandCollapsePattern])).Collapse()

    $save = FindId $window "save"
    (Pattern $save ([System.Windows.Automation.InvokePattern])).Invoke()
} finally {
    $app.CloseMainWindow() | Out-Null
    if (-not $app.WaitForExit(5000)) { Stop-Process $app -Force }
}
```

### Tips

- **Wait for elements.** The app updates its tree when it gets to the action, so an element appears a moment after the action that causes it. Search in a loop with a timeout, as `Find` above does, instead of sleeping for a fixed time.
- **Invoke returns before the click runs.** The handler runs on the app's UI thread right after, so a dialog it opens doesn't block the test. Wait for whatever the click should change.
- **Prefer patterns to simulated keys and clicks.** They don't depend on screen position, window focus or keyboard layout. Use simulated input, such as `SendKeys` or `SendInput`, for what only the keyboard does: shortcuts, Tab order, typing into a field character by character.
- **Listen desktop-wide for menu events.** UI Automation raises a menu's opened and closed events on the desktop, so a handler registered on the app's window alone doesn't get them.
- **Run on an unlocked desktop.** UI Automation and simulated input need an interactive session. A CI agent needs to run as a logged-in user with the screen unlocked, not as a service.
