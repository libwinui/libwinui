// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/render/ContextMenuHost.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui::testing
{

/// ContextMenuHost test double: records every menu a control asks to show and answers with a
/// scripted choice instead of opening a popup.
///
/// Script the answer with pick() (by visible text) or pick_index() before the control's
/// Element::handle_context_menu() runs; the script applies to the next show() only and then
/// resets to a dismissal. Like the real menu, a separator or a disabled item can't be picked, so
/// scripting one reads as a dismissal.
///
/// Example:
/// @code
/// ui::testing::HeadlessTextMeasurer measurer;
/// ui::TextControl block{"orange text"};
/// block.set_selection_enabled(true);
/// (void)block.measure({400, 30}, measurer, ui::Theme::default_light());
/// block.arrange({0, 0, 400, 30});
/// block.select(0, 6);
/// ui::testing::set_focused(block, true);
///
/// ui::testing::TestClipboard       clipboard;
/// ui::testing::TestContextMenuHost menu;
/// ui::testing::set_clipboard(&clipboard);
/// menu.pick("Copy");
/// REQUIRE(block.handle_context_menu(ui::Point{5, 5}, menu));
/// REQUIRE(menu.show_count() == 1);
/// REQUIRE(clipboard.text() == "orange");
/// @endcode
class TestContextMenuHost final : public ContextMenuHost
{
public:
    // ---- scripting (applies to the next show() only, then resets to a dismissal) ----
    /// Makes the next show() choose the first non-separator item whose text equals `text`.
    /// Overrides an earlier pick_index(); no match (or a disabled match) dismisses.
    void pick(std::string text)
    {
        pick_text_  = std::move(text);
        pick_index_ = std::nullopt;
    }
    /// Makes the next show() choose the item at `index` (separators count). Overrides an earlier
    /// pick(); an out-of-range, separator or disabled index dismisses.
    void pick_index(std::size_t index)
    {
        pick_index_ = index;
        pick_text_  = std::nullopt;
    }

    // ---- inspection ----
    /// How many menus have been shown over this host's lifetime.
    [[nodiscard]] int show_count() const
    {
        return show_count_;
    }
    /// The items of the most recent show() (empty before the first one).
    [[nodiscard]] const std::vector<ContextMenuItem>& last_items() const
    {
        return last_items_;
    }
    /// Where the most recent menu was anchored, in window DIPs (the caret for a keyboard-invoked
    /// menu).
    [[nodiscard]] Point last_position() const
    {
        return last_position_;
    }
    /// The last menu's item texts, separators as empty strings.
    [[nodiscard]] std::vector<std::string> last_texts() const
    {
        std::vector<std::string> texts;
        texts.reserve(last_items_.size());
        for (const ContextMenuItem& item : last_items_)
        {
            texts.push_back(item.text);
        }
        return texts;
    }
    /// The enabled state of the last menu's item with visible text `text` (nullopt: absent).
    [[nodiscard]] std::optional<bool> enabled(const std::string& text) const
    {
        for (const ContextMenuItem& item : last_items_)
        {
            if (!item.is_separator && item.text == text)
            {
                return item.enabled;
            }
        }
        return std::nullopt;
    }

    // ---- ContextMenuHost ----
    /// Records `position`/`items`, bumps show_count(), and returns the scripted choice (then
    /// clears the script). Returns nullopt, a dismissal, when nothing pickable was scripted.
    [[nodiscard]] std::optional<std::size_t> show(Point position, const std::vector<ContextMenuItem>& items) override
    {
        ++show_count_;
        last_items_    = items;
        last_position_ = position;

        std::optional<std::size_t> choice = pick_index_;
        if (pick_text_)
        {
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (!items[i].is_separator && items[i].text == *pick_text_)
                {
                    choice = i;
                    break;
                }
            }
        }
        pick_text_  = std::nullopt;
        pick_index_ = std::nullopt;

        // As in the real menu, a separator or a disabled item can't be picked.
        if (choice && (*choice >= items.size() || items[*choice].is_separator || !items[*choice].enabled))
        {
            choice = std::nullopt;
        }
        return choice;
    }

private:
    std::optional<std::string>   pick_text_;
    std::optional<std::size_t>   pick_index_;
    int                          show_count_ = 0;
    std::vector<ContextMenuItem> last_items_;
    Point                        last_position_{};
};

} // namespace ui::testing
