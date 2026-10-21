// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"

#include <string>
#include <variant>
#include <vector>

namespace ui
{

/// The children an accessibility client sees under `node`, in navigation order.
///
/// A decorative node (Accessibility::decorative) or a NativeControl (automation_is_native_host())
/// is skipped, and its own children appear in its place as children of `node`. A collapsed node
/// (Visibility::Collapsed) is left out with everything in it, since none of it is on screen.
/// While `node` has a popup open, the popup's content comes last, unless that content names a
/// closer parent through automation_parent_of(), as a submenu does with its item.
///
/// The returned pointers are non-owning and valid until the tree changes. Each call walks the
/// tree again.
///
/// Example:
/// @code
/// // The row only lays the buttons out, so it is marked decorative.
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::Text{.text = "Unsaved changes"},
///         ui::HorizontalStack{
///             .accessibility = {.decorative = true},
///             .content       = {ui::Button{.text = "Save"}, ui::Button{.text = "Discard"}},
///         },
///     },
/// });
///
/// // What a screen reader finds under the form: the text and both buttons, the row spliced out.
/// for (ui::Element* child : ui::automation_children(*window.content()))
/// {
///     std::println("{}", child->automation_name()); // Unsaved changes, Save, Discard
/// }
/// @endcode
[[nodiscard]] std::vector<Element*> automation_children(const Element& node);

/// What an accessibility client can do with `node`: the provider for each pattern it supports
/// now, null for the others (Element::automation_providers()). Screen readers reach a control
/// through these, and a test can drive one the same way.
///
/// Example:
/// @code
/// ui::CheckBoxControl check_box{"Remember me"};
/// if (ui::ToggleProvider* toggle = ui::automation_providers(check_box).toggle)
/// {
///     toggle->toggle_next(); // as a screen reader's Toggle does
/// }
/// @endcode
[[nodiscard]] AutomationProviders automation_providers(const Element& node);

/// The nearest ancestor that automation_children() would not skip, or nullptr at the root.
/// It is the inverse of automation_children(): `automation_parent(*child)` is the node whose
/// automation_children() lists `child`.
///
/// Example:
/// @code
/// // Continuing the automation_children() example: with Save or Discard focused, the node above
/// // it is the form, because the decorative row between them is skipped.
/// if (ui::Element* focused = window.focused())
/// {
///     ui::Element* group = ui::automation_parent(*focused); // window.content()
/// }
/// @endcode
[[nodiscard]] Element* automation_parent(const Element& node);

/// The node a client sees as focused while `focused` has keyboard focus. That is the item the
/// control names inside itself through Element::automation_focus(), followed as far as it goes
/// (for a menu bar, the item in its open menu), or else `focused` itself. Returns nullptr for
/// nullptr.
[[nodiscard]] Element* automation_focus(Element* focused);

/// A role's name as describe_automation() prints it: "Button", "TreeItem".
[[nodiscard]] const char* automation_role_name(AutomationRole role);

/// A property of a node that changes as it is used, for AutomationEvents::property_changed().
enum class AutomationProperty
{
    Name,                ///< automation_name().
    ToggleState,         ///< The Toggle pattern's state.
    ExpandCollapseState, ///< Whether the ExpandCollapse pattern says expanded.
    IsSelected,          ///< Whether the SelectionItem pattern says selected.
    Value,               ///< The Value pattern's text.
    RangeValue,          ///< The RangeValue pattern's number.
};

/// A property's value; std::monostate when the node has no such pattern.
using AutomationValue = std::variant<std::monostate, bool, double, std::string, ToggleState>;

/// The value of `property` on `node` now.
[[nodiscard]] AutomationValue automation_value(const Element& node, AutomationProperty property);

/// Receives what changed in a Window, for accessibility clients. The platform passes these
/// changes on to screen readers, and a test can record them (ui::testing::seams(window)).
///
/// The window reports after each layout, which follows every input it handles, in this order:
/// 1. menus that opened or closed, and the nodes whose children changed with them
/// 2. the node automation_focus() names: a new one as focused, or the same one's changed properties
/// 3. a change among the items around it (added, removed or renamed)
class AutomationEvents
{
public:
    virtual ~AutomationEvents() = default;
    /// Whether a client is listening; while false the window skips the checks.
    [[nodiscard]] virtual bool listening() const = 0;
    /// `node` took keyboard focus as automation sees it.
    virtual void focus_changed(Element& node) = 0;
    /// `property` of `node`, the node with automation focus, went from `before` to `after`.
    virtual void property_changed(Element&               node,
                                  AutomationProperty     property,
                                  const AutomationValue& before,
                                  const AutomationValue& after) = 0;
    /// A menu (a node with AutomationRole::Menu) opened.
    virtual void menu_opened(Element& menu) = 0;
    /// A menu closed.
    virtual void menu_closed(Element& menu) = 0;
    /// `node`'s automation children changed: one came, went or was renamed.
    virtual void structure_changed(Element& node) = 0;
};

/// A text dump of the accessibility tree rooted at `root`, the same on every machine and
/// resolution. It has one line for `root` and one for each node automation_children() reports
/// below it, indented two spaces per level. A line holds the role, the quoted name if it is not
/// empty, `#` and the Accessibility::automation_id if one is set, and a bracketed list of the
/// supported patterns if there are any. Every line ends in `\n`.
///
/// It does for accessibility what describe() does for rendering, and makes a stable check of
/// what a screen reader sees in a test, with no window or accessibility client needed.
///
/// Example:
/// @code
/// // Print what a screen reader sees of a window, e.g. while giving a custom control its role.
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::Button{.text = "Save"},
///         ui::CheckBox{.text = "Remember me"},
///     },
/// });
/// std::print("{}", ui::describe_automation(*window.content()));
/// // Group
/// //   Button "Save" [Invoke]
/// //   CheckBox "Remember me" [Toggle=Off]
/// @endcode
[[nodiscard]] std::string describe_automation(const Element& root);

} // namespace ui
