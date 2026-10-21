// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace ui
{

// ---------------------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------------------
/// A named, composable bundle of layout defaults: the margin, the six sizes and the two
/// alignments every spec carries. `.style = app_styles::Card` gives every card the same room, and
/// works on every spec.
///
/// Composition is compile-time: `styles::Spaced | styles::Centered` is a constant expression,
/// with the right-hand side winning wherever it sets a field. Unset fields (std::nullopt)
/// pass through, so two partial styles combine. A field set on the spec itself wins over the
/// style's.
///
/// Typography is not part of a style: a ui::Text takes `.role` (a step of the theme's type ramp)
/// or `.text_style`, and a control with a foreground takes `.foreground`.
///
/// Example:
/// @code
/// namespace app_styles
/// {
/// inline constexpr ui::Style Card = ui::styles::Spaced | ui::Style{.min_width = 200.0f};
/// }
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::Text{.text = "Saved 2 minutes ago", .role = ui::TextRole::Caption, .style = app_styles::Card},
///     ui::Button{.text = "Open", .style = app_styles::Card},
///     // An explicit field beats the style's: 16 DIP margin, the minimum width kept.
///     ui::Button{.text = "Close", .style = app_styles::Card, .margin = ui::Thickness{16.0f}},
/// }});
/// @endcode
struct Style
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Generic layout properties, mirroring what Element applies for every control. A set
    /// field fills in a spec field left unset (see ui::apply_common()).
    std::optional<Thickness> margin{};
    OptionalFloat            width{};                ///< Width, DIPs.
    OptionalFloat            height{};               ///< Height, DIPs.
    OptionalFloat            min_width{};            ///< Min width, DIPs.
    OptionalFloat            min_height{};           ///< Min height, DIPs.
    OptionalFloat            max_width{};            ///< Max width, DIPs.
    OptionalFloat            max_height{};           ///< Max height, DIPs.
    std::optional<Alignment> horizontal_alignment{}; ///< Horizontal alignment.
    std::optional<Alignment> vertical_alignment{};   ///< Vertical alignment.

    /// Merges `other` into this style: `other` wins wherever it sets a field; unset fields
    /// pass through. Returns `*this`.
    constexpr Style& operator|=(const Style& other)
    {
        if (other.margin)
        {
            margin = other.margin;
        }
        if (other.width)
        {
            width = other.width;
        }
        if (other.height)
        {
            height = other.height;
        }
        if (other.max_width)
        {
            max_width = other.max_width;
        }
        if (other.max_height)
        {
            max_height = other.max_height;
        }
        if (other.min_width)
        {
            min_width = other.min_width;
        }
        if (other.min_height)
        {
            min_height = other.min_height;
        }
        if (other.horizontal_alignment)
        {
            horizontal_alignment = other.horizontal_alignment;
        }
        if (other.vertical_alignment)
        {
            vertical_alignment = other.vertical_alignment;
        }
        return *this;
    }
};

/// Composes two styles into a new one; `rhs` wins wherever it sets a field. Usable in constant
/// expressions: `constexpr Style s = styles::Spaced | styles::Leading;`.
[[nodiscard]] constexpr Style operator|(Style lhs, const Style& rhs)
{
    lhs |= rhs;
    return lhs;
}

/// The starter set, each entry setting one aspect so that any two compose.
namespace styles
{
inline constexpr Style Compact{.margin = Thickness{2.0f}}; ///< 2 DIP margin all round.
inline constexpr Style Spaced{.margin = Thickness{8.0f}};  ///< 8 DIP margin all round.
/// Centered on both axes in the slot.
inline constexpr Style Centered{.horizontal_alignment = Alignment::Center, .vertical_alignment = Alignment::Center};
inline constexpr Style Leading{.horizontal_alignment = Alignment::Leading};   ///< Left-aligned in the slot.
inline constexpr Style Trailing{.horizontal_alignment = Alignment::Trailing}; ///< Right-aligned in the slot.
} // namespace styles

// ---------------------------------------------------------------------------------------
// The two helpers every build() calls
// ---------------------------------------------------------------------------------------

namespace detail
{
// Open while a bound value reaches a control: apply()'s sink and a bound collection's update. A
// spec's change handler, wrapped by write_back(), doesn't run while one is open, so it reports
// only what the user changed; the two-way property is still written.
class BoundUpdate
{
public:
    BoundUpdate() noexcept;
    ~BoundUpdate();
    BoundUpdate(const BoundUpdate&)            = delete;
    BoundUpdate& operator=(const BoundUpdate&) = delete;

    [[nodiscard]] static bool active() noexcept;
};
} // namespace detail

/// Seed a control property from a Binding and, if the binding is reactive, keep it in
/// sync for as long as the node lives.
///
/// Together with write_back(), shared_writer(), write_back_value(), apply_common(), adopt(),
/// ui::build() and SpecList, it is what the build() of a spec for your own control calls. These
/// helpers keep the same compatibility promise as Element.
///
/// `sink` is called once immediately with the binding's initial value, then again on every
/// change, through a Subscription the target Element owns. That subscription dies with the
/// node (see core/Property.hpp).
///
/// Example:
/// @code
/// // A piece of a spec's build() && (see apply_common() for the whole shape):
/// std::unique_ptr<ui::TextControl> build_label(ui::Binding<std::string> text)
/// {
///     auto node = std::make_unique<ui::TextControl>();
///     ui::apply(text, *node, [raw = node.get()](const std::string& v) { raw->set_text(v); });
///     return node;
/// }
/// @endcode
template <typename T, typename Fn>
    requires std::invocable<Fn&, const T&>
inline void apply(Binding<T>& binding, Element& target, Fn sink)
{
    {
        const detail::BoundUpdate bound;
        sink(binding.initial());
    }
    if (binding.reactive())
    {
        target.add_subscription(binding.subscribe(
            [sink = std::move(sink)](const T& value) mutable
            {
                const detail::BoundUpdate bound;
                sink(value);
            }));
    }
}

namespace detail
{
// `handler`, skipped while a bound value reaches the control (see BoundUpdate).
template <typename... Args>
[[nodiscard]] Callback<void(Args...)> user_changes_only(Callback<void(Args...)> handler)
{
    if (!handler)
    {
        return handler;
    }
    return [handler = std::move(handler)](Args... args) mutable
    {
        if (!BoundUpdate::active())
        {
            handler(std::forward<Args>(args)...);
        }
    };
}
} // namespace detail

/// The change handler of an editable field: writes the control's new value back through a
/// two-way binding, then runs the spec's own handler, unless the change came from a binding.
/// Returns an empty callback when there is nothing to do.
///
/// So a spec's `on_*_changed` reports changes the user made. A value written into the bound
/// property, or a change it causes, such as new items clearing a selection, updates the control
/// and a two-way property without running the handler.
///
/// The first argument of the handler is the new value. Call it in build() where the control's
/// `on_*_changed` is installed, after apply() has seeded the value:
///
/// @code
/// void bind_value(ui::SliderControl& node, ui::Binding<double>& value, ui::Callback<void(double)> on_value_changed)
/// {
///     ui::apply(value, node, [raw = &node](double v) { raw->set_value(v); });
///     if (auto changed = ui::write_back(value, std::move(on_value_changed)))
///     {
///         node.on_value_changed(std::move(changed));
///     }
/// }
/// @endcode
template <typename T, typename First, typename... Rest>
[[nodiscard]] inline Callback<void(First, Rest...)> write_back(Binding<T>& binding, Callback<void(First, Rest...)> handler)
{
    auto write = binding.take_writer();
    if (!write)
    {
        return detail::user_changes_only(std::move(handler));
    }
    return [write = std::move(write), handler = std::move(handler)](First first, Rest... rest) mutable
    {
        write(T(first));
        if (handler && !detail::BoundUpdate::active())
        {
            handler(std::forward<First>(first), std::forward<Rest>(rest)...);
        }
    };
}

/// The writer of a two-way binding, shared between the handlers of an open/closed field, or
/// null when the binding is one-way. See write_back_value().
template <typename T>
[[nodiscard]] inline std::shared_ptr<typename Binding<T>::Write> shared_writer(Binding<T>& binding)
{
    auto write = binding.take_writer();
    if (!write)
    {
        return nullptr;
    }
    return std::make_shared<typename Binding<T>::Write>(std::move(write));
}

/// As write_back(), with a writer already taken by shared_writer(), for a field whose apply()
/// sink also writes back what the control coerced the bound value to (a Slider's clamp), so the
/// property ends up equal to the control without running the spec's handler:
///
/// @code
/// void bind_value(ui::SliderControl& node, ui::Binding<double>& value, ui::Callback<void(double)> on_value_changed)
/// {
///     const auto write = ui::shared_writer(value);
///     ui::apply(value, node, [raw = &node, write](const double& v) {
///         raw->set_value(v);
///         if (write && raw->value() != v) { (*write)(raw->value()); }
///     });
///     if (auto changed = ui::write_back(write, std::move(on_value_changed)))
///     {
///         node.on_value_changed(std::move(changed));
///     }
/// }
/// @endcode
template <typename W, typename First, typename... Rest>
[[nodiscard]] inline Callback<void(First, Rest...)> write_back(const std::shared_ptr<W>&      writer,
                                                               Callback<void(First, Rest...)> handler)
{
    if (!writer)
    {
        return detail::user_changes_only(std::move(handler));
    }
    return [writer, handler = std::move(handler)](First first, Rest... rest) mutable
    {
        (*writer)(first);
        if (handler && !detail::BoundUpdate::active())
        {
            handler(std::forward<First>(first), std::forward<Rest>(rest)...);
        }
    };
}

/// As write_back(), for a field whose handler carries no value: an open/closed state, written as
/// `value` when the handler runs (`.is_open` with `on_opened` and `on_closed`). Returns `handler`
/// unchanged when `writer` is null.
///
/// @code
/// void bind_open(ui::PopupControl& node, ui::Binding<bool>& is_open, ui::Callback<void()> on_opened,
///                ui::Callback<void()> on_closed)
/// {
///     const auto open = ui::shared_writer(is_open);
///     if (auto opened = ui::write_back_value<bool>(open, true, std::move(on_opened)))
///     {
///         node.on_opened(std::move(opened));
///     }
///     if (auto closed = ui::write_back_value<bool>(open, false, std::move(on_closed)))
///     {
///         node.on_closed(std::move(closed));
///     }
/// }
/// @endcode
template <typename T, typename... Args>
[[nodiscard]] inline Callback<void(Args...)> write_back_value(const std::shared_ptr<typename Binding<T>::Write>& writer,
                                                              std::type_identity_t<T>                            value,
                                                              Callback<void(Args...)> handler)
{
    if (!writer)
    {
        return handler;
    }
    return [writer, value = std::move(value), handler = std::move(handler)](Args... args) mutable
    {
        (*writer)(value);
        if (handler)
        {
            handler(std::forward<Args>(args)...);
        }
    };
}

namespace detail
{
// A control's range from two bindings. Each change sets the pair as bound, not as the control
// keeps it (set_range() raises a max below the min), so a min moved past the max and back leaves
// the max where it was bound. Constants are the constructor's business and cost nothing here.
template <typename Control>
void apply_range(Binding<double>& min, Binding<double>& max, Control& node)
{
    if (!min.reactive() && !max.reactive())
    {
        return;
    }
    auto range = std::make_shared<std::pair<double, double>>(min.initial(), max.initial());
    apply(min,
          node,
          [raw = &node, range](const double& v)
          {
              range->first = v;
              raw->set_range(range->first, range->second);
          });
    apply(max,
          node,
          [raw = &node, range](const double& v)
          {
              range->second = v;
              raw->set_range(range->first, range->second);
          });
}

// apply_common()'s nine layout fields, out of line so that every spec's build() shares one copy.
void apply_layout(Element&                           target,
                  const Style&                       style,
                  Binding<std::optional<Thickness>>& margin,
                  Binding<std::optional<float>>&     width,
                  Binding<std::optional<float>>&     height,
                  Binding<std::optional<float>>&     min_width,
                  Binding<std::optional<float>>&     min_height,
                  Binding<std::optional<float>>&     max_width,
                  Binding<std::optional<float>>&     max_height,
                  Binding<std::optional<Alignment>>& horizontal_alignment,
                  Binding<std::optional<Alignment>>& vertical_alignment);
// apply_common()'s generic events; each one set replaces the element's handler.
void apply_events(Element&                                target,
                  Action                                  pointer_entered,
                  Action                                  pointer_exited,
                  Action                                  got_focus,
                  Action                                  lost_focus,
                  Callback<Handling(const KeyEvent&)>     key_down,
                  Callback<Handling(const PointerEvent&)> wheel);
} // namespace detail

/// Apply the presentation and layout fields every spec carries.
///
/// Duck-typed on field names, so a spec of your own control works if it declares the common
/// fields flat, with no shared base class.
///
/// The nine layout fields (margin, the six sizes, the two alignments) are bindable and
/// optional. Precedence: a bound field always wins, a constant that is set wins, and `.style`
/// fills in a field left unset. So `.style = styles::Spaced, .margin = Thickness{16}` gives 16,
/// `.style = styles::Spaced` alone gives 8, and `.width = size` follows `size` whatever the
/// style says. A set value means what it says: `.width = 0` is zero wide, and only an unset
/// width is Auto. A bound std::nullopt puts the control back to its own default (Auto, no
/// limit, no margin, Stretch). Moves `spec.accessibility` out when it is not empty.
///
/// `.visible` maps to Element::set_visibility(): false collapses the control, which then takes no
/// room in layout, draws nothing, gets no input and leaves the tab ring. Nothing is destroyed, so
/// a collapsed subtree keeps its state (typed text, scroll offsets, `.control` handles) and its
/// bindings stay live. A spec without the field is always visible.
///
/// A caller-defined spec gets the whole common-field vocabulary by declaring the same fields and
/// calling this from its build().
///
/// Example:
/// @code
/// struct Badge // a caller-defined spec
/// {
///     ui::Binding<std::string> text{};
///
///     ui::Style                                  style{};
///     ui::Binding<std::optional<ui::Thickness>> margin{};
///     ui::Binding<std::optional<float>>         width{}, height{}, min_width{}, min_height{};
///     ui::Binding<std::optional<float>>         max_width{}, max_height{};
///     ui::Binding<std::optional<ui::Alignment>> horizontal_alignment{}, vertical_alignment{};
///     ui::Binding<bool>                         visible{true};
///     ui::Accessibility                         accessibility{};
///
///     using control_type = ui::TextControl;
///     ui::ControlOut<Badge> control{};
///
///     [[nodiscard]] std::unique_ptr<ui::Element> build() &&
///     {
///         auto node = std::make_unique<ui::TextControl>();
///         ui::apply(text, *node, [raw = node.get()](const std::string& v) { raw->set_text(v); });
///         ui::apply_common(*this, *node);
///         ui::publish_control(control, node.get());
///         return node;
///     }
/// };
///
/// window.set_content(ui::VerticalStack{.content = {Badge{.text = "New", .style = ui::styles::Spaced}}});
/// @endcode
template <typename S>
inline void apply_common(S& spec, Element& target)
{
    detail::apply_layout(target,
                         spec.style,
                         spec.margin,
                         spec.width,
                         spec.height,
                         spec.min_width,
                         spec.min_height,
                         spec.max_width,
                         spec.max_height,
                         spec.horizontal_alignment,
                         spec.vertical_alignment);

    if constexpr (requires { spec.visible.reactive(); })
    {
        // The default, a constant true, is what an element already is.
        if (spec.visible.reactive() || !spec.visible.initial())
        {
            apply(spec.visible,
                  target,
                  [raw = &target](const bool& shown)
                  { raw->set_visibility(shown ? Visibility::Visible : Visibility::Collapsed); });
        }
    }

    if constexpr (requires { spec.tool_tip.reactive(); })
    {
        // Unset leaves the control's own tooltip alone, such as a command's description.
        if (spec.tool_tip.reactive() || !spec.tool_tip.initial().empty())
        {
            apply(spec.tool_tip, target, [raw = &target](const std::string& tip) { raw->set_tool_tip(tip); });
        }
    }

    if (!spec.accessibility.empty())
    {
        target.set_accessibility(std::move(spec.accessibility));
    }

    if constexpr (requires { spec.on_pointer_entered; })
    {
        detail::apply_events(target,
                             std::move(spec.on_pointer_entered),
                             std::move(spec.on_pointer_exited),
                             std::move(spec.on_got_focus),
                             std::move(spec.on_lost_focus),
                             std::move(spec.on_key_down),
                             std::move(spec.on_wheel));
    }
}

/// Adopt an owned child node into `parent`, wiring the parent link the same way every
/// container's own add() does. Only sets the parent pointer: `node` keeps ownership, so the
/// caller must store it in a member of `parent` that outlives the link. Returns the node as a
/// non-owning observer, or nullptr when `node` is empty.
inline Element* adopt(Element& parent, std::unique_ptr<Element>& node)
{
    if (!node)
    {
        return nullptr;
    }
    detail::TreeAccess::set_parent(*node, &parent);
    return node.get();
}

} // namespace ui
