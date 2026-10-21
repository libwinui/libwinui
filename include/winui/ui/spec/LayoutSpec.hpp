// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/layouts/Layout.hpp"

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace ui
{

/// Anything with an rvalue-qualified build() producing an owned Layout: what a `.layout` field
/// takes. The four built-in layout values satisfy it, and so does an app's own.
template <typename L>
concept LayoutLike = requires(L&& layout) {
    { std::forward<L>(layout).build() } -> std::convertible_to<std::unique_ptr<Layout>>;
};

/// How generated children are arranged: the type of every `.layout` field.
///
/// A layout is written as a plain value, `.layout = ui::TileLayout{.min_row_spacing = 8}`, with no
/// `make_unique` and no setters. A name ending in `Layout` is such a value and never a control:
/// there is no `ui::Control<ui::TileLayout>`. An empty LayoutSpec leaves the field's default in
/// place, which for ui::ForEach is a vertical stack.
///
/// Which to reach for: hand-written children pick a layout control (`ui::VerticalStack`,
/// `ui::Grid`, `ui::WrapPanel`); children generated from a collection pick a `.layout`.
///
/// A layout of your own is a class: derive from ui::VirtualizingLayout or
/// ui::NonVirtualizingLayout and hand a `std::make_unique<MyLayout>()` to the same field.
///
/// Example:
/// @code
/// struct Photo
/// {
///     std::string title;
/// };
/// ui::ObservableVector<Photo> photos;
///
/// window.set_content(ui::ForEach<Photo>{
///     .items  = photos,
///     .layout = ui::TileLayout{.min_item_width = 120.0f, .min_row_spacing = 8.0f, .min_column_spacing = 8.0f},
///     .item   = [](const Photo& p) -> ui::Spec { return ui::Text{.text = p.title}; },
/// });
/// @endcode
class LayoutSpec
{
public:
    /// No layout: the field's own default applies.
    LayoutSpec() = default;

    /// A layout value: built the moment it becomes a LayoutSpec.
    template <LayoutLike L>
        requires(!std::same_as<std::remove_cvref_t<L>, LayoutSpec>)
    LayoutSpec(L&& layout) : layout_(std::forward<L>(layout).build()) // NOLINT(google-explicit-constructor)
    {
    }

    /// A layout object the app made itself, for a layout class of its own or one it keeps a
    /// pointer to and changes later. Takes ownership.
    template <typename T>
        requires std::derived_from<T, Layout>
    LayoutSpec(std::unique_ptr<T> layout) : layout_(std::move(layout)) // NOLINT(google-explicit-constructor)
    {
    }

    /// Move-only: it owns the layout it will hand over.
    LayoutSpec(const LayoutSpec&)            = delete;
    LayoutSpec& operator=(const LayoutSpec&) = delete;
    LayoutSpec(LayoutSpec&&)                 = default;
    LayoutSpec& operator=(LayoutSpec&&)      = default;

    /// Moves the layout out; null if none was given.
    [[nodiscard]] std::unique_ptr<Layout> take()
    {
        return std::move(layout_);
    }

    /// True while a layout is held.
    [[nodiscard]] explicit operator bool() const
    {
        return layout_ != nullptr;
    }

private:
    std::unique_ptr<Layout> layout_;
};

} // namespace ui
