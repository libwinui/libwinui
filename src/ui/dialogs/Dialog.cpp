// SPDX-License-Identifier: MIT
#include "winui/ui/dialogs/Dialog.hpp"

#include "../../render/Compositor.hpp"
#include "../Resumption.hpp"
#include "winui/core/Callback.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowRef.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <coroutine>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// Dialog metrics.
constexpr float DialogMinWidth     = 320.0f; // DialogMinWidth
constexpr float DialogMaxWidth     = 548.0f; // DialogMaxWidth
constexpr float DialogMinHeight    = 184.0f; // DialogMinHeight
constexpr float DialogMaxHeight    = 756.0f; // DialogMaxHeight
constexpr float DialogPadding      = 24.0f;  // DialogPadding, both areas
constexpr float DialogTitleGap     = 12.0f;  // DialogTitleMargin's bottom
constexpr float ButtonSpacing      = 8.0f;   // DialogButtonSpacing
constexpr float SeparatorThickness = 1.0f;   // DialogSeparatorThickness
} // namespace

DialogControl::DialogControl() :
    title_(std::make_unique<TextControl>()),
    body_(std::make_unique<StackControl>(Orientation::Vertical))
{
    set_parent_of(*title_, this);
    title_->set_role(TextRole::Subtitle); // 20px SemiBold, the dialog title's style
    title_->set_wrapping(TextWrapping::Wrap);
    set_parent_of(*body_, this);
}

void DialogControl::set_title(std::string title)
{
    title_->set_text(std::move(title));
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

std::unique_ptr<ButtonControl> DialogControl::make_button(std::string text, Callback<void()> on_click, DialogResult result)
{
    auto button = std::make_unique<ButtonControl>(std::move(text));
    set_parent_of(*button, this);
    button->on_click(
        // `mutable` because Callback (std::move_only_function) is not const-invocable.
        [this, cb = std::move(on_click), result]() mutable
        {
            if (cb)
            {
                cb();
            }
            close_with(result);
        });
    return button;
}

ButtonControl* DialogControl::set_primary_button(std::string text, Callback<void()> on_click)
{
    primary_ = make_button(std::move(text), std::move(on_click), DialogResult::Primary);
    update_button_styles();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return primary_.get();
}

ButtonControl* DialogControl::set_secondary_button(std::string text, Callback<void()> on_click)
{
    secondary_ = make_button(std::move(text), std::move(on_click), DialogResult::Secondary);
    update_button_styles();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return secondary_.get();
}

ButtonControl* DialogControl::set_close_button(std::string text, Callback<void()> on_click)
{
    close_ = make_button(std::move(text), std::move(on_click), DialogResult::Close);
    update_button_styles();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return close_.get();
}

void DialogControl::set_default_button(std::optional<DialogDefaultButton> button)
{
    default_button_ = button;
    update_button_styles();
}

DialogDefaultButton DialogControl::default_button() const
{
    if (default_button_)
    {
        return *default_button_;
    }
    return primary_ ? DialogDefaultButton::Primary : DialogDefaultButton::None;
}

ButtonControl* DialogControl::default_button_ptr() const
{
    switch (default_button())
    {
        case DialogDefaultButton::Primary:
            return primary_.get();
        case DialogDefaultButton::Secondary:
            return secondary_.get();
        case DialogDefaultButton::Close:
            return close_.get();
        case DialogDefaultButton::None:
            break;
    }
    return nullptr;
}

std::vector<ButtonControl*> DialogControl::visible_buttons() const
{
    std::vector<ButtonControl*> out;
    for (ButtonControl* b : {primary_.get(), secondary_.get(), close_.get()})
    {
        if (b != nullptr)
        {
            out.push_back(b);
        }
    }
    return out;
}

// The accent follows the default button until focus is
// on a command button, then stays on the default button only while that one has focus.
void DialogControl::update_button_styles()
{
    ButtonControl* const        def              = default_button_ptr();
    std::vector<ButtonControl*> buttons          = visible_buttons();
    const bool                  focus_in_command = std::any_of(buttons.begin(),
                                                               buttons.end(),
                                                               [](ButtonControl* b) { return b->focused(); });
    for (ButtonControl* b : buttons)
    {
        const bool          accent = def != nullptr && b == def && (!focus_in_command || b->focused());
        const ButtonVariant want   = accent ? ButtonVariant::Accent : ButtonVariant::Neutral;
        if (b->variant() != want)
        {
            b->set_variant(want);
        }
    }
}

void DialogControl::close_with(DialogResult result)
{
    if (closing_)
    {
        return; // a close already in its Closing handler
    }
    DialogClosingArgs args{.result = result};
    bool              canceled = false;
    if (on_closing_)
    {
        closing_ = true;
        canceled = detail::canceled(on_closing_(args));
        closing_ = false;
    }
    if (canceled)
    {
        return;
    }
    result_ = result;
    if (close_modal_)
    {
        close_modal_();
    }
}

void DialogControl::hide()
{
    close_with(DialogResult::None);
}

void DialogControl::dismiss()
{
    if (close_ && close_->is_enabled())
    {
        (void)detail::TreeAccess::automation_providers(*close_).invoke->invoke(); // its click, then a close with Close
        return;
    }
    close_with(DialogResult::Close);
}

bool DialogControl::key_override(const KeyEvent& e)
{
    if (e.key == Key::Escape && e.action == KeyAction::Down)
    {
        dismiss(); // the window hands the modal Escape before anything inside it
        return true;
    }
    if (e.key != Key::Enter || e.action != KeyAction::Down)
    {
        return false;
    }
    ButtonControl* const def = default_button_ptr();
    return def != nullptr && detail::TreeAccess::automation_providers(*def).invoke->invoke(); // false when disabled
}

Element* DialogControl::default_focus()
{
    std::vector<Element*> content;
    body_->collect_focusables(content);
    if (!content.empty())
    {
        return content.front();
    }
    ButtonControl* const def = default_button_ptr();
    return def != nullptr && def->is_enabled() ? def : nullptr;
}

Size DialogControl::measure_override(Size available, const MeasureContext& context) const
{
    const float border = context.theme.border_width;
    // A width or max width set on the dialog replaces its own 548 cap, so a wide layout fits;
    // measure() has already held the offer to it. The same goes for the height's 756.
    const bool  sized_w      = width().has_value() || std::isfinite(Element::max_width());
    const bool  sized_h      = height().has_value() || std::isfinite(Element::max_height());
    const float max_width    = sized_w ? available.width : std::min(DialogMaxWidth, available.width);
    const float min_width    = std::min(DialogMinWidth, max_width);
    const float max_height   = sized_h ? available.height : std::min(DialogMaxHeight, available.height);
    const float min_height   = std::min(DialogMinHeight, max_height);
    const float chrome_width = 2.0f * (border + DialogPadding);

    // The command area asks for as many equal columns as its state has (three with every
    // button, two otherwise, since a lone button still leaves the left half empty).
    const std::vector<ButtonControl*> buttons  = visible_buttons();
    float                             button_w = 0.0f;
    float                             button_h = 0.0f;
    for (ButtonControl* b : buttons)
    {
        const Size s = b->measure(Size{Unbounded, Unbounded}, context);
        button_w     = std::max(button_w, s.width);
        button_h     = std::max(button_h, s.height);
    }
    const float columns   = buttons.size() == 3 ? 3.0f : 2.0f;
    const float command_w = buttons.empty() ? 0.0f : columns * button_w + (columns - 1.0f) * ButtonSpacing;

    // Measured once at the widest the dialog may be to learn how wide it wants to be, then again
    // at the width it settles on, so a wrapped title or body breaks where arrange will put it.
    const bool  has_title  = !title_->text().empty();
    const float widest     = std::max(0.0f, max_width - chrome_width);
    const Size  title_max  = has_title ? title_->measure(Size{widest, Unbounded}, context) : Size{};
    const Size  body_max   = body_->measure(Size{widest, Unbounded}, context);
    const float width      = std::clamp(std::max({title_max.width, body_max.width, command_w}) + chrome_width,
                                        min_width,
                                        std::max(min_width, max_width));
    const float inner      = std::max(0.0f, width - chrome_width);
    const Size  title_size = has_title ? title_->measure(Size{inner, Unbounded}, context) : Size{};
    const Size  body_size  = body_->measure(Size{inner, Unbounded}, context);

    title_height_     = title_size.height;
    button_height_    = button_h;
    separator_height_ = context.theme.surfaces.dialog_separator ? SeparatorThickness : 0.0f;
    command_height_   = buttons.empty() ? 0.0f : 2.0f * DialogPadding + button_h;
    border_           = border;

    const float top_height = 2.0f * DialogPadding + (has_title ? title_size.height + DialogTitleGap : 0.0f) +
                             body_size.height + separator_height_;
    const float height = std::clamp(top_height + command_height_ + 2.0f * border, min_height, std::max(min_height, max_height));
    return Size{width, height};
}

void DialogControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const Rect inner = final_rect.inset(Thickness{border_});
    command_rect_ = command_height_ > 0.0f ? Rect{inner.x, inner.bottom() - command_height_, inner.width, command_height_}
                                           : Rect{};
    top_rect_ = Rect{inner.x, inner.y, inner.width, std::max(0.0f, inner.height - command_height_)};

    // The title and the content, in the top area's padding; the separator is the area's own
    // bottom border, so the padding sits above it.
    const Rect text{top_rect_.x + DialogPadding,
                    top_rect_.y + DialogPadding,
                    std::max(0.0f, top_rect_.width - 2.0f * DialogPadding),
                    std::max(0.0f, top_rect_.height - 2.0f * DialogPadding - separator_height_)};
    float      y = text.y;
    if (!title_->text().empty())
    {
        title_->arrange(Rect{text.x, y, text.width, title_height_});
        y += title_height_ + DialogTitleGap;
    }
    else
    {
        title_->arrange(Rect{text.x, y, text.width, 0.0f});
    }
    body_->arrange(Rect{text.x, y, text.width, std::max(0.0f, text.bottom() - y)});

    // The button row: equal columns with a spacer between each, three only when all three
    // buttons show.
    const std::vector<ButtonControl*> buttons = visible_buttons();
    if (buttons.empty())
    {
        return;
    }
    float       x0    = command_rect_.x + DialogPadding;
    const float by    = command_rect_.y + DialogPadding;
    float       width = std::max(0.0f, command_rect_.width - 2.0f * DialogPadding);
    // A dialog made wider than the standard keeps its buttons at the size they have there,
    // grouped at the right, rather than stretching them across the row.
    const float standard = std::max(0.0f, DialogMaxWidth - 2.0f * (border_ + DialogPadding));
    if (width > standard)
    {
        x0 += width - standard;
        width = standard;
    }
    const auto place = [this](ButtonControl* b, float left, float top, float w)
    { b->arrange(Rect{left, top, w, button_height_}); };
    if (buttons.size() == 3)
    {
        const float third = std::max(0.0f, (width - 2.0f * ButtonSpacing) / 3.0f);
        for (std::size_t i = 0; i < 3; ++i)
        {
            place(buttons[i], x0 + static_cast<float>(i) * (third + ButtonSpacing), by, third);
        }
        return;
    }
    const float half = std::max(0.0f, (width - ButtonSpacing) / 2.0f);
    if (buttons.size() == 2)
    {
        place(buttons[0], x0, by, half);
        place(buttons[1], x0 + half + ButtonSpacing, by, half);
        return;
    }
    place(buttons[0], x0 + half + ButtonSpacing, by, half); // a lone button takes the right half
}

void DialogControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme  = context.theme;
    const float  radius = theme.overlay_corner_radius;

    // `colors.background`, not the `surfaces.menu` overlay token: a dialog's background is the
    // solid base background color, fully opaque, unlike a Flyout.
    renderer.fill_rounded_rect(bounds(), radius, theme.colors().background);

    if (theme.surfaces.dialog_top_overlay && theme.surfaces.dialog_top_overlay->a > 0.0f)
    {
        // Rounded at the top only when the command area is below it: the rect is grown past
        // its bottom edge by a radius and clipped back, so its lower corners fall outside.
        const float inner_radius = std::max(0.0f, radius - border_);
        if (command_height_ > 0.0f)
        {
            renderer.push_clip(top_rect_);
            renderer.fill_rounded_rect(Rect{top_rect_.x, top_rect_.y, top_rect_.width, top_rect_.height + 2.0f * inner_radius},
                                       inner_radius,
                                       *theme.surfaces.dialog_top_overlay);
            renderer.pop_clip();
        }
        else
        {
            renderer.fill_rounded_rect(top_rect_, inner_radius, *theme.surfaces.dialog_top_overlay);
        }
    }
    if (theme.surfaces.dialog_separator && command_height_ > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{top_rect_.x, top_rect_.bottom() - SeparatorThickness, top_rect_.width, SeparatorThickness},
                                   0.0f,
                                   *theme.surfaces.dialog_separator);
    }

    if (!title_->text().empty())
    {
        title_->render(renderer, context);
    }
    body_->render(renderer, context);
    for (ButtonControl* b : visible_buttons())
    {
        b->render(renderer, context);
    }

    stroke_rounded_rect_inside(renderer,
                               bounds(),
                               radius,
                               theme.surfaces.surface_stroke.value_or(theme.surfaces.field_border),
                               theme.border_width);
}

void DialogControl::for_each_child(ChildVisitor visit) const
{
    visit(*title_);
    visit(*body_);
    for (ButtonControl* b : visible_buttons())
    {
        visit(*b);
    }
}

void DialogControl::collect_focusables(std::vector<Element*>& out)
{
    body_->collect_focusables(out);
    for (ButtonControl* b : visible_buttons())
    {
        b->collect_focusables(out);
    }
}

void DialogControl::tick_override(Duration delta)
{
    // Focus moves without telling the dialog (Window owns it), so the accent is re-derived here,
    // once per tick; set_style() only marks dirty when it actually changes.
    update_button_styles();
    if (open_ms_ < OpenTransitionMs)
    {
        open_ms_ = std::min(open_ms_ + animation_delta(delta).count(), OpenTransitionMs);
        mark_dirty(DirtyFlags::Visual); // keep Window::tick()'s dirty check true for the fade's duration
    }
}

void DialogControl::collect_live(std::vector<Element*>& out)
{
    detail::TreeAccess::collect_live(*body_, out);
}

std::unique_ptr<ui::Element> Dialog::build() &&
{
    auto node = std::make_unique<ui::DialogControl>();

    apply(title, *node, [raw = node.get()](const std::string& value) { raw->set_title(value); });

    if (content)
    {
        node->set_content(content.take());
    }

    // The control shows them Primary, Secondary, Close whatever the order here.
    for (auto [spec, set] : {std::pair{&primary, &ui::DialogControl::set_primary_button},
                             std::pair{&secondary, &ui::DialogControl::set_secondary_button},
                             std::pair{&close, &ui::DialogControl::set_close_button}})
    {
        if (*spec)
        {
            ui::ButtonControl* button = ((*node).*set)(std::move(spec->text), std::move(spec->on_click));
            apply(spec->enabled, *button, [button](const bool& value) { button->set_enabled(value); });
        }
    }
    apply(default_button,
          *node,
          [raw = node.get()](const std::optional<DialogDefaultButton>& v) { raw->set_default_button(v); });

    if (on_closing)
    {
        node->on_closing(std::move(on_closing));
    }
    if (on_closed)
    {
        node->on_closed(std::move(on_closed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

namespace
{
// The coroutine show_dialog() suspended, owned by the dialog's on_closed handler: the answer posts
// it to the window to resume; a dialog that goes unanswered destroys the handler and the frame.
struct DialogWait
{
    WindowRef          home;
    detail::Resumption resumption;
    DialogResult*      result;
};
} // namespace

void DialogAwaiter::await_suspend(std::coroutine_handle<> coroutine)
{
    auto wait       = std::make_unique<DialogWait>(window_->ref(), detail::Resumption{coroutine}, &result_);
    spec_.on_closed = [own = std::move(spec_.on_closed), wait = std::move(wait)](DialogResult result) mutable
    {
        if (own)
        {
            own(result);
        }
        *wait->result = result;
        // Through the window, so a closed one drops the coroutine rather than resuming it.
        (void)wait->home.post(std::move(wait->resumption));
    };
    // Nothing of *this is touched after show_modal(): a replaced or destroyed dialog destroys the
    // frame holding it.
    Window& window = *window_;
    window.show_modal(std::move(spec_));
}

} // namespace ui
