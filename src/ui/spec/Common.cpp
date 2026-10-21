// SPDX-License-Identifier: MIT
#include "winui/ui/spec/Common.hpp"

namespace ui::detail
{
namespace
{
thread_local int bound_updates = 0;

// One layout field: a bound field always wins, then a set constant, then the style.
template <typename T, typename S, typename Set>
void apply_layout_field(Element& target, Binding<std::optional<T>>& field, const S& from_style, Set set)
{
    if (field.reactive())
    {
        apply(field, target, [raw = &target, set](const std::optional<T>& value) { set(*raw, value); });
    }
    else if (field.initial())
    {
        set(target, field.initial());
    }
    else if (from_style)
    {
        set(target, std::optional<T>(from_style));
    }
}
} // namespace

BoundUpdate::BoundUpdate() noexcept
{
    ++bound_updates;
}

BoundUpdate::~BoundUpdate()
{
    --bound_updates;
}

bool BoundUpdate::active() noexcept
{
    return bound_updates > 0;
}

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
                  Binding<std::optional<Alignment>>& vertical_alignment)
{
    apply_layout_field(target,
                       margin,
                       style.margin,
                       [](Element& e, const std::optional<Thickness>& v) { e.set_margin(v.value_or(Thickness{})); });
    apply_layout_field(target, width, style.width, [](Element& e, const std::optional<float>& v) { e.set_width(v); });
    apply_layout_field(target, height, style.height, [](Element& e, const std::optional<float>& v) { e.set_height(v); });
    apply_layout_field(target,
                       min_width,
                       style.min_width,
                       [](Element& e, const std::optional<float>& v) { e.set_min_width(v); });
    apply_layout_field(target,
                       min_height,
                       style.min_height,
                       [](Element& e, const std::optional<float>& v) { e.set_min_height(v); });
    apply_layout_field(target,
                       max_width,
                       style.max_width,
                       [](Element& e, const std::optional<float>& v) { e.set_max_width(v); });
    apply_layout_field(target,
                       max_height,
                       style.max_height,
                       [](Element& e, const std::optional<float>& v) { e.set_max_height(v); });
    apply_layout_field(target,
                       horizontal_alignment,
                       style.horizontal_alignment,
                       [](Element& e, const std::optional<Alignment>& v)
                       { e.set_horizontal_alignment(v.value_or(Alignment::Stretch)); });
    apply_layout_field(target,
                       vertical_alignment,
                       style.vertical_alignment,
                       [](Element& e, const std::optional<Alignment>& v)
                       { e.set_vertical_alignment(v.value_or(Alignment::Stretch)); });
}

void apply_events(Element&                                target,
                  Action                                  pointer_entered,
                  Action                                  pointer_exited,
                  Action                                  got_focus,
                  Action                                  lost_focus,
                  Callback<Handling(const KeyEvent&)>     key_down,
                  Callback<Handling(const PointerEvent&)> wheel)
{
    if (key_down)
    {
        target.on_key_down(std::move(key_down));
    }
    if (wheel)
    {
        target.on_wheel(std::move(wheel));
    }
    if (pointer_entered)
    {
        target.on_pointer_entered(std::move(pointer_entered));
    }
    if (pointer_exited)
    {
        target.on_pointer_exited(std::move(pointer_exited));
    }
    if (got_focus)
    {
        target.on_got_focus(std::move(got_focus));
    }
    if (lost_focus)
    {
        target.on_lost_focus(std::move(lost_focus));
    }
}
} // namespace ui::detail
