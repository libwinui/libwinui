// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ui
{

/// A circular avatar showing a photo, initials or a placeholder glyph, with an optional badge. It
/// takes no input and never receives focus.
///
/// What it shows, in priority order:
/// - the "People" glyph when `is_group()` is true, even over a set photo
/// - the `profile_picture()`, clipped to the circle
/// - `effective_initials()`
/// - the "Contact" glyph
///
/// The badge is an accent-filled circle in the top-right corner, at half the picture's size. It
/// shows a positive `badge_number()`, as "99+" past 99, or else the `badge_icon()`. Screen readers
/// hear the person's name followed by ", N items", ", icon" or `badge_text()`. The name is "Group",
/// the display name, the initials or "Person". `ui::PersonPicture` is the declarative form.
///
/// Example:
/// @code
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::PersonPicture{.display_name = "Jane Doe"},  // shows "JD"
///         ui::PersonPicture{.display_name = "Jane Doe", .badge_number = 5, .width = 72.0f},
///         ui::PersonPicture{.group        = true},              // the People glyph
///     },
/// });
///
/// // Imperatively:
/// auto avatar = std::make_unique<ui::PersonPictureControl>();
/// avatar->set_display_name("Alex Wilber");
/// avatar->set_diameter(48.0f);
/// avatar->set_badge_number(3);
/// avatar->set_badge_text("3 unread messages"); // spoken instead of ", 3 items"
/// @endcode
class PersonPictureControl final : public Element
{
public:
    /// A 96 DIP placeholder avatar (the "Contact" glyph) with no badge.
    PersonPictureControl() = default;

    /// The size when no width or height is set, in DIPs. 96 by default.
    [[nodiscard]] float diameter() const
    {
        return diameter_;
    }
    /// Sets the diameter, at least 1 DIP. Updates layout and redraws.
    void set_diameter(float diameter)
    {
        diameter_ = (std::max)(1.0f, diameter);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The size the picture is drawn at. It is `diameter()` until an explicit width or height is
    /// set, then that one, or the smaller when both are, so the picture stays round.
    [[nodiscard]] float effective_diameter() const;

    /// The person's name. The initials and the accessible name are derived from it.
    [[nodiscard]] const std::string& display_name() const
    {
        return display_name_;
    }
    /// Sets the display name. Redraws.
    void set_display_name(std::string name)
    {
        display_name_ = std::move(name);
        mark_dirty(DirtyFlags::Visual);
    }

    /// Explicit initials. When not empty, they replace the initials derived from display_name()
    /// (see `effective_initials()`).
    [[nodiscard]] const std::string& initials() const
    {
        return initials_;
    }
    /// Sets the explicit initials. Redraws.
    void set_initials(std::string initials)
    {
        initials_ = std::move(initials);
        mark_dirty(DirtyFlags::Visual);
    }

    /// Whether the picture shows the "People" glyph, even over a set profile picture.
    [[nodiscard]] bool is_group() const
    {
        return is_group_;
    }
    /// Turns the group glyph on or off. Redraws.
    void set_group(bool group)
    {
        is_group_ = group;
        mark_dirty(DirtyFlags::Visual);
    }

    /// A decoded photo, drawn clipped to the circle. An empty Bitmap, the default, falls back to
    /// the initials.
    [[nodiscard]] const Bitmap& profile_picture() const
    {
        return profile_picture_;
    }
    /// Sets the photo. Redraws.
    void set_profile_picture(Bitmap picture)
    {
        profile_picture_ = std::move(picture);
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- badge ----
    /// The badge number. 0 or less shows no number.
    [[nodiscard]] int badge_number() const
    {
        return badge_number_;
    }
    /// Sets the badge number. Measures and redraws, since the badge widens for "99+".
    void set_badge_number(int number)
    {
        badge_number_ = number;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The badge icon, shown while badge_number() isn't: a Symbol, a glyph, a path or a picture.
    /// It is drawn at 60% of the badge's size in the badge's text color, and a picture keeps its
    /// own colors. Empty shows no icon.
    [[nodiscard]] const IconSource& badge_icon() const
    {
        return badge_icon_;
    }
    /// Sets the badge icon. Redraws.
    void set_badge_icon(IconSource icon)
    {
        badge_icon_ = std::move(icon);
        mark_dirty(DirtyFlags::Visual);
    }
    /// What screen readers hear in place of ", N items" or ", icon".
    [[nodiscard]] const std::string& badge_text() const
    {
        return badge_text_;
    }
    /// Sets the badge text. It changes only the accessible name, so nothing is redrawn.
    void set_badge_text(std::string text)
    {
        badge_text_ = std::move(text);
    }

    /// The number the badge shows, "99+" past 99. Empty when it shows the icon or nothing.
    [[nodiscard]] std::string badge_content() const;
    /// Whether any badge shows.
    [[nodiscard]] bool has_badge() const
    {
        return badge_number_ > 0 || !badge_icon_.empty();
    }
    /// Where the badge is drawn for the current bounds: in the top-right corner, half the picture's
    /// size tall, moved 4 DIPs up and right. A circle, widened to the left into a pill when the
    /// number doesn't fit in one.
    [[nodiscard]] Rect badge_bounds() const;

    /// initials() if set, else initials_from_display_name(display_name()).
    [[nodiscard]] std::string effective_initials() const;

    /// The initials for `display_name`. For a name in Latin, Cyrillic or a similar script, these are
    /// the first characters of the first and last words, upper-cased. A single word gives one
    /// initial. A trailing (…), […] or {…} is dropped first, leading punctuation is skipped and
    /// combining marks stay with their letter. A name that starts in a script where initials mean
    /// nothing, such as CJK, Arabic, Indic or Thai, gives none.
    [[nodiscard]] static std::string initials_from_display_name(std::string_view display_name);

    // ---- automation ----
    /// Exposed to UI Automation as text.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Text;
    }
    /// "Group", else the display name, else the initials, else "Person", followed by ", N items",
    /// ", icon" or `badge_text()` for a badge.
    [[nodiscard]] std::string automation_name_fallback() const override;

protected:
    /// A square of `effective_diameter()`, clipped to `available` on a bounded axis.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws the circle with its photo, initials or glyph, then the badge.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    float       diameter_ = 96.0f; // the default width and height
    std::string display_name_;
    std::string initials_;
    bool        is_group_ = false;
    Bitmap      profile_picture_{};
    int         badge_number_ = 0;
    IconSource  badge_icon_{};
    std::string badge_text_;
    // The badge number's width per DIP of font size, from the last measure; 0 before one.
    mutable float badge_text_ratio_ = 0.0f;
};

/// A circular avatar with an optional number, glyph or picture badge. It shows a photo, initials
/// or a placeholder glyph, picked in the order ui::PersonPictureControl describes. Builds a
/// ui::PersonPictureControl.
///
/// The pictures can be bound like every other field. A Binding doesn't compare its values, only
/// a Property does, so bind a picture through a conversion from another property:
/// `bind(choice, [](int i) { ... })`.
///
/// Example:
/// @code
/// ui::Property<int> unread{3};
///
/// window.set_content(ui::HorizontalStack{.spacing = 12, .content = {
///     ui::PersonPicture{.display_name    = "Jane Doe"}, // shows "JD"
///     ui::PersonPicture{.diameter        = 48.0f,
///                       .display_name    = "Alex Wilber",
///                       .profile_picture = ui::Bitmap::read("alex.png"),
///                       .badge_number    = unread,
///                       .badge_text      = ui::bind(unread, [](int n) { return std::format("{} unread", n); })},
///     ui::PersonPicture{.group           = true},
/// }});
/// @endcode
struct PersonPicture
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<PersonPicture>` points at.
    using control_type = PersonPictureControl;

    // ---- properties ----
    /// The circle's diameter in DIPs, at least 1.
    Binding<float> diameter{96.0f};
    /// The person's name. Read by screen readers, and the source of the initials when `initials` is
    /// empty.
    Binding<std::string> display_name{};
    /// Initials to show in place of ones derived from `display_name`.
    Binding<std::string> initials{};
    /// Shows the "People" glyph, over every picture and initial.
    Binding<bool> group{false};
    /// The photo, clipped to the circle. Shown in place of the initials.
    Binding<Bitmap> profile_picture{};
    /// The badge number, shown when positive, as "99+" past 99.
    Binding<int> badge_number{0};
    /// The badge icon, shown when the number isn't: a Symbol, a glyph, a path or a picture.
    Binding<IconSource> badge_icon{};
    /// What screen readers hear in place of the default ", N items" or ", icon".
    Binding<std::string> badge_text{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<PersonPicture> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::PersonPictureControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
