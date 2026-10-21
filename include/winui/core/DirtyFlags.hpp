// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace ui
{

/// Bitmask of what about an Element is out of date and must be redone at the next layout pass.
///
/// Setters mark flags on their element and the bits bubble to every ancestor, so a host can
/// ask the root whether anything below it changed. On the element itself `Measure` and `Arrange`
/// also say its own last measure or arrange is out of date. On an ancestor they only say
/// something below needs it. Combine with `|`, test with `&` or any(), clear with `&= ~flags`.
/// Read back through Element::is_dirty() / Element::dirty_flags(); Window::layout() clears them.
///
/// Example:
/// @code
/// // In a custom control, each setter marks what its change invalidates.
/// class TagControl : public ui::Element
/// {
/// public:
///     void set_label(std::string label)
///     {
///         if (label_ != label)
///         {
///             label_ = std::move(label);
///             mark_dirty(ui::DirtyFlags::Layout | ui::DirtyFlags::Visual); // new size and new look
///         }
///     }
///
///     void set_highlighted(bool highlighted)
///     {
///         if (highlighted_ != highlighted)
///         {
///             highlighted_ = highlighted;
///             mark_dirty(ui::DirtyFlags::Visual); // same size and place, only a redraw
///         }
///     }
///
/// private:
///     std::string label_;
///     bool        highlighted_ = false;
/// };
///
/// // Outside the element: has anything below the panel changed size since the last layout pass?
/// bool needs_measure(const ui::Element& panel)
/// {
///     return panel.is_dirty(ui::DirtyFlags::Measure); // measured again at the next layout
/// }
/// @endcode
enum class DirtyFlags : std::uint8_t
{
    None    = 0,       ///< Up to date.
    Measure = 1u << 0, ///< The desired size must be recomputed, and so the arrangement too.
    Visual  = 1u << 1, ///< Appearance changed (color, focus ring, scroll offset, ...); size and place unaffected.
    Arrange = 1u << 2, ///< The place of the element or its children changed, its desired size did not.

    Layout = Measure | Arrange,          ///< Measure and arrange: what a size-affecting setter marks.
    All    = Measure | Visual | Arrange, ///< Every flag; the default mask of the queries.
};

/// Union of two flag sets.
[[nodiscard]] constexpr DirtyFlags operator|(DirtyFlags a, DirtyFlags b)
{
    return static_cast<DirtyFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// Intersection of two flag sets; pass the result to any() to test a bit.
[[nodiscard]] constexpr DirtyFlags operator&(DirtyFlags a, DirtyFlags b)
{
    return static_cast<DirtyFlags>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

/// Complement within DirtyFlags::All, so `~Layout` never carries undefined high bits.
[[nodiscard]] constexpr DirtyFlags operator~(DirtyFlags a)
{
    return static_cast<DirtyFlags>(~static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(DirtyFlags::All));
}

/// Adds the flags in `b` to `a`.
constexpr DirtyFlags& operator|=(DirtyFlags& a, DirtyFlags b)
{
    return a = a | b;
}

/// Keeps only the flags of `a` that are also in `b`; `a &= ~f` clears `f`.
constexpr DirtyFlags& operator&=(DirtyFlags& a, DirtyFlags b)
{
    return a = a & b;
}

/// True if at least one flag is set.
[[nodiscard]] constexpr bool any(DirtyFlags flags)
{
    return flags != DirtyFlags::None;
}

} // namespace ui
