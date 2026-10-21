// SPDX-License-Identifier: MIT
#include "winui/core/Input.hpp"

#include "winui/core/Strings.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ui
{

std::optional<int> key_letter_index(Key key)
{
    switch (key)
    {
        case Key::A:
            return 0;
        case Key::B:
            return 1;
        case Key::C:
            return 2;
        case Key::D:
            return 3;
        case Key::E:
            return 4;
        case Key::F:
            return 5;
        case Key::G:
            return 6;
        case Key::H:
            return 7;
        case Key::I:
            return 8;
        case Key::J:
            return 9;
        case Key::K:
            return 10;
        case Key::L:
            return 11;
        case Key::M:
            return 12;
        case Key::N:
            return 13;
        case Key::O:
            return 14;
        case Key::P:
            return 15;
        case Key::Q:
            return 16;
        case Key::R:
            return 17;
        case Key::S:
            return 18;
        case Key::T:
            return 19;
        case Key::U:
            return 20;
        case Key::V:
            return 21;
        case Key::W:
            return 22;
        case Key::X:
            return 23;
        case Key::Y:
            return 24;
        case Key::Z:
            return 25;
        default:
            return std::nullopt;
    }
}

// A key's name in shortcut text: the named keys from strings().keys, the rest as they are.
std::string_view key_display_name(Key key)
{
    static constexpr const char* Letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                              "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    if (const std::optional<int> letter = key_letter_index(key))
    {
        return Letters[*letter];
    }
    switch (key)
    {
        case Key::Enter:
            return strings().keys.enter;
        case Key::Space:
            return strings().keys.space;
        case Key::Tab:
            return strings().keys.tab;
        case Key::Escape:
            return strings().keys.escape;
        case Key::ArrowUp:
            return strings().keys.up;
        case Key::ArrowDown:
            return strings().keys.down;
        case Key::ArrowLeft:
            return strings().keys.left;
        case Key::ArrowRight:
            return strings().keys.right;
        case Key::Backspace:
            return strings().keys.backspace;
        case Key::Delete:
            return strings().keys.del;
        case Key::Insert:
            return strings().keys.insert;
        case Key::Home:
            return strings().keys.home;
        case Key::End:
            return strings().keys.end;
        case Key::PageUp:
            return strings().keys.page_up;
        case Key::PageDown:
            return strings().keys.page_down;
        case Key::F1:
            return "F1";
        case Key::F2:
            return "F2";
        case Key::F3:
            return "F3";
        case Key::F4:
            return "F4";
        case Key::F5:
            return "F5";
        case Key::F6:
            return "F6";
        case Key::F7:
            return "F7";
        case Key::F8:
            return "F8";
        case Key::F9:
            return "F9";
        case Key::F10:
            return "F10";
        case Key::F11:
            return "F11";
        case Key::F12:
            return "F12";
        case Key::Number0:
            return "0";
        case Key::Number1:
            return "1";
        case Key::Number2:
            return "2";
        case Key::Number3:
            return "3";
        case Key::Number4:
            return "4";
        case Key::Number5:
            return "5";
        case Key::Number6:
            return "6";
        case Key::Number7:
            return "7";
        case Key::Number8:
            return "8";
        case Key::Number9:
            return "9";
        case Key::Add:
            return "+";
        case Key::Subtract:
            return "-";
        case Key::Multiply:
            return "*";
        case Key::Divide:
            return "/";
        case Key::Plus:
            return "+";
        case Key::Minus:
            return "-";
        case Key::Comma:
            return ",";
        case Key::Period:
            return ".";
        case Key::Slash:
            return "/";
        default:
            return "";
    }
}

std::string shortcut_text(const KeyCombo& chord)
{
    const std::string_view name = key_display_name(chord.key);
    if (name.empty())
    {
        return {};
    }
    std::string              text;
    const Strings::KeysText& keys = strings().keys;
    if (chord.modifiers.win)
    {
        text += keys.win + "+";
    }
    if (chord.modifiers.ctrl)
    {
        text += keys.ctrl + "+";
    }
    if (chord.modifiers.alt)
    {
        text += keys.alt + "+";
    }
    if (chord.modifiers.shift)
    {
        text += keys.shift + "+";
    }
    text += name;
    return text;
}

} // namespace ui
