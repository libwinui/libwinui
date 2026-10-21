// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"

#include <string>

namespace ui
{

/// The semantic name and description a control exposes to assistive technology
/// (`name` / `description`).
///
/// Every spec carries an `.accessibility` field and every Element stores one
/// (Element::accessibility() / Element::set_accessibility()). A control with a visible label
/// derives its accessible name from that label, so `ui::Button{.text = "Save"}` needs nothing.
/// Only content with no readable text of its own, such as an icon-only button, a progress ring or a
/// bare image, has to say what it is.
///
/// Example:
/// @code
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Button{.width         = 50.0f,
///                    .height        = 50.0f,
///                    .accessibility = {.name        = "Save document",
///                                      .description = "Saves the current document"},
///                    .on_click      = [] { /* save */ },
///                    .content       = ui::Icon{.source = ui::Symbol::Save}},
///         ui::ProgressRing{.active = true, .accessibility = {.name = "Saving"}},
///     },
/// });
///
/// // Imperative path: the same value on the retained control.
/// auto divider = std::make_unique<ui::TextControl>("|");
/// divider->set_accessibility({.decorative = true});
/// @endcode
struct Accessibility
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The short name announced for this control ("Save document"). Empty derives the name from
    /// the control's own visible text, which is right for almost every control.
    std::string name{};

    /// A longer explanation, announced after the name when a screen reader asks for it. Usually
    /// empty. Add one only when the name is not enough.
    std::string description{};

    /// True when this control tells a screen reader nothing its neighbors don't already say, such
    /// as a decorative icon beside a label that repeats it, a separator or a spacer. Screen readers
    /// then skip it, so it isn't announced twice.
    bool decorative = false;

    /// A fixed identifier that UI test tools find this control by ("save-button"). It stays the
    /// same from run to run and across languages, while the name changes with the text. Empty
    /// reports none. Screen readers don't read it.
    std::string automation_id{};

    /// True when nothing was specified, so the control falls back to its visible text.
    [[nodiscard]] bool empty() const
    {
        return name.empty() && description.empty() && !decorative && automation_id.empty();
    }

    /// Memberwise equality.
    friend bool operator==(const Accessibility&, const Accessibility&) = default;
};

} // namespace ui
