// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Version.hpp"

#include <string>
#include <string_view>

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace ui
{

/// A font family name kept inline, so whatever holds one owns its copy: a name taken from a
/// `std::string` stays valid after the string is gone. A literal, a `std::string_view` or a
/// `std::string` converts to it, and it is usable in `constexpr` tables (`ui::glyphs::`). Up to
/// `Capacity` bytes of UTF-8; a longer name asserts and is cut.
///
/// Example:
/// @code
/// ui::IconGlyph star_icon(const std::string& family) // a family name read from the settings
/// {
///     return ui::IconGlyph{0xE734, family}; // copies the name
/// }
/// @endcode
class FontName
{
public:
    /// The longest name it holds, in bytes.
    static constexpr std::size_t Capacity = 63;

    /// The empty name.
    constexpr FontName() noexcept = default;
    /// From a NUL-terminated name; null is the empty name.
    constexpr FontName(const char* name) noexcept :
        FontName(std::string_view{name != nullptr ? name : ""}) // NOLINT(google-explicit-constructor)
    {
    }
    /// From any run of characters.
    constexpr FontName(std::string_view name) noexcept // NOLINT(google-explicit-constructor)
    {
        assert(name.size() <= Capacity && "font family name too long for ui::FontName");
        size_ = static_cast<std::uint8_t>(name.size() < Capacity ? name.size() : Capacity);
        for (std::size_t i = 0; i < size_; ++i)
        {
            data_[i] = name[i];
        }
    }
    /// From a string, copied.
    FontName(const std::string& name) noexcept : FontName(std::string_view{name}) // NOLINT(google-explicit-constructor)
    {
    }

    /// The name, NUL-terminated.
    [[nodiscard]] constexpr const char* c_str() const noexcept
    {
        return data_;
    }
    /// The name.
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return {data_, size_};
    }
    /// The name as a string_view.
    constexpr operator std::string_view() const noexcept // NOLINT(google-explicit-constructor)
    {
        return view();
    }
    /// True for the empty name.
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return size_ == 0;
    }
    /// The name's length in bytes.
    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return size_;
    }
    /// The name's bytes, NUL-terminated.
    [[nodiscard]] constexpr const char* data() const noexcept
    {
        return data_;
    }
    /// The name as a std::string.
    [[nodiscard]] std::string str() const
    {
        return std::string{view()};
    }
    /// By content.
    friend constexpr bool operator==(const FontName& a, const FontName& b) noexcept
    {
        return a.view() == b.view();
    }

private:
    char         data_[Capacity + 1]{};
    std::uint8_t size_ = 0;
};

/// Where a text run sits inside the box it is drawn into, per axis (TextStyle::horizontal and
/// TextStyle::vertical). Leading/Trailing are left/right horizontally and top/bottom vertically.
enum class TextAlign
{
    Leading,  ///< Left edge horizontally, top edge vertically.
    Center,   ///< Centered on the axis (TextStyle's default for both axes).
    Trailing, ///< Right edge horizontally, bottom edge vertically.
};

/// The semantic role a run of text plays on a page: what `TextControl::set_role()` (and
/// `ui::Text::role`) takes and `Theme::TypeRamp::for_role()` resolves against the active theme's
/// fonts. `Body` is the default, so a plain, role-less `TextControl` reads as body text.
///
/// Example:
/// @code
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Text{.text = "Settings", .role = ui::TextRole::Title},
///         ui::Text{.text = "Changes apply immediately.", .role = ui::TextRole::Caption},
///     },
/// });
/// @endcode
enum class TextRole
{
    Body,      ///< Running body text; the default.
    Header,    ///< Largest display rung (a page or hub header).
    Subheader, ///< Section header one rung below Header.
    Title,     ///< Page or dialog title.
    Subtitle,  ///< Group heading below a Title.
    Caption,   ///< Small secondary text (hints, timestamps).
    /// Body text at the ramp's SemiBold, for a label that heads the
    /// content below it. An era that sets no rung of its own gets its body style in SemiBold.
    BodyStrong,
};

/// Which extra rendering pass a TextEffect applies. The classic disabled label is `Emboss`:
/// the string is drawn twice, once in the highlight color offset by (1,1) and then in the
/// shadow color at the origin, for an engraved look. `None` (the default) keeps a single plain
/// pass.
enum class TextEffectKind
{
    None,   ///< One plain pass in the run's own color.
    Emboss, ///< Highlight pass at the offset, then the shadow pass on the origin.
};

/// A multi-pass text effect carried by TextStyle::effect; the classic themes use it for the
/// engraved disabled-button label. The run's own color is ignored while an effect is active:
/// the renderer draws `highlight` at (`offset_x`, `offset_y`) first, then `shadow` on the origin.
/// Measuring is unaffected.
///
/// Example:
/// @code
/// ui::TextStyle style;
/// style.effect = ui::TextEffect{ui::TextEffectKind::Emboss, ui::colors::White,
///                                  ui::Color::from_argb(0xFF808080), 1.0f, 1.0f};
/// @endcode
struct TextEffect
{
    /// Which effect to apply; None disables the extra pass entirely.
    TextEffectKind kind = TextEffectKind::None;
    /// Color of the offset raised pass, the classic 3D highlight color.
    Color highlight = colors::White;
    /// Color of the base pass on the origin, the classic 3D shadow color.
    Color shadow = colors::Black;
    /// Horizontal offset of the highlight pass, in DIPs.
    float offset_x = 1.0f;
    /// Vertical offset of the highlight pass, in DIPs.
    float offset_y = 1.0f;

    /// Memberwise equality.
    friend bool operator==(const TextEffect&, const TextEffect&) = default;
};

/// Whether a run breaks onto further lines when it meets the width it is laid out in. `Wrap`
/// breaks between words, and inside a word too long for a line on its own. `WrapWholeWords`
/// never splits a word and lets one that doesn't fit overflow instead. Explicit line breaks
/// (`\n`) start a new line in either mode.
enum class TextWrapping
{
    NoWrap,         ///< Everything on one line; the default.
    Wrap,           ///< Break between words, and inside a word too long for a line.
    WrapWholeWords, ///< Break only between words; an overlong word overflows.
};

/// How a text input control such as TextBox cases what the user types or pastes. Text already
/// in the control and text set from code keep their case.
enum class CharacterCasing
{
    Normal, ///< Keep the case as typed.
    Lower,  ///< Lower-case typed and pasted text.
    Upper,  ///< Upper-case typed and pasted text.
};

/// Font weight classes, with their OpenType values. Metro's type hierarchy is carried almost
/// entirely by weight, on a ramp a plain `bool bold` can't express: Header and Subheader are
/// Light, Body is SemiLight, Title is SemiBold and Caption is Regular.
enum class FontWeight : std::uint16_t
{
    Light     = 300, ///< Light.
    SemiLight = 350, ///< Semilight.
    Regular   = 400, ///< Regular; the default.
    SemiBold  = 600, ///< Semibold.
    Bold      = 700, ///< Bold.
};

/// Everything a Renderer and TextMeasurer need to know about how a run of text looks: font,
/// size, weight, alignment inside its box, wrapping, decorations and spacing.
///
/// A plain aggregate, filled in with designated initializers. Controls normally take their style
/// from the theme's type ramp (see TextRole); passing one explicitly (`TextControl::set_text_style()`,
/// `ui::Text::style`) pins it. Sizes are in DIPs.
///
/// Example:
/// @code
/// const ui::TextStyle heading{
///     .font_size   = 20.0f,
///     .horizontal  = ui::TextAlign::Leading,
///     .font_family = "Segoe UI Variable",
///     .weight      = ui::FontWeight::SemiBold,
///     .wrapping    = ui::TextWrapping::WrapWholeWords,
/// };
///
/// auto label = std::make_unique<ui::TextControl>("Account");
/// label->set_text_style(heading); // wins over the theme's role ramp from now on
///
/// // Or in a spec:
/// ui::Text{.text = "Account", .text_style = heading};
/// @endcode
struct TextStyle
{
    /// Font size in DIPs (not points). Default 14.
    float font_size = 14.0f;
    /// Horizontal placement inside the drawing box; Center by default.
    TextAlign horizontal = TextAlign::Center;
    /// Vertical placement inside the drawing box; Center by default.
    TextAlign vertical = TextAlign::Center;
    /// Font family name, looked up among the installed fonts.
    FontName font_family = "Segoe UI";

    /// Font weight.
    FontWeight weight = FontWeight::Regular;
    /// Italic face.
    bool italic = false;

    /// Puts every line in a line box of this height in DIPs, whatever the font's natural metrics,
    /// as the Metro theme does (20 DIPs for body text, 40 for the 56px header). 0, the default,
    /// uses the font's natural line height.
    float line_height = 0.0f;

    /// Defaults to no effect (a single plain pass); see TextEffect.
    TextEffect effect{};

    /// NoWrap (the default) keeps every run on one line, whatever box it is drawn into. A wrapping
    /// style is measured with TextMeasurer::measure_wrapped() against the width it will be drawn
    /// in, and a renderer breaks its lines at that box's width.
    TextWrapping wrapping = TextWrapping::NoWrap;

    /// A line under the run, at the font's own underline position and thickness. Never changes
    /// the measured size.
    bool underline = false;
    /// A line through the run, likewise.
    bool strikethrough = false;

    /// Extra room after every character, in thousandths of an em
    /// (200 at 24px adds 4.8 DIPs a character). Negative values tighten. It is part of the
    /// run's advance, so it widens the measured size, the last character's included.
    int character_spacing = 0;

    /// `character_spacing` converted to DIPs per character at this style's `font_size`.
    [[nodiscard]] float character_spacing_dips() const
    {
        return static_cast<float>(character_spacing) * font_size / 1000.0f;
    }

    /// Memberwise equality.
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

/// What a TextMeasurer reports for a run of text: its extent, where its first baseline sits and
/// how many lines it took. Converts to a Size (the extent), so code that only needs the box
/// reads `.width` and `.height` or assigns it to a Size.
///
/// Example:
/// @code
/// const ui::TextStyle   style{.font_size = 14.0f};
/// const ui::TextMetrics metrics = measurer.measure("Hello", style);
/// const ui::Size        box     = metrics; // width x height
/// const float           ascent  = metrics.baseline;
/// @endcode
struct TextMetrics
{
    /// The widest line's width, in DIPs.
    float width = 0.0f;
    /// The height of all lines together, in DIPs.
    float height = 0.0f;
    /// From the top of the box to the first line's baseline, in DIPs.
    float baseline = 0.0f;
    /// How many lines the text took; 1 for a single line, empty text included.
    int line_count = 1;

    /// The extent, width x height.
    [[nodiscard]] Size size() const
    {
        return {width, height};
    }
    operator Size() const // NOLINT(google-explicit-constructor): the extent is what most callers want
    {
        return size();
    }

    friend bool operator==(const TextMetrics&, const TextMetrics&) = default;
};

/// Measures text. The on-screen implementation measures with the real fonts, and the headless one
/// (HeadlessTextMeasurer) uses a deterministic approximation. Because measurement sits behind this
/// interface, layout runs without a window: Element::measure() takes a measurer and passes it down
/// the tree.
///
/// Call it through MeasureContext::measurer. The library implements it.
///
/// Parallel layout (ParallelLayout) calls one measurer from several threads at once, so an
/// implementation keeps no unsynchronized state: both shipped ones keep none at all.
///
/// Example:
/// @code
/// // An element is handed the measurer when it is measured, and never keeps it:
/// class Caption : public ui::Element
/// {
/// protected:
///     ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
///     {
///         const ui::TextStyle style{.font_size = 14.0f, .wrapping = ui::TextWrapping::Wrap};
///         // Wrapped to a bounded width; an unbounded one (ui::Unbounded) keeps it on one line.
///         return context.measurer.measure_wrapped(text_, style, available.width);
///     }
///
/// private:
///     std::string text_;
/// };
/// @endcode
class TextMeasurer
{
public:
    virtual ~TextMeasurer() = default;
    /// One line, whatever `style.wrapping` says: the natural, unbroken extent of the run, in DIPs.
    /// `text` is UTF-8.
    [[nodiscard]] virtual TextMetrics measure(std::string_view text, const TextStyle& style) const = 0;

    /// The run broken into lines no wider than `max_width` per `style.wrapping`: the widest
    /// line's width and the height of all lines together. A width of 0 or less, an infinite one
    /// (ui::Unbounded), or a NoWrap style, is measure(). The default ignores wrapping
    /// altogether; both shipped measurers override it.
    [[nodiscard]] virtual TextMetrics measure_wrapped(std::string_view text, const TextStyle& style, float /*max_width*/) const
    {
        return measure(text, style);
    }
};

} // namespace ui
