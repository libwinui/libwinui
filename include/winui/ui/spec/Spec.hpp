// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/Element.hpp"

#include <concepts>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// Anything with an rvalue-qualified build() producing an owned Element. Because it is a concept,
/// a mistake is reported at the caller's own line. Passing a non-spec into a child list gives
/// "no conversion from 'int' to 'Spec'", not a template backtrace.
///
/// An app's own spec type satisfies it by providing `std::unique_ptr<ui::Element> build() &&`,
/// and then composes anywhere a built-in spec does.
///
/// Example:
/// @code
/// struct LabeledField // a caller-defined spec
/// {
///     std::string label;
///     ui::Property<std::string>* value = nullptr;
///
///     [[nodiscard]] std::unique_ptr<ui::Element> build() &&
///     {
///         return ui::build(ui::VerticalStack{.spacing = 4, .content = {
///             ui::Text{.text = label},
///             ui::TextBox{.text = ui::bind(*value)},
///         }});
///     }
/// };
/// static_assert(ui::SpecLike<LabeledField>);
///
/// ui::Property<std::string> name;
/// window.set_content(ui::VerticalStack{.content = {LabeledField{"Name", &name}}});
/// @endcode
template <typename S>
concept SpecLike = requires(S&& spec) {
    { std::forward<S>(spec).build() } -> std::convertible_to<std::unique_ptr<Element>>;
};

/// Any piece of UI: the one type-erased, move-only description value.
///
/// A Spec holds the already-built node of whatever spec (or hand-built `unique_ptr<Element>`) it
/// was made from. It is what a component function returns, what a slot parameter takes, the type
/// of every single-content field (`.content`, `.pane`, `.anchor`, ...) and of each entry in a
/// `.content` list, so the same value composes in all four places. It is consumed once. A
/// default-constructed Spec is empty: a component that decided to show nothing, or a content
/// field left unset. Containers skip an empty one.
///
/// Converts implicitly from any SpecLike value and from a `std::unique_ptr` to an Element
/// subclass, which is what lets a braced list mix specs and hand-built controls.
///
/// A braced list of slots (Grid cells, Canvas items, tabs) is a `std::initializer_list`, which
/// only hands out const references. take() is const so the node can still be moved out of each
/// slot. Each list drains its entries inside the full-expression that created it, so nothing
/// reads a Spec after its node was moved out.
///
/// Handlers captured in a spec returned from a function must capture by value, or capture a
/// reference to state that outlives the window; taking `Property<T>&` parameters makes that
/// the easy thing.
///
/// Example:
/// @code
/// ui::Spec user_row(ui::Property<std::string>& name, std::string email)
/// {
///     return ui::HorizontalStack{
///         .spacing  = 8,
///         .content  = {
///             ui::Icon{ .source = ui::glyphs::Contact, .accessibility = { .decorative = true } },
///             ui::VerticalStack{ .content = {
///                 ui::Text{ .text = name },
///                 ui::Text{ .text = std::move(email), .role = ui::TextRole::Caption },
///             }},
///         },
///     };
/// }
///
/// ui::Property<std::string> alice{"Alice"}, bob{"Bob"};
/// auto note = std::make_unique<ui::TextControl>("Hand-built"); // a control made without a spec
///
/// window.set_content(ui::VerticalStack{.content = {
///     user_row(alice, "alice@example.com"), // a component's result
///     user_row(bob, "bob@example.com"),
///     ui::Text{.text = "Sales"},            // a spec: built as the list is formed
///     std::move(note),                      // an existing subtree: adopted unchanged
/// }});
///
/// bool show_footer = true;
/// ui::Spec footer = show_footer ? ui::Spec{ui::Text{.text = "Footer"}} : ui::Spec{};
/// @endcode
class Spec
{
public:
    /// An empty Spec: no content. Building it yields a null node, which every container skips.
    Spec() = default;

    /// A spec, built the moment it becomes a Spec. Spec itself is excluded, so this never takes
    /// the place of Spec's move constructor.
    template <SpecLike S>
        requires(!std::same_as<std::remove_cvref_t<S>, Spec>)
    Spec(S&& spec) : node_(std::forward<S>(spec).build()) // NOLINT(google-explicit-constructor)
    {
    }

    /// An already-built subtree, placed into a declared tree unchanged. This is how a control
    /// without a spec, or a subtree assembled in a loop, combines with declared UI. Takes
    /// ownership; no copy of the node is made.
    template <typename T>
        requires std::derived_from<T, Element>
    Spec(std::unique_ptr<T> node) : node_(std::move(node)) // NOLINT(google-explicit-constructor)
    {
    }

    /// Move-only: a Spec owns the node it will hand over.
    Spec(const Spec&)            = delete;
    Spec& operator=(const Spec&) = delete;
    Spec(Spec&&)                 = default;
    Spec& operator=(Spec&&)      = default;

    /// Satisfies SpecLike, so a Spec composes anywhere a spec does. Leaves it empty.
    [[nodiscard]] std::unique_ptr<Element> build() &&
    {
        return std::move(node_);
    }

    /// Move the built node out. Const because initializer_list only ever hands out const
    /// references; see the class comment for why that is sound. Calling it twice yields a null
    /// second time, which SpecList never does.
    [[nodiscard]] std::unique_ptr<Element> take() const
    {
        return std::move(node_);
    }

    /// True while a node is held: not default-constructed and not yet built or take()n.
    [[nodiscard]] explicit operator bool() const
    {
        return node_ != nullptr;
    }

private:
    mutable std::unique_ptr<Element> node_;
};

/// A built, owned list of elements. Move-only: it owns the built elements from the moment it is
/// constructed.
///
/// The type of a container's `.content` field. Written as a braced list in the common case, or
/// as a single spec without braces when there is only one; build one up in a loop with
/// push_back(), then move it into the field.
///
/// Example:
/// @code
/// std::vector<std::string> names{"Ada", "Grace", "Linus"};
/// ui::SpecList rows;
/// for (const auto& name : names)
/// {
///     rows.push_back(ui::CheckBox{.text = name});
/// }
///
/// window.set_content(ui::VerticalStack{.spacing = 4, .content = std::move(rows)});
///
/// // One element needs no braces.
/// ui::VerticalStack{.spacing = 4, .content = ui::Text{.text = "Just this"}};
/// @endcode
class SpecList
{
public:
    /// An empty list.
    SpecList() = default;

    /// A braced list of two or more: builds each entry (a spec or a unique_ptr) into `nodes`, in
    /// order. An empty Spec is skipped: it stands for no element.
    template <typename... S>
        requires(sizeof...(S) > 1 && (std::constructible_from<Spec, S &&> && ...))
    SpecList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        // Each entry is its own argument and becomes a Spec only here. A
        // `std::initializer_list<Spec>` would convert the entries while the braced list is matched,
        // and MSVC redoes that work for every list the list sits in, so a few levels of nesting
        // cost gigabytes of compiler memory.
        nodes.reserve(sizeof...(S));
        (append(Spec{std::forward<S>(items)}.take()), ...);
    }

    /// A list of one: a single spec, without braces. An empty ui::Spec gives an empty list.
    template <SpecLike S>
    SpecList(S&& spec) // NOLINT(google-explicit-constructor)
    {
        if (auto node = std::forward<S>(spec).build())
        {
            nodes.push_back(std::move(node));
        }
    }

    /// A list of one already-built subtree; a null pointer gives an empty list.
    template <typename T>
        requires std::derived_from<T, Element>
    SpecList(std::unique_ptr<T> node) // NOLINT(google-explicit-constructor)
    {
        if (node != nullptr)
        {
            nodes.push_back(std::move(node));
        }
    }

    SpecList(const SpecList&)            = delete;
    SpecList& operator=(const SpecList&) = delete;
    SpecList(SpecList&&)                 = default;
    SpecList& operator=(SpecList&&)      = default;

    /// True when the list holds no elements.
    [[nodiscard]] bool empty() const
    {
        return nodes.empty();
    }
    /// The number of elements held.
    [[nodiscard]] std::size_t size() const
    {
        return nodes.size();
    }

    /// Build `spec` and append it, as one more entry of a braced list would be. An empty Spec
    /// is skipped.
    void push_back(Spec spec)
    {
        append(std::move(spec).build());
    }

    /// The built elements, in order. Public so a parent's build() can drain it.
    std::vector<std::unique_ptr<Element>> nodes;

private:
    void append(std::unique_ptr<Element> node)
    {
        if (node != nullptr)
        {
            nodes.push_back(std::move(node));
        }
    }
};

/// The variadic form of the brace list: builds each argument (a spec or a unique_ptr) into a
/// SpecList, skipping empty ones. Useful where a list is passed through a function and braces
/// would not deduce. To build a list in a loop, use SpecList::push_back().
///
/// Example:
/// @code
/// ui::SpecList actions = ui::content(ui::Button{.text = "OK"}, ui::Button{.text = "Cancel"});
///
/// window.set_content(ui::HorizontalStack{.spacing = 8, .content = std::move(actions)});
/// @endcode
template <typename... S>
    requires(std::constructible_from<Spec, S &&> && ...)
[[nodiscard]] inline SpecList content(S&&... specs)
{
    SpecList   list;
    const auto append = [&list](std::unique_ptr<Element> node)
    {
        if (node != nullptr)
        {
            list.nodes.push_back(std::move(node));
        }
    };
    list.nodes.reserve(sizeof...(S));
    (append(Spec{std::forward<S>(specs)}.take()), ...);
    return list;
}

/// A spec that is only there when `condition` holds: `spec` when it is true, an empty ui::Spec
/// (no element at all) when it is false.
///
/// A false condition skips the spec's own build(), but C++ evaluates the argument first: the
/// children of a container spec (its `.content` list) are already built by then, and are thrown
/// away again. Their `.control` handles end up empty. Pass a callable returning the spec instead
/// (the other overload) to build nothing at all when the condition is false.
///
/// The condition is read once, while the tree is built. For one that follows state, use the
/// `.visible` field every spec has, ui::Lazy, or ui::Match.
///
/// Example:
/// @code
/// bool is_admin = true;
/// window.set_content(ui::VerticalStack{.content = {
///     ui::Text{.text = "Report"},
///     ui::when(is_admin, ui::Button{.text = "Delete all"}),
/// }});
/// @endcode
template <typename S>
    requires std::constructible_from<Spec, S&&>
[[nodiscard]] inline Spec when(bool condition, S&& spec)
{
    return condition ? Spec{std::forward<S>(spec)} : Spec{};
}

/// The lazy form of ui::when(): `make` is called only when `condition` holds, so a false
/// condition builds nothing, children included. `make` returns a spec (or anything a ui::Spec is
/// made from).
///
/// Example:
/// @code
/// bool                    is_admin = true;
/// ui::Control<ui::Button> delete_all;
/// window.set_content(ui::VerticalStack{.content = {
///     ui::Text{.text = "Report"},
///     ui::when(is_admin, [&] {
///         return ui::HorizontalStack{.spacing = 8, .content = {
///             ui::Button{.text = "Delete all", .control = delete_all},
///             ui::Button{.text = "Export"},
///         }};
///     }),
/// }});
/// @endcode
template <typename F>
    requires std::invocable<F&&> && std::constructible_from<Spec, std::invoke_result_t<F&&>>
[[nodiscard]] inline Spec when(bool condition, F&& make)
{
    return condition ? Spec{std::forward<F>(make)()} : Spec{};
}

/// Builds a spec into an owned node, for handing a declared subtree to an imperative API that
/// takes a unique_ptr.
template <SpecLike S>
[[nodiscard]] inline std::unique_ptr<Element> build(S&& spec)
{
    return std::forward<S>(spec).build();
}

} // namespace ui
