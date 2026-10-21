// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"

#include <format>
#include <string>
#include <variant>
#include <vector>

namespace ui::testing
{

/// AutomationEvents test double: keeps a line of text for every event a window reports, the way
/// a screen reader would hear about it. Structure changes go to `structure` instead, as they come
/// with many changes and say little on their own.
///
/// Install it with seams(window).set_automation_events(); the window then reports to it after
/// each layout. Set `listening_now` to false to act as if no screen reader is listening, and the
/// window reports nothing.
///
/// Example:
/// @code
/// ui::testing::TestAutomationEvents events;
/// ui::testing::seams(window).set_automation_events(&events);
/// ui::testing::seams(window).handle_key({ui::Key::Tab, ui::KeyAction::Down});
/// REQUIRE(events.log == std::vector<std::string>{"focus Button \"Save\""});
/// @endcode
class TestAutomationEvents final : public AutomationEvents
{
public:
    /// Whether the window should report; true by default.
    bool listening_now = true;
    /// One line per event: `focus <role> "<name>"`, or `<property> "<name>": <before> -> <after>`.
    std::vector<std::string> log;
    /// The node of each focus event, in order.
    std::vector<Element*> focused;
    /// The node of each structure change, in order.
    std::vector<Element*> structure;

    [[nodiscard]] bool listening() const override
    {
        return listening_now;
    }
    void focus_changed(Element& node) override
    {
        focused.push_back(&node);
        log.push_back(std::format("focus {} \"{}\"", automation_role_name(node.automation_role()), node.automation_name()));
    }
    void property_changed(Element&               node,
                          AutomationProperty     property,
                          const AutomationValue& before,
                          const AutomationValue& after) override
    {
        log.push_back(
            std::format("{} \"{}\": {} -> {}", property_name(property), node.automation_name(), text(before), text(after)));
    }

    void menu_opened(Element& menu) override
    {
        log.push_back(std::format("menu opened {}{}{}", '"', menu.automation_name(), '"'));
    }
    void menu_closed(Element& menu) override
    {
        log.push_back(std::format("menu closed {}{}{}", '"', menu.automation_name(), '"'));
    }
    void structure_changed(Element& node) override
    {
        structure.push_back(&node);
    }

private:
    [[nodiscard]] static const char* property_name(AutomationProperty property)
    {
        switch (property)
        {
            case AutomationProperty::Name:
                return "Name";
            case AutomationProperty::ToggleState:
                return "ToggleState";
            case AutomationProperty::ExpandCollapseState:
                return "ExpandCollapseState";
            case AutomationProperty::IsSelected:
                return "IsSelected";
            case AutomationProperty::Value:
                return "Value";
            case AutomationProperty::RangeValue:
                return "RangeValue";
        }
        return "?";
    }
    [[nodiscard]] static std::string text(const AutomationValue& value)
    {
        if (const auto* b = std::get_if<bool>(&value))
        {
            return *b ? "true" : "false";
        }
        if (const auto* d = std::get_if<double>(&value))
        {
            return std::format("{}", *d);
        }
        if (const auto* s = std::get_if<std::string>(&value))
        {
            return "\"" + *s + "\"";
        }
        if (const auto* t = std::get_if<ToggleState>(&value))
        {
            return *t == ToggleState::On ? "On" : *t == ToggleState::Off ? "Off" : "Indeterminate";
        }
        return "none";
    }
};

} // namespace ui::testing
