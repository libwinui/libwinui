// SPDX-License-Identifier: MIT
#include "winui/ui/text/PasswordBox.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ui
{

namespace
{
// The reveal button: a 30 DIP column, inset by 0,4,4,4, the reveal glyph at 12px.
constexpr float     RevealButtonWidth  = 30.0f;
constexpr Thickness RevealButtonMargin = Edges{.top = 4.0f, .right = 4.0f, .bottom = 4.0f};
constexpr IconGlyph RevealGlyph{0xF78D, "Segoe Fluent Icons", 12.0f};
} // namespace

PasswordBoxControl::PasswordBoxControl() : box_(std::make_unique<TextBoxControl>())
{
    set_parent_of(*box_, this);
    box_->set_password_char(password_char_);
    box_->set_delete_button_enabled(false); // the reveal button is the only button
    box_->set_spell_check(false);
    box_->on_text_changed([this](const std::string& text) { edited(text); });
}

PasswordBoxControl::~PasswordBoxControl() = default;

void PasswordBoxControl::set_password(std::string password)
{
    box_->set_text(std::move(password));
    if (box_->text().empty())
    {
        can_show_reveal_ = false;
    }
    apply_reveal();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void PasswordBoxControl::set_password_char(char32_t mask)
{
    password_char_ = mask;
    apply_reveal();
}

void PasswordBoxControl::set_reveal_mode(PasswordBoxRevealMode mode)
{
    reveal_mode_ = mode;
    apply_reveal();
}

void PasswordBoxControl::enabled_changed()
{
    box_->set_enabled(is_enabled());
    if (!is_enabled())
    {
        reveal_pressed_ = false;
        reveal_hovered_ = false;
    }
    apply_reveal();
}

bool PasswordBoxControl::reveal_button_visible() const
{
    return reveal_mode_ == PasswordBoxRevealMode::Peek && can_show_reveal_ && is_focused() && box_->is_enabled() &&
           box_->bounds().width > font_em_ * 5.0f;
}

Rect PasswordBoxControl::reveal_button_rect() const
{
    if (!reveal_button_visible())
    {
        return {};
    }
    const Rect& b = box_->bounds();
    return Rect{b.right() - RevealButtonWidth, b.top(), RevealButtonWidth, b.height};
}

void PasswordBoxControl::apply_reveal()
{
    if (!reveal_button_visible())
    {
        reveal_pressed_ = false;
        reveal_hovered_ = false;
    }
    revealed_ = reveal_mode_ == PasswordBoxRevealMode::Visible ||
                (reveal_mode_ == PasswordBoxRevealMode::Peek && reveal_pressed_);
    box_->set_password_char(revealed_ ? std::nullopt : std::optional<char32_t>{password_char_});
    box_->set_trailing_space(reveal_button_visible() ? RevealButtonWidth : 0.0f);
    mark_dirty(DirtyFlags::Visual);
}

void PasswordBoxControl::edited(const std::string& text)
{
    // The reveal button shows only for a password typed
    // from empty, and goes away when the field is cleared.
    if (!can_show_reveal_ && !text.empty())
    {
        can_show_reveal_ = was_empty_;
    }
    if (text.empty())
    {
        can_show_reveal_ = false;
    }
    was_empty_ = text.empty();
    apply_reveal();
    if (on_password_changed_)
    {
        on_password_changed_(text);
    }
    was_empty_ = box_->text().empty(); // the handler may have replaced it
}

void PasswordBoxControl::focus_changed(bool focused)
{
    set_focused_of(*box_, focused);
    if (focused)
    {
        // A password already there when focus arrives is never peekable.
        can_show_reveal_ = false;
        was_empty_       = box_->text().empty();
    }
    apply_reveal();
}

Size PasswordBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    font_em_ = context.theme.text_style.font_size;
    return box_->measure(available, context);
}

void PasswordBoxControl::arrange_override(const Rect&, const ArrangeContext&)
{
    box_->arrange(bounds());
    box_->set_trailing_space(reveal_button_visible() ? RevealButtonWidth : 0.0f);
}

void PasswordBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    box_->render(renderer, context);
    if (!reveal_button_visible())
    {
        return;
    }
    // The same fill and glyph colors as the TextBox's delete button.
    const ButtonState state  = reveal_pressed_   ? ButtonState::Pressed
                               : reveal_hovered_ ? ButtonState::Hover
                                                 : ButtonState::Normal;
    const Rect        button = reveal_button_rect().inset(RevealButtonMargin);
    const Color       fill   = theme.subtle_for_state(state).background;
    if (fill.a > 0.0f)
    {
        renderer.fill_rounded_rect(button, theme.corner_radius, fill);
    }
    const Color glyph = theme.colors().field_text.with_alpha(reveal_pressed_ ? 0.45f : 0.6f);
    draw_icon(renderer, RevealGlyph, button, glyph, 0.0f, theme.icon_font_family);
}

bool PasswordBoxControl::pointer_override(const PointerEvent& e)
{
    if (!box_->is_enabled())
    {
        return false;
    }
    const bool on_button = reveal_button_visible() && reveal_button_rect().contains(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (on_button != reveal_hovered_)
            {
                reveal_hovered_ = on_button;
                mark_dirty(DirtyFlags::Visual);
            }
            if (reveal_pressed_ && !on_button)
            {
                // A held button stops being pressed once the pointer leaves it, and the password
                // hides with it.
                reveal_pressed_ = false;
                apply_reveal();
            }
            if (reveal_pressed_)
            {
                return false;
            }
            break;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && on_button)
            {
                reveal_pressed_ = true;
                apply_reveal();
                return false;
            }
            break;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && reveal_pressed_)
            {
                reveal_pressed_ = false;
                apply_reveal();
                return false;
            }
            break;
        case PointerAction::Leave:
            reveal_hovered_ = false;
            if (reveal_pressed_)
            {
                reveal_pressed_ = false;
                apply_reveal();
            }
            break;
        case PointerAction::Wheel:
            break;
    }
    return on_button && (e.action == PointerAction::Down || e.action == PointerAction::Up);
}

void PasswordBoxControl::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    out.push_back(box_.get());
}

bool PasswordBoxControl::covers_children_at(Point p) const
{
    // The reveal button sits inside the text box.
    return reveal_button_visible() && reveal_button_rect().contains(p);
}

bool PasswordBoxControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (is_focused())
    {
        box_->handle_key(e);
    }
    return handled;
}

bool PasswordBoxControl::text_override(const TextInputEvent& text_event)
{
    const std::string_view text = text_event.text;
    if (is_focused())
    {
        box_->handle_text(text);
    }
    return true;
}

bool PasswordBoxControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    return box_->handle_context_menu(position, host);
}

void PasswordBoxControl::clear_dirty_override(DirtyFlags flags)
{
    clear_dirty_of(*box_, flags);
}

void PasswordBoxControl::build_visual_tree_override(VisualTree& tree) const
{
    box_->build_visual_tree(tree, visual_id());
}

void PasswordBoxControl::tick_override(Duration delta)
{
    box_->tick(delta);
}

bool PasswordBoxControl::set_text_value(std::string_view value)
{
    if (!box_->is_enabled())
    {
        return false;
    }
    set_password(std::string(value));
    edited(box_->text());
    return true;
}

std::unique_ptr<ui::Element> PasswordBox::build() &&
{
    auto node = std::make_unique<ui::PasswordBoxControl>();
    apply(header, *node, [raw = node.get()](const std::string& value) { raw->set_header(value); });
    apply(description, *node, [raw = node.get()](const std::string& value) { raw->set_description(value); });
    apply(error, *node, [raw = node.get()](const std::string& value) { raw->set_error(value); });
    apply(password, *node, [raw = node.get()](const std::string& value) { raw->set_password(value); });
    apply(placeholder_text, *node, [raw = node.get()](const std::string& value) { raw->set_placeholder_text(value); });
    apply(password_char, *node, [raw = node.get()](const char32_t& v) { raw->set_password_char(v); });
    apply(reveal_mode, *node, [raw = node.get()](const ui::PasswordBoxRevealMode& value) { raw->set_reveal_mode(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(max_length, *node, [raw = node.get()](const std::size_t& value) { raw->set_max_length(value); });

    if (auto changed = ui::write_back(password, std::move(on_password_changed)))
    {
        node->on_password_changed(std::move(changed));
    }
    if (on_submit)
    {
        node->on_submit(std::move(on_submit));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
