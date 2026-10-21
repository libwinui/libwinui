// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"

#include <optional>
#include <string>
#include <string_view>

#include <cstdint>

namespace ui
{

/// The system pointer shapes an element can ask for with Element::set_cursor(). Each shows the
/// system cursor of the same name.
///
/// Example:
/// @code
/// void set_cursors(ui::Element& splitter, ui::Element& link)
/// {
///     splitter.set_cursor(ui::CursorShape::SizeWestEast);
///     link.set_cursor(ui::CursorShape::Hand);
/// }
/// @endcode
enum class CursorShape
{
    Arrow,                  ///< The standard arrow; the default.
    Cross,                  ///< Crosshair, for precise picking.
    Hand,                   ///< Pointing hand, over hyperlinks.
    Help,                   ///< Arrow with a question mark.
    IBeam,                  ///< Text caret, over editable or selectable text.
    SizeAll,                ///< Four-way arrow, for moving.
    SizeNortheastSouthwest, ///< Diagonal resize, / direction.
    SizeNorthSouth,         ///< Vertical resize.
    SizeNorthwestSoutheast, ///< Diagonal resize, \ direction.
    SizeWestEast,           ///< Horizontal resize, e.g. a column splitter.
    UniversalNo,            ///< Slashed circle. The action is not allowed here.
    UpArrow,                ///< Vertical arrow.
    Wait,                   ///< Busy. Input is blocked.
    Pin,                    ///< Location pin.
    Person,                 ///< Person.
    AppStarting,            ///< Arrow with a busy indicator. Input still works.
};

/// The modifier keys held with a key or a pointer event, and the ones a KeyCombo asks for.
///
/// Example:
/// @code
/// class ListControl : public ui::Element
/// {
/// protected:
///     bool pointer_override(const ui::PointerEvent& e) override
///     {
///         if (e.action == ui::PointerAction::Down && e.modifiers.shift)
///         {
///             extend_selection_to(e.position);
///             return true;
///         }
///         return false;
///     }
///
///     bool key_override(const ui::KeyEvent& e) override
///     {
///         // Exactly Ctrl+Shift, nothing else held.
///         return e.modifiers == ui::ModifierKeys{.ctrl = true, .shift = true};
///     }
///
/// private:
///     void extend_selection_to(ui::Point position);
/// };
/// @endcode
struct ModifierKeys
{
    bool ctrl  = false; ///< Ctrl.
    bool shift = false; ///< Shift.
    bool alt   = false; ///< Alt.
    bool win   = false; ///< The Windows logo key.

    /// True when no modifier is held.
    [[nodiscard]] constexpr bool none() const
    {
        return !ctrl && !shift && !alt && !win;
    }
    /// Both sets together: `ui::Ctrl + ui::Shift`.
    friend constexpr ModifierKeys operator+(ModifierKeys a, ModifierKeys b)
    {
        return ModifierKeys{a.ctrl || b.ctrl, a.shift || b.shift, a.alt || b.alt, a.win || b.win};
    }
    /// Member-wise comparison.
    friend constexpr bool operator==(const ModifierKeys&, const ModifierKeys&) = default;
};

/// Ctrl, for writing a key combination: `ui::Ctrl + ui::Key::S`.
inline constexpr ModifierKeys Ctrl{.ctrl = true};
/// Shift, for writing a key combination: `ui::Ctrl + ui::Shift + ui::Key::N`.
inline constexpr ModifierKeys Shift{.shift = true};
/// Alt, for writing a key combination: `ui::Alt + ui::Key::Enter`.
inline constexpr ModifierKeys Alt{.alt = true};
/// The Windows logo key, for writing a key combination.
inline constexpr ModifierKeys Win{.win = true};

/// Which mouse button a PointerEvent is about.
enum class PointerButton
{
    None,   ///< No button: moves, leaves and wheel turns.
    Left,   ///< The primary button (also a touch or pen contact).
    Right,  ///< The secondary button; context menus open on its Up.
    Middle, ///< The wheel button.
};

/// What happened in a PointerEvent.
enum class PointerAction
{
    Move,  ///< The pointer moved over the element (hover), with or without a button held.
    Down,  ///< `button` was pressed; see PointerEvent::click_count for double/triple clicks.
    Up,    ///< `button` was released.
    Leave, ///< The pointer left the element (or the window); clears hover state.
    Wheel, ///< A wheel turn; see PointerEvent::wheel_delta and horizontal_wheel.
};

/// What kind of device a PointerEvent came from.
enum class PointerDeviceType : std::uint8_t
{
    Mouse, ///< A mouse or touchpad: the default, and what an event of unknown origin counts as.
    Pen,   ///< A pen or stylus, its tip or its eraser end.
    Touch, ///< A finger on a touch screen.
};

/// A set of pointer devices, for a control that takes input from some
/// kinds of device only, such as ui::InkCanvasControl::set_input_device_types(). Flags.
enum class PointerDeviceTypes : std::uint8_t
{
    None  = 0,                   ///< No device.
    Mouse = 1u << 0,             ///< The mouse and the touchpad.
    Touch = 1u << 1,             ///< Touch screens.
    Pen   = 1u << 2,             ///< Pens, tip and eraser end alike.
    All   = Mouse | Touch | Pen, ///< Every device.
};

/// Combines two device sets (`ui::PointerDeviceTypes::Pen | ui::PointerDeviceTypes::Mouse`).
[[nodiscard]] constexpr PointerDeviceTypes operator|(PointerDeviceTypes a, PointerDeviceTypes b)
{
    return static_cast<PointerDeviceTypes>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// Whether `set` includes `device`.
[[nodiscard]] constexpr bool has_device(PointerDeviceTypes set, PointerDeviceType device)
{
    const auto bit = device == PointerDeviceType::Mouse   ? PointerDeviceTypes::Mouse
                     : device == PointerDeviceType::Touch ? PointerDeviceTypes::Touch
                                                          : PointerDeviceTypes::Pen;
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(bit)) != 0;
}

/// One mouse / touch / pen event, in window-client DIPs, as delivered to
/// Element::handle_pointer() and Window::handle_pointer().
///
/// A pen or a finger arrives as mouse events. `device` says which it was and, for a pen, the event
/// carries its pressure, tilt and buttons. A press-and-release pair is what a control counts as a
/// click.
///
/// Example:
/// @code
/// // In an element of your own: a press and a release inside the bounds is a click.
/// class ToggleControl : public ui::Element
/// {
/// protected:
///     bool pointer_override(const ui::PointerEvent& e) override
///     {
///         if (e.action == ui::PointerAction::Down && e.button == ui::PointerButton::Left)
///         {
///             pressed_ = true;
///             return true;
///         }
///         if (e.action == ui::PointerAction::Up && pressed_)
///         {
///             pressed_ = false;
///             if (bounds().contains(e.position))
///             {
///                 toggle();
///             }
///             return true;
///         }
///         return false;
///     }
///
///     // The wheel arrives through wheel_override(); return true when the element scrolled.
///     bool wheel_override(const ui::PointerEvent& e) override
///     {
///         offset_ += e.wheel_delta; // positive scrolls down
///         return true;
///     }
///
/// private:
///     void toggle();
///
///     bool  pressed_ = false;
///     float offset_  = 0.0f;
/// };
/// @endcode
struct PointerEvent
{
    /// Pointer position in window-client DIPs (the same space as Element::bounds()).
    Point position{};
    /// The button pressed or released; None for Move, Leave and Wheel.
    PointerButton button = PointerButton::None;
    /// What happened; defaults to a plain move.
    PointerAction action = PointerAction::Move;

    /// Valid only for PointerAction::Wheel: the scroll amount in DIPs. Positive scrolls down, so the
    /// content moves up, as with a non-inverted wheel. One notch of a standard mouse wheel is a few
    /// dozen DIPs. Code that constructs an event converts the platform's wheel units to DIPs first.
    float wheel_delta = 0.0f;

    /// The modifier keys held at the time of this event. TextBox extends its selection on
    /// Shift+click, and Ctrl+wheel zooms where a plain wheel turn scrolls.
    ModifierKeys modifiers{};

    /// Valid only for PointerAction::Down: 1 for a plain click, 2/3 for a
    /// double-/triple-click. Clicks count as one sequence while they fall within the system's
    /// double-click time and distance. TextBox selects a word on a double-click and everything on a
    /// triple-click.
    int click_count = 1;

    /// Valid only for PointerAction::Wheel: the turn came from a horizontal wheel or a sideways
    /// touchpad swipe, and `wheel_delta` is positive toward the right. A vertical wheel with Shift
    /// held stays a vertical turn here. A scroller decides itself whether Shift redirects it.
    bool horizontal_wheel = false;

    /// The device behind the event. A pen's tip and a finger press, move and release as the left
    /// button.
    PointerDeviceType device = PointerDeviceType::Mouse;
    /// How hard the pen or finger presses, 0..1. 0.5 when the device does not say, which is
    /// always the case for the mouse, so pressure-sensitive drawing comes out even.
    float pressure = 0.5f;
    /// A pen's tilt along x in degrees, -90..90, positive toward the right; 0 for any other device.
    float tilt_x = 0.0f;
    /// A pen's tilt along y in degrees, -90..90, positive toward the user; 0 for any other device.
    float tilt_y = 0.0f;
    /// A pen is erasing: its eraser end is down (the pen is turned over), or its eraser button
    /// is held. False for any other device.
    bool eraser = false;
    /// A pen's barrel button is held. With the default pen settings the system turns a press with it
    /// held into a right-click, so it shows up mostly on hover moves.
    bool barrel_button = false;
};

/// The keys a control or a shortcut reads: navigation and editing keys, letters, digits, function
/// keys and the keypad operators. Any other key arrives as Unknown.
///
/// Example:
/// @code
/// const ui::KeyCombo save = ui::Ctrl + ui::Key::S; // save.text() is "Ctrl+S"
///
/// // In an element of your own:
/// class FileListControl : public ui::Element
/// {
/// protected:
///     bool key_override(const ui::KeyEvent& e) override
///     {
///         if (e.action == ui::KeyAction::Down && e.key == ui::Key::Delete)
///         {
///             remove_selected();
///             return true;
///         }
///         return false;
///     }
///
/// private:
///     void remove_selected();
/// };
/// @endcode
enum class Key
{
    Unknown,    ///< A key outside this set; controls ignore it.
    A,          ///< Letter A; TextBox's Ctrl+A select-all.
    B,          ///< Letter B.
    C,          ///< Letter C; TextBox's Ctrl+C copy.
    D,          ///< Letter D.
    E,          ///< Letter E.
    F,          ///< Letter F.
    G,          ///< Letter G.
    H,          ///< Letter H.
    I,          ///< Letter I.
    J,          ///< Letter J.
    K,          ///< Letter K.
    L,          ///< Letter L.
    M,          ///< Letter M.
    N,          ///< Letter N.
    O,          ///< Letter O.
    P,          ///< Letter P.
    Q,          ///< Letter Q.
    R,          ///< Letter R.
    S,          ///< Letter S.
    T,          ///< Letter T.
    U,          ///< Letter U.
    V,          ///< Letter V; TextBox's Ctrl+V paste.
    W,          ///< Letter W.
    X,          ///< Letter X; TextBox's Ctrl+X cut.
    Y,          ///< Letter Y; StandardCommand::Redo's Ctrl+Y.
    Z,          ///< Letter Z; TextBox's Ctrl+Z, which also reverts an autocorrection.
    Number0,    ///< Digit 0 on the main row.
    Number1,    ///< Digit 1 on the main row.
    Number2,    ///< Digit 2 on the main row.
    Number3,    ///< Digit 3 on the main row.
    Number4,    ///< Digit 4 on the main row.
    Number5,    ///< Digit 5 on the main row.
    Number6,    ///< Digit 6 on the main row.
    Number7,    ///< Digit 7 on the main row.
    Number8,    ///< Digit 8 on the main row.
    Number9,    ///< Digit 9 on the main row.
    F1,         ///< Function key F1.
    F2,         ///< Function key F2.
    F3,         ///< Function key F3.
    F4,         ///< F4; opens and closes a ComboBox.
    F5,         ///< Function key F5.
    F6,         ///< Function key F6.
    F7,         ///< Function key F7.
    F8,         ///< Function key F8.
    F9,         ///< Function key F9.
    F10,        ///< Function key F10.
    F11,        ///< F11; the usual full-screen toggle. No control reads it, an app does.
    F12,        ///< Function key F12.
    Enter,      ///< Activates the focused button (on release); commits a NumberBox edit.
    Space,      ///< Activates the focused button or toggle (on release).
    Tab,        ///< Moves focus; Shift+Tab moves backwards.
    Escape,     ///< Closes the topmost popup, flyout or dialog; leaves a focus scope.
    Backspace,  ///< Deletes before the caret in TextBox and RichEditBox.
    Delete,     ///< Deletes after the caret; StandardCommand::Delete's shortcut.
    Insert,     ///< Insert.
    Home,       ///< Start of line / first item.
    End,        ///< End of line / last item.
    PageUp,     ///< A page up in a scroller; Carousel's previous item.
    PageDown,   ///< A page down in a scroller; Carousel's next item.
    ArrowLeft,  ///< Left arrow: moves the caret, a slider, or between tabs and items.
    ArrowUp,    ///< Up arrow: moves within lists, sliders, menus and text.
    ArrowRight, ///< Right arrow.
    ArrowDown,  ///< Down arrow; Alt+Down opens a SplitButton's or ComboBox's drop-down.
    Add,        ///< The keypad's +.
    Subtract,   ///< The keypad's -.
    Multiply,   ///< The keypad's *.
    Divide,     ///< The keypad's /.
    Plus,       ///< The main keyboard's + key (with = on a US layout).
    Minus,      ///< The main keyboard's - key.
    Comma,      ///< The main keyboard's , key.
    Period,     ///< The main keyboard's . key.
    Slash,      ///< The key with / and ? on a US layout; other layouts put other characters there.
};

/// Whether a KeyEvent is a press or a release.
enum class KeyAction
{
    Down, ///< Pressed, or auto-repeated while held. Shortcuts and navigation fire on Down.
    Up,   ///< Released. Button activation by Enter/Space completes on Up.
};

/// One keyboard event, delivered to the focused element by Window::handle_key().
///
/// Text input is not a KeyEvent: typed characters arrive separately as UTF-8 text. A KeyEvent
/// carries only the semantic Key plus the modifier state.
///
/// Example:
/// @code
/// // In an element of your own, which gets the keys while it has focus:
/// class EditorControl : public ui::Element
/// {
/// protected:
///     bool key_override(const ui::KeyEvent& e) override
///     {
///         if (e.action != ui::KeyAction::Down)
///         {
///             return false;
///         }
///         if (e.key == ui::Key::Enter)
///         {
///             commit();
///             return true;
///         }
///         if (e.key == ui::Key::N && e.modifiers == ui::ModifierKeys{.ctrl = true, .shift = true}) // Ctrl+Shift+N
///         {
///             new_document();
///             return true;
///         }
///         return false;
///     }
///
/// private:
///     void commit();
///     void new_document();
/// };
///
/// // Or for the whole window, before the focused element sees the key:
/// ui::Property<std::string> search_text;
/// window.on_key_preview([&](const ui::KeyEvent& e)
/// {
///     if (e.action == ui::KeyAction::Down && e.key == ui::Key::Escape)
///     {
///         search_text = ""; // cancel the search
///         return ui::Handled; // it goes no further
///     }
///     return ui::NotHandled;
/// });
/// @endcode
struct KeyEvent
{
    Key       key    = Key::Unknown;    ///< The key pressed or released.
    KeyAction action = KeyAction::Down; ///< Press (including auto-repeat) or release.

    /// The modifier keys held alongside `key`. Shift extends a TextBox selection and reverses
    /// Tab; Ctrl or Alt makes a shortcut. The key combinations the system owns (Alt+F4, Alt+Space,
    /// Alt+Tab, Alt+Esc) never arrive.
    ModifierKeys modifiers{};
};

/// Typed text, delivered to the focused element and bubbled up its parents like a key
/// (Element::handle_text()). It arrives separately from the KeyEvent of the key that typed it.
struct TextInputEvent
{
    std::string text{}; ///< The typed text, UTF-8: usually one character.
};

/// Whether `key` is one of the letters A-Z, and which (0 for A). Unset otherwise.
[[nodiscard]] std::optional<int> key_letter_index(Key key);

/// The name shortcut text gives `key` ("S", "Delete", "F4", "+" for the keypad Add), the one a
/// menu item shows beside its label and a tooltip after its text. Empty for Key::Unknown. The
/// named keys (Delete, Enter, the arrows, ...) come from strings().keys, and the view stays
/// valid until the next set_strings().
[[nodiscard]] std::string_view key_display_name(Key key);


/// A key and its modifiers, without the action it runs. Menu items, app-bar buttons and commands
/// carry their shortcut in it. The key combination fires app-wide (Element::handle_global_shortcut())
/// and its text() is shown beside the item. Key::Unknown is no shortcut. Written the way the menu
/// shows it, `ui::Ctrl + ui::Shift + ui::Key::N`, or as a bare key, `ui::Key::F4`. Alt key combinations
/// work, except the ones the system owns (Alt+F4, Alt+Space, Alt+Tab, Alt+Esc), which never
/// reach the window.
///
/// Example:
/// @code
/// const ui::KeyCombo save = ui::Ctrl + ui::Key::S;
///
/// const std::string hint = save.text(); // "Ctrl+S", what a menu shows beside the label
///
/// // In a key handler: is this the key combination?
/// window.on_key_preview([save](const ui::KeyEvent& event)
/// {
///     if (save.matches(event))
///     {
///         save_document();
///         return ui::Handled;
///     }
///     return ui::NotHandled;
/// });
/// @endcode
struct KeyCombo
{
    Key          key = Key::Unknown; ///< The key; Unknown means "no shortcut".
    ModifierKeys modifiers{};        ///< Exactly the modifiers that must be held.

    /// No shortcut.
    constexpr KeyCombo() = default;
    /// The key alone, so a field takes `ui::Key::F4`.
    constexpr KeyCombo(Key k) : key(k) // NOLINT(google-explicit-constructor): a key is a chord
    {
    }
    /// The key with `mods` held: `ui::KeyCombo{ui::Key::S, {.ctrl = true}}`.
    constexpr KeyCombo(Key k, ModifierKeys mods) : key(k), modifiers(mods)
    {
    }

    /// True for Key::Unknown, i.e. no shortcut assigned.
    [[nodiscard]] constexpr bool empty() const
    {
        return key == Key::Unknown;
    }
    /// True if `e` is a key-down of exactly this key combination: the same key and exactly the same
    /// modifiers. Always false for an empty() combination and for key-up events.
    [[nodiscard]] constexpr bool matches(const KeyEvent& e) const
    {
        return !empty() && e.action == KeyAction::Down && e.key == key && e.modifiers == modifiers;
    }
    /// "Ctrl+S" (shortcut_text()); empty for an empty() combination.
    [[nodiscard]] std::string text() const;

    /// Member-wise comparison.
    friend constexpr bool operator==(const KeyCombo&, const KeyCombo&) = default;
};

/// A key combination of `mods` and `key`: `ui::Ctrl + ui::Key::S`.
[[nodiscard]] constexpr KeyCombo operator+(ModifierKeys mods, Key key)
{
    return KeyCombo{key, mods};
}

/// The shortcut text for `chord`: the modifiers in a fixed order (Win, Ctrl, Alt, Shift) and the
/// key, joined with "+", such as "Ctrl+S", "Ctrl+Shift+N", "Alt+Enter" or "Delete". Empty for an
/// empty key combination. The key names come from strings().keys.
[[nodiscard]] std::string shortcut_text(const KeyCombo& chord);

inline std::string KeyCombo::text() const
{
    return shortcut_text(*this);
}

} // namespace ui
