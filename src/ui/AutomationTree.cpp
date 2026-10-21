// SPDX-License-Identifier: MIT
#include "winui/ui/AutomationTree.hpp"

#include "winui/core/Utf8.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <cstdio>

namespace ui
{
namespace
{

bool skipped(const Element& e)
{
    return e.accessibility().decorative || e.automation_is_native_host();
}

void collect_filtered_children(const Element& node, std::vector<Element*>& out)
{
    std::vector<Element*> raw;
    node.automation_children(raw);
    // An open popup's content comes last under the element that opened it, unless the content
    // names a closer parent of its own (a menu under the item it drops from).
    if (const PopupRequest* popup = detail::TreeAccess::popup_request(node);
        popup != nullptr && popup->content != nullptr && std::ranges::find(raw, popup->content) == raw.end() &&
        automation_parent(*popup->content) == &node)
    {
        raw.push_back(popup->content);
    }
    for (Element* child : raw)
    {
        if (child == nullptr || child->visibility() != Visibility::Visible)
        {
            continue; // a collapsed node is not on screen, and neither is anything in it
        }
        if (skipped(*child))
        {
            // Splice: a decorative/native-host node never appears itself, but whatever it
            // wraps still needs to reach its nearest visible ancestor.
            collect_filtered_children(*child, out);
        }
        else
        {
            out.push_back(child);
        }
    }
}

const char* role_str(AutomationRole role)
{
    switch (role)
    {
        case AutomationRole::Group:
            return "Group";
        case AutomationRole::Pane:
            return "Pane";
        case AutomationRole::Button:
            return "Button";
        case AutomationRole::CheckBox:
            return "CheckBox";
        case AutomationRole::RadioButton:
            return "RadioButton";
        case AutomationRole::Edit:
            return "Edit";
        case AutomationRole::Text:
            return "Text";
        case AutomationRole::Image:
            return "Image";
        case AutomationRole::List:
            return "List";
        case AutomationRole::ListItem:
            return "ListItem";
        case AutomationRole::ComboBox:
            return "ComboBox";
        case AutomationRole::Slider:
            return "Slider";
        case AutomationRole::ProgressBar:
            return "ProgressBar";
        case AutomationRole::Tab:
            return "Tab";
        case AutomationRole::TabItem:
            return "TabItem";
        case AutomationRole::Tree:
            return "Tree";
        case AutomationRole::TreeItem:
            return "TreeItem";
        case AutomationRole::Menu:
            return "Menu";
        case AutomationRole::MenuItem:
            return "MenuItem";
        case AutomationRole::MenuBar:
            return "MenuBar";
        case AutomationRole::ToolBar:
            return "ToolBar";
        case AutomationRole::Hyperlink:
            return "Hyperlink";
        case AutomationRole::Document:
            return "Document";
        case AutomationRole::Header:
            return "Header";
        case AutomationRole::HeaderItem:
            return "HeaderItem";
        case AutomationRole::Separator:
            return "Separator";
        case AutomationRole::SpinButton:
            return "SpinButton";
        case AutomationRole::Thumb:
            return "Thumb";
        case AutomationRole::SplitButton:
            return "SplitButton";
        case AutomationRole::DataGrid:
            return "DataGrid";
        case AutomationRole::DataItem:
            return "DataItem";
        case AutomationRole::Custom:
            return "Custom";
        case AutomationRole::TitleBar:
            return "TitleBar";
        case AutomationRole::Calendar:
            return "Calendar";
    }
    return "Group";
}

const char* toggle_state_str(ToggleState state)
{
    switch (state)
    {
        case ToggleState::Off:
            return "Off";
        case ToggleState::On:
            return "On";
        case ToggleState::Indeterminate:
            return "Indeterminate";
    }
    return "Off";
}

std::string fnum(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// A label for the semantic dump, matching render/Describe.cpp's own printable(): every code point
// outside printable ASCII is escaped, so the golden is pure ASCII and stable regardless of locale.
std::string printable(std::string_view text)
{
    std::string s;
    for (const char32_t cp : code_points(text))
    {
        if (cp >= 0x20 && cp < 0x7F)
        {
            s.push_back(static_cast<char>(cp));
        }
        else
        {
            char buf[12];
            std::snprintf(buf, sizeof(buf), cp > 0xFFFF ? "\\U%08X" : "\\u%04X", static_cast<unsigned>(cp));
            s += buf;
        }
    }
    return s;
}

// One "[Pattern=state]"-shaped token per advertised pattern, in a fixed order so the dump is
// deterministic regardless of how AutomationPatterns' bits happen to be combined.
std::string patterns_str(const Element& node)
{
    const AutomationProviders p = automation_providers(node);

    std::vector<std::string> tokens;
    if (p.invoke != nullptr)
    {
        tokens.push_back("Invoke");
    }
    if (p.toggle != nullptr)
    {
        tokens.push_back(std::string("Toggle=") + toggle_state_str(p.toggle->toggle_value()));
    }
    if (p.selection_item != nullptr)
    {
        tokens.push_back(std::string("SelectionItem=") + (p.selection_item->item_selected() ? "Selected" : "Unselected"));
    }
    if (p.selection != nullptr)
    {
        tokens.push_back("Selection");
    }
    if (p.expand_collapse != nullptr)
    {
        tokens.push_back(std::string("ExpandCollapse=") + (p.expand_collapse->expanded_state() ? "Expanded" : "Collapsed"));
    }
    if (p.range_value != nullptr)
    {
        tokens.push_back("RangeValue=" + fnum(p.range_value->range_value()) +
                         (p.range_value->range_read_only() ? " (ReadOnly)" : ""));
    }
    if (p.value != nullptr)
    {
        tokens.push_back("Value=\"" + printable(p.value->text_value()) + "\"" +
                         (p.value->text_value_read_only() ? " (ReadOnly)" : ""));
    }
    if (p.scroll != nullptr)
    {
        tokens.push_back("Scroll");
    }
    if (p.grid != nullptr)
    {
        tokens.push_back(
            "Grid=" + std::to_string(p.grid->grid_row_count()) + "x" + std::to_string(p.grid->grid_column_count()));
    }
    if (p.grid_item != nullptr)
    {
        tokens.push_back("GridItem=" + std::to_string(p.grid_item->grid_item_row()) + "," +
                         std::to_string(p.grid_item->grid_item_column()));
    }
    if (p.table != nullptr)
    {
        tokens.push_back("Table");
    }
    if (p.table_item != nullptr)
    {
        tokens.push_back("TableItem");
    }

    if (tokens.empty())
    {
        return {};
    }
    std::string out = " [";
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (i != 0)
        {
            out += " ";
        }
        out += tokens[i];
    }
    out += "]";
    return out;
}

void describe_recursive(const Element& node, int depth, std::string& out)
{
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
    out += role_str(node.automation_role());
    const std::string name = node.automation_name();
    if (!name.empty())
    {
        out += " \"" + printable(name) + "\"";
    }
    if (const std::string& id = node.accessibility().automation_id; !id.empty())
    {
        out += " #" + printable(id);
    }
    out += patterns_str(node);
    out += "\n";
    for (const Element* child : automation_children(node))
    {
        describe_recursive(*child, depth + 1, out);
    }
}

} // namespace

AutomationProviders automation_providers(const Element& node)
{
    return detail::TreeAccess::automation_providers(const_cast<Element&>(node));
}

std::vector<Element*> automation_children(const Element& node)
{
    std::vector<Element*> out;
    collect_filtered_children(node, out);
    return out;
}

Element* automation_parent(const Element& node)
{
    Element* p = node.parent();
    if (p != nullptr)
    {
        if (Element* between = p->automation_parent_of(node))
        {
            p = between;
        }
    }
    while (p != nullptr && skipped(*p))
    {
        p = p->parent();
    }
    return p;
}

const char* automation_role_name(AutomationRole role)
{
    return role_str(role);
}

Element* automation_focus(Element* focused)
{
    Element* node = focused;
    // A few steps at most (a menu bar, its menu, a submenu); the bound guards a cycle.
    for (int step = 0; node != nullptr && step < 16; ++step)
    {
        Element* inner = node->automation_focus();
        if (inner == nullptr || inner == node)
        {
            break;
        }
        node = inner;
    }
    return node;
}

AutomationValue automation_value(const Element& node, AutomationProperty property)
{
    const AutomationProviders p = automation_providers(node);
    switch (property)
    {
        case AutomationProperty::Name:
            return node.automation_name();
        case AutomationProperty::ToggleState:
            return p.toggle != nullptr ? AutomationValue{p.toggle->toggle_value()} : AutomationValue{};
        case AutomationProperty::ExpandCollapseState:
            return p.expand_collapse != nullptr ? AutomationValue{p.expand_collapse->expanded_state()} : AutomationValue{};
        case AutomationProperty::IsSelected:
            return p.selection_item != nullptr ? AutomationValue{p.selection_item->item_selected()} : AutomationValue{};
        case AutomationProperty::Value:
            return p.value != nullptr ? AutomationValue{p.value->text_value()} : AutomationValue{};
        case AutomationProperty::RangeValue:
            return p.range_value != nullptr ? AutomationValue{p.range_value->range_value()} : AutomationValue{};
    }
    return {};
}

std::string describe_automation(const Element& root)
{
    std::string out;
    describe_recursive(root, 0, out);
    return out;
}

} // namespace ui
