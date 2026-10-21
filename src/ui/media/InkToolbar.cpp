// SPDX-License-Identifier: MIT
#include "winui/ui/media/InkToolbar.hpp"

#include "../LocalText.hpp"
#include "../themes/ControlChrome.hpp"
#include "InkRender.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// The primary accent text color, which only Fluent's palette carries; the highlight color elsewhere,
// as in high contrast.
Color accent_text(const Theme& theme)
{
    return detail::has_fluent_palette(theme) ? theme.palette.accent_text_primary : theme.colors().selection;
}

// A tool button's width and height, its margin and the glyph sizes.
constexpr float InkButtonSize     = 36.0f;
constexpr float InkButtonMargin   = 2.0f;
constexpr float InkToolbarMargin  = 2.0f; // the margin around the tool row
constexpr float InkGlyphSize      = 16.0f;
constexpr float InkExtensionSize  = 10.0f;
constexpr float InkExtensionInset = 2.0f;

// The pen flyout: a 6-column grid of 52 DIP swatches, the
// 296 DIP width slider, 12 DIP side padding and the rows' 8/12 DIP gaps.
constexpr int   InkPaletteColumns    = 6;
constexpr float InkSwatchCell        = 52.0f;
constexpr float InkSwatchInset       = 8.0f;
constexpr float InkSwatchHoverInset  = 6.0f;
constexpr float InkSwatchChosenInset = 10.0f;
constexpr float InkSwatchRingInset   = 6.0f;
constexpr float InkSwatchPressInset  = 2.0f;
constexpr float InkSwatchRingWidth   = 2.0f;
constexpr float InkFlyoutMinWidth    = 320.0f;
constexpr float InkFlyoutSidePadding = 12.0f;
constexpr float InkSliderWidth       = 296.0f;
constexpr float InkSliderHeight      = 44.0f;
constexpr float InkPreviewMargin     = 10.0f; // around the sample stroke
constexpr float InkPreviewMinHeight  = 24.0f;

// The eraser flyout's item: 40 tall, at least 136 wide, a 2 DIP inset
// border, the glyph in a 12 DIP gutter each side.
constexpr float InkItemHeight       = 40.0f;
constexpr float InkItemMinWidth     = 136.0f;
constexpr float InkItemInset        = 2.0f;
constexpr float InkItemGlyphGutter  = 12.0f;
constexpr float InkItemAccentWidth  = 3.0f;
constexpr float InkItemAccentHeight = 16.0f;

constexpr IconGlyph InkPenGlyph{0xE76D};
constexpr IconGlyph InkPenFillGlyph{0xE88F};
constexpr IconGlyph InkPencilGlyph{0xED63};
constexpr IconGlyph InkPencilFillGlyph{0xF0C6};
constexpr IconGlyph InkHighlighterGlyph{0xE7E6};
constexpr IconGlyph InkHighlighterFillGlyph{0xE891};
constexpr IconGlyph InkEraserGlyph{0xED60};
constexpr IconGlyph InkEraseAllGlyph{0xE74D};
constexpr IconGlyph InkPointEraserGlyph{0xED61};
constexpr IconGlyph InkChevronDownGlyph{0xE96E};
constexpr IconGlyph InkChevronUpGlyph{0xE96D};
constexpr IconGlyph InkChevronLeftGlyph{0xE96F};
constexpr IconGlyph InkChevronRightGlyph{0xE970};
constexpr IconGlyph InkRulerGlyph{0xECC6};
constexpr IconGlyph InkProtractorGlyph{0xF0B4};

// The ballpoint pen's and pencil's palette, and the highlighter's.
const std::vector<Color>& ink_pen_palette()
{
    static const std::vector<Color> Palette = {
        Color::from_argb(0xFF000000), Color::from_argb(0xFFFFFFFF), Color::from_argb(0xFFD1D3D4),
        Color::from_argb(0xFFA7A9AC), Color::from_argb(0xFF808285), Color::from_argb(0xFF58595B),
        Color::from_argb(0xFFB31564), Color::from_argb(0xFFE61B1B), Color::from_argb(0xFFFF5500),
        Color::from_argb(0xFFFFAA00), Color::from_argb(0xFFFFCE00), Color::from_argb(0xFFFFE600),
        Color::from_argb(0xFFA2E61B), Color::from_argb(0xFF26E600), Color::from_argb(0xFF008055),
        Color::from_argb(0xFF00AACC), Color::from_argb(0xFF004DE6), Color::from_argb(0xFF3D00B8),
        Color::from_argb(0xFF6600CC), Color::from_argb(0xFF600080), Color::from_argb(0xFFF7D7C4),
        Color::from_argb(0xFFBB9167), Color::from_argb(0xFF8E562E), Color::from_argb(0xFF613D30),
        Color::from_argb(0xFFFF80FF), Color::from_argb(0xFFFFC680), Color::from_argb(0xFFFFFF80),
        Color::from_argb(0xFF80FF9E), Color::from_argb(0xFF80D6FF), Color::from_argb(0xFFBCB3FF),
    };
    return Palette;
}

const std::vector<Color>& ink_highlighter_palette()
{
    static const std::vector<Color> Palette = {
        Color::from_argb(0xFFFFE600), // yellow
        Color::from_argb(0xFF26E600), // green
        Color::from_argb(0xFF44C8F5), // light blue
        Color::from_argb(0xFFEC008C), // pink
        Color::from_argb(0xFFFF5500), // orange
        Color::from_argb(0xFF6600CC), // purple
    };
    return Palette;
}

const std::string& ink_tool_name(InkToolbarTool tool)
{
    const Strings::InkToolbarText& t = strings().ink_toolbar;
    switch (tool)
    {
        case InkToolbarTool::BallpointPen:
            return t.ballpoint_pen;
        case InkToolbarTool::Pencil:
            return t.pencil;
        case InkToolbarTool::Highlighter:
            return t.highlighter;
        case InkToolbarTool::CustomPen:
            return t.custom_pen;
        case InkToolbarTool::CustomTool:
            return t.custom_tool;
        case InkToolbarTool::Eraser:
            break;
    }
    return t.eraser;
}

// The background every ink toolbar button draws for its state, checked resting on the hover wash
// (the secondary subtle fill); returns the foreground.
Color draw_ink_button_background(Renderer& renderer, const Theme& theme, const Rect& bounds, ButtonState state, bool checked)
{
    const auto& v          = theme.subtle_for_state(state);
    const Color background = checked && state == ButtonState::Normal
                                 ? theme.subtle_for_state(ButtonState::Hover).background
                                 : v.background;
    renderer.fill_rounded_rect(bounds, theme.corner_radius, background);
    return v.text;
}

// The chevron toward the side a button's flyout opens on.
void draw_ink_extension(Renderer&                       renderer,
                        const Theme&                    theme,
                        const Rect&                     bounds,
                        InkToolbarButtonFlyoutPlacement placement,
                        Color                           color)
{
    const IconGlyph* chevron = &InkChevronDownGlyph;
    Rect             box{bounds.x, bounds.bottom() - InkExtensionSize, bounds.width, InkExtensionSize};
    switch (placement)
    {
        case InkToolbarButtonFlyoutPlacement::Top:
            chevron = &InkChevronUpGlyph;
            box     = Rect{bounds.x, bounds.y + InkExtensionInset, bounds.width, InkExtensionSize};
            break;
        case InkToolbarButtonFlyoutPlacement::Left:
            chevron = &InkChevronLeftGlyph;
            box     = Rect{bounds.x + InkExtensionInset, bounds.y, InkExtensionSize, bounds.height};
            break;
        case InkToolbarButtonFlyoutPlacement::Right:
            chevron = &InkChevronRightGlyph;
            box = Rect{bounds.right() - InkExtensionInset - InkExtensionSize, bounds.y, InkExtensionSize, bounds.height};
            break;
        case InkToolbarButtonFlyoutPlacement::Auto:
        case InkToolbarButtonFlyoutPlacement::Bottom:
            break;
    }
    draw_icon(renderer, *chevron, box, color, InkExtensionSize, theme.icon_font_family);
}

// A custom button's icon: a glyph in the theme's icon font while it keeps IconGlyph's default family,
// as the built-in glyphs are; a path or picture fitted into the glyph's square.
void draw_custom_glyph(Renderer& renderer, const Theme& theme, const IconSource& icon, const Rect& bounds, Color color)
{
    icon.draw_in_slot(renderer, bounds, color, InkGlyphSize, theme.icon_font_family);
}

ButtonState ink_button_state(bool enabled, bool pressed, bool hovered)
{
    if (!enabled)
    {
        return ButtonState::Disabled;
    }
    if (pressed)
    {
        return ButtonState::Pressed;
    }
    return hovered ? ButtonState::Hover : ButtonState::Normal;
}

// ---- the pen flyout's palette ----

class InkPaletteGrid final : public Element
{
public:
    explicit InkPaletteGrid(InkToolbarToolButtonControl& pen) : pen_(pen)
    {
    }

    [[nodiscard]] bool focusable() const override
    {
        return !pen_.palette().empty();
    }
    [[nodiscard]] bool takes_arrow_key(Key) const
    {
        return true;
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::List;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return strings().ink_toolbar.colors;
    }

    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        const auto count = static_cast<int>(pen_.palette().size());
        const int  rows  = (count + InkPaletteColumns - 1) / InkPaletteColumns;
        const int  cols  = std::min(count, InkPaletteColumns);
        return Size{static_cast<float>(cols) * InkSwatchCell, static_cast<float>(rows) * InkSwatchCell};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Color ring = context.theme.colors().text.with_alpha(1.0f); // the chosen item's border
        for (std::size_t i = 0; i < pen_.palette().size(); ++i)
        {
            const Rect  cell   = cell_rect(i);
            const bool  chosen = i == pen_.selected_brush_index();
            const bool  hover  = hover_ == static_cast<int>(i);
            const bool  press  = hover && pressed_;
            const float inset  = chosen ? InkSwatchChosenInset : (hover ? InkSwatchHoverInset : InkSwatchInset);
            const float radius = InkSwatchCell * 0.5f - inset;
            renderer.fill_ellipse(cell.center(), radius, radius, pen_.palette()[i]);
            if (chosen || press)
            {
                const float ring_inset = press ? InkSwatchPressInset : InkSwatchRingInset;
                const float r          = InkSwatchCell * 0.5f - ring_inset - InkSwatchRingWidth * 0.5f;
                renderer.stroke_ellipse(cell.center(), r, r, ring, InkSwatchRingWidth);
            }
        }
    }


private:
    [[nodiscard]] Rect cell_rect(std::size_t i) const
    {
        const auto col = static_cast<float>(i % InkPaletteColumns);
        const auto row = static_cast<float>(i / InkPaletteColumns);
        return Rect{bounds().x + col * InkSwatchCell, bounds().y + row * InkSwatchCell, InkSwatchCell, InkSwatchCell};
    }
    [[nodiscard]] int index_at(Point p) const
    {
        if (!bounds().contains(p))
        {
            return -1;
        }
        const int col   = static_cast<int>((p.x - bounds().x) / InkSwatchCell);
        const int row   = static_cast<int>((p.y - bounds().y) / InkSwatchCell);
        const int index = row * InkPaletteColumns + col;
        return col < InkPaletteColumns && index < static_cast<int>(pen_.palette().size()) ? index : -1;
    }
    void set_hover(int index)
    {
        if (hover_ != index)
        {
            hover_ = index;
            mark_dirty(DirtyFlags::Visual);
        }
    }

    InkToolbarToolButtonControl& pen_;
    int                          hover_   = -1;
    bool                         pressed_ = false;

protected:
    void focus_changed(bool /*focused*/) override
    {
        mark_dirty(DirtyFlags::Visual);
    }
    bool key_override(const KeyEvent& e) override
    {

        const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
        if (!is_focused())
        {
            return false; // a key bubbling up from inside: this control reads only its own
        }
        if (e.action != KeyAction::Down || pen_.palette().empty())
        {
            return handled;
        }
        const auto count = static_cast<int>(pen_.palette().size());
        int        index = static_cast<int>(pen_.selected_brush_index());
        switch (e.key)
        {
            case Key::ArrowLeft:
                index -= 1;
                break;
            case Key::ArrowRight:
                index += 1;
                break;
            case Key::ArrowUp:
                index -= InkPaletteColumns;
                break;
            case Key::ArrowDown:
                index += InkPaletteColumns;
                break;
            case Key::Home:
                index = 0;
                break;
            case Key::End:
                index = count - 1;
                break;
            default:
                return handled;
        }
        if (index >= 0 && index < count)
        {
            pen_.set_selected_brush_index(static_cast<std::size_t>(index));
            mark_dirty(DirtyFlags::Visual);
        }
        return handled;
    }

protected:
    /// Around the chosen color's cell, round.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return cell_rect(pen_.selected_brush_index());
    }
    [[nodiscard]] float focus_ring_corner_radius(const Theme& /*theme*/) const override
    {
        return InkSwatchCell * 0.5f;
    }
};

// ---- the pen flyout's sample stroke ----

class InkStrokePreview final : public Element
{
public:
    explicit InkStrokePreview(InkToolbarToolButtonControl& pen) : pen_(pen)
    {
    }

    // A gentle S, 248 by 24 DIPs, drawn with the pen's own attributes.
    static const std::vector<InkPoint>& sample()
    {
        static const std::vector<InkPoint> Points = {
            {.position = {0.0f, 22.5f}},
            {.position = {30.0f, 8.0f}},
            {.position = {70.0f, 0.5f}},
            {.position = {120.0f, 8.0f}},
            {.position = {175.0f, 24.0f}},
            {.position = {215.0f, 17.0f}},
            {.position = {248.0f, 1.5f}},
        };
        return Points;
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Image;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return strings().ink_toolbar.stroke_preview;
    }

    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        // Sized for the widest stroke, so the flyout does not jump while the slider moves.
        return Size{0.0f, std::max(InkPreviewMinHeight, 24.0f + pen_.max_stroke_width() + InkPreviewMargin)};
    }

    void render_override(Renderer& renderer, const RenderContext&) const override
    {
        const InkStroke                 stroke{.points = sample(), .attributes = pen_.drawing_attributes()};
        const std::vector<DrawGeometry> drawings = detail::ink_stroke_drawings(stroke);
        if (drawings.empty())
        {
            return;
        }
        const float dx = bounds().x + std::max(0.0f, (bounds().width - 248.0f) * 0.5f);
        const float dy = bounds().y + (bounds().height - 24.0f) * 0.5f;
        renderer.push_clip(bounds());
        renderer.push_transform(Matrix::translation(dx, dy));
        for (const DrawGeometry& d : drawings)
        {
            renderer.draw_geometry(d);
        }
        renderer.pop_transform();
        renderer.pop_clip();
    }


private:
    InkToolbarToolButtonControl& pen_;
};

// ---- the eraser flyout's "Erase all ink" ----

class InkFlyoutItem final : public Element, private InvokeProvider
{
public:
    // `label` names the field of strings().ink_toolbar it shows.
    InkFlyoutItem(IconGlyph glyph, std::string Strings::InkToolbarText::* label, Callback<void()> action) :
        glyph_(glyph),
        label_(label),
        action_(std::move(action))
    {
    }

    // The current one of a choice (the eraser kind) keeps the accent pill.
    void set_checked(bool checked)
    {
        if (checked_ != checked)
        {
            checked_ = checked;
            mark_dirty(DirtyFlags::Visual);
        }
    }

    [[nodiscard]] bool focusable() const override
    {
        return true;
    }
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::MenuItem;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return label();
    }

    [[nodiscard]] Size measure_override(Size, const MeasureContext& context) const override
    {
        const float text = context.measurer.measure(label(), context.theme.text_style).width;
        const float wide = InkItemInset * 2.0f + InkItemGlyphGutter * 2.0f + InkGlyphSize + text + InkItemGlyphGutter;
        return Size{std::max(InkItemMinWidth, wide), InkItemHeight};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme&        theme = context.theme;
        const ButtonVisual& v     = theme.subtle_for_state(ink_button_state(true, pressed_, hovered_));
        const Rect          face  = bounds().inset(Thickness{InkItemInset});
        renderer.fill_rounded_rect(face, theme.corner_radius, v.background);
        if (pressed_ || checked_)
        {
            renderer.fill_rounded_rect(Rect{face.x,
                                            face.y + (face.height - InkItemAccentHeight) * 0.5f,
                                            InkItemAccentWidth,
                                            InkItemAccentHeight},
                                       InkItemAccentWidth * 0.5f,
                                       accent_text(theme));
        }
        const Rect glyph{face.x + InkItemGlyphGutter, face.y, InkGlyphSize, face.height};
        draw_icon(renderer, glyph_, glyph, v.text, InkGlyphSize, theme.icon_font_family);
        TextStyle style  = theme.text_style;
        style.horizontal = TextAlign::Leading;
        const float left = glyph.right() + InkItemGlyphGutter;
        renderer.draw_text(label(), Rect{left, face.y, std::max(0.0f, face.right() - left), face.height}, v.text, style);
    }


private:
    void set_state(bool hovered, bool pressed)
    {
        if (hovered_ != hovered || pressed_ != pressed)
        {
            hovered_ = hovered;
            pressed_ = pressed;
            mark_dirty(DirtyFlags::Visual);
        }
    }

    IconGlyph                        glyph_;
    bool                             checked_ = false;
    [[nodiscard]] const std::string& label() const
    {
        return strings().ink_toolbar.*label_;
    }

    std::string Strings::InkToolbarText::* label_;
    Callback<void()>                       action_;
    bool                                   hovered_ = false;
    bool                                   pressed_ = false;

protected:
    void focus_changed(bool /*focused*/) override
    {
        mark_dirty(DirtyFlags::Visual);
    }
    bool key_override(const KeyEvent& e) override
    {

        const bool handled = key_handled(e, false, takes_enter_key(), false);
        if (!is_focused())
        {
            return false; // a key bubbling up from inside: this control reads only its own
        }
        if (e.action == KeyAction::Up && (e.key == Key::Enter || e.key == Key::Space) && action_)
        {
            action_();
        }
        return handled;
    }

protected:
    /// Around the item's face, inside its slot.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return bounds().inset(Thickness{InkItemInset});
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke = this;
    }
    bool invoke() override
    {
        if (action_)
        {
            action_();
        }
        return true;
    }
};

// `text` names the field of strings().ink_toolbar it shows.
std::unique_ptr<TextControl> ink_flyout_heading(std::string Strings::InkToolbarText::* text, Thickness margin)
{
    auto heading = std::make_unique<TextControl>();
    heading->set_margin(margin);
    detail::follow_strings(*heading, [raw = heading.get(), text] { raw->set_text(strings().ink_toolbar.*text); });
    return heading;
}
} // namespace

// ---- InkToolbarToolButtonControl ----

InkToolbarToolButtonControl::InkToolbarToolButtonControl(InkToolbarTool tool) : tool_(tool)
{
    set_margin(Thickness{InkButtonMargin});
    switch (tool_)
    {
        case InkToolbarTool::BallpointPen:
            palette_ = ink_pen_palette();
            break;
        case InkToolbarTool::Pencil:
            palette_        = ink_pen_palette();
            selected_width_ = 4.0f;
            break;
        case InkToolbarTool::Highlighter:
            palette_        = ink_highlighter_palette();
            min_width_      = 12.0f;
            max_width_      = 64.0f;
            selected_width_ = 16.0f;
            break;
        case InkToolbarTool::Eraser:
        case InkToolbarTool::CustomTool:
            break;
        case InkToolbarTool::CustomPen:
            palette_ = ink_pen_palette();
            break;
    }
}

InkToolbarToolButtonControl::InkToolbarToolButtonControl(InkToolbarTool tool, InkToolbarCustomButton custom) :
    InkToolbarToolButtonControl(tool)
{
    custom_ = std::move(custom);
}

InkToolbarToolButtonControl::~InkToolbarToolButtonControl() = default;

void InkToolbarToolButtonControl::set_palette(std::vector<Color> palette)
{
    palette_        = std::move(palette);
    selected_index_ = palette_.empty() ? 0 : std::min(selected_index_, palette_.size() - 1);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    attributes_changed();
}

void InkToolbarToolButtonControl::set_selected_brush_index(std::size_t index)
{
    const std::size_t clamped = palette_.empty() ? 0 : std::min(index, palette_.size() - 1);
    if (clamped == selected_index_)
    {
        return;
    }
    selected_index_ = clamped;
    mark_dirty(DirtyFlags::Visual);
    attributes_changed();
}

Color InkToolbarToolButtonControl::selected_color() const
{
    return palette_.empty() ? colors::Black : palette_[selected_index_];
}

void InkToolbarToolButtonControl::set_stroke_width_range(float min, float max)
{
    if (min > max)
    {
        std::swap(min, max);
    }
    min_width_ = std::max(0.0f, min);
    max_width_ = std::max(min_width_, max);
    mark_dirty(DirtyFlags::Visual);
    const float clamped = std::clamp(selected_width_, min_width_, max_width_);
    if (clamped != selected_width_)
    {
        selected_width_ = clamped;
        attributes_changed();
    }
}

void InkToolbarToolButtonControl::set_selected_stroke_width(float width)
{
    const float clamped = std::clamp(width, min_width_, max_width_);
    if (clamped == selected_width_)
    {
        return;
    }
    selected_width_ = clamped;
    mark_dirty(DirtyFlags::Visual);
    attributes_changed();
}

InkDrawingAttributes InkToolbarToolButtonControl::drawing_attributes() const
{
    const float w = selected_width_;
    switch (tool_)
    {
        case InkToolbarTool::Pencil:
        {
            InkDrawingAttributes a = InkDrawingAttributes::for_pencil();
            a.color                = selected_color();
            a.size                 = Size{w, w};
            return a;
        }
        case InkToolbarTool::Highlighter:
            return InkDrawingAttributes{.color               = selected_color(),
                                        .size                = Size{w / 3.0f, w},
                                        .pen_tip             = InkPenTip::Rectangle,
                                        .draw_as_highlighter = true};
        case InkToolbarTool::CustomPen:
            if (custom_ && custom_->pen)
            {
                return custom_->pen(selected_color(), w);
            }
            break;
        case InkToolbarTool::BallpointPen:
        case InkToolbarTool::Eraser:
        case InkToolbarTool::CustomTool:
            break;
    }
    return InkDrawingAttributes{.color = selected_color(), .size = Size{w, w}};
}

void InkToolbarToolButtonControl::set_clear_all_visible(bool visible)
{
    if (clear_all_visible_ == visible)
    {
        return;
    }
    clear_all_visible_ = visible;
    if (!visible && flyout_ != nullptr)
    {
        flyout_->hide();
    }
    mark_dirty(DirtyFlags::Visual);
}

bool InkToolbarToolButtonControl::extension_glyph_shown() const
{
    const bool has_flyout = flyout_ != nullptr;
    return checked_ && has_flyout && !flyout_->is_open();
}

void InkToolbarToolButtonControl::attributes_changed()
{
    if (owner_ != nullptr)
    {
        owner_->button_attributes_changed(*this);
    }
}

Size InkToolbarToolButtonControl::measure_override(Size, const MeasureContext&) const
{
    return Size{InkButtonSize, InkButtonSize};
}

void InkToolbarToolButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme   = context.theme;
    const bool   enabled = owner_ == nullptr || owner_->is_enabled();
    const auto   state   = ink_button_state(enabled, pressed_, hovered_);
    const Color  text    = draw_ink_button_background(renderer, theme, bounds(), state, checked_);

    const IconGlyph* outline = &InkEraserGlyph;
    const IconGlyph* fill    = nullptr;
    switch (tool_)
    {
        case InkToolbarTool::BallpointPen:
            outline = &InkPenGlyph;
            fill    = &InkPenFillGlyph;
            break;
        case InkToolbarTool::Pencil:
            outline = &InkPencilGlyph;
            fill    = &InkPencilFillGlyph;
            break;
        case InkToolbarTool::Highlighter:
            outline = &InkHighlighterGlyph;
            fill    = &InkHighlighterFillGlyph;
            break;
        case InkToolbarTool::Eraser:
        case InkToolbarTool::CustomPen:
        case InkToolbarTool::CustomTool:
            break;
    }
    if (custom_)
    {
        // A custom pen's glyph takes its selected color, as the pens' fill does.
        draw_custom_glyph(renderer, theme, custom_->icon, bounds(), is_pen() ? selected_color() : text);
    }
    else
    {
        if (fill != nullptr)
        {
            draw_icon(renderer, *fill, bounds(), selected_color(), InkGlyphSize, theme.icon_font_family);
        }
        draw_icon(renderer, *outline, bounds(), text, InkGlyphSize, theme.icon_font_family);
    }

    if (extension_glyph_shown())
    {
        const auto placement = owner_ != nullptr ? owner_->button_flyout_placement() : InkToolbarButtonFlyoutPlacement::Auto;
        draw_ink_extension(renderer, theme, bounds(), placement, enabled ? accent_text(theme) : text);
    }
}

bool InkToolbarToolButtonControl::pointer_override(const PointerEvent& e)
{
    const bool enabled = owner_ == nullptr || owner_->is_enabled();
    const bool inside  = bounds().contains(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (hovered_ != inside)
            {
                hovered_ = inside;
                mark_dirty(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Down:
            if (enabled && e.button == PointerButton::Left && inside)
            {
                pressed_ = true;
                mark_dirty(DirtyFlags::Visual);
                return true;
            }
            return false;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_)
            {
                pressed_ = false;
                mark_dirty(DirtyFlags::Visual);
                if (inside && enabled && owner_ != nullptr)
                {
                    owner_->clicked(*this);
                }
                return true;
            }
            return false;
        case PointerAction::Leave:
            hovered_ = false;
            pressed_ = false;
            mark_dirty(DirtyFlags::Visual);
            return false;
        case PointerAction::Wheel:
            return false;
    }
    return false;
}

std::string InkToolbarToolButtonControl::tool_tip() const
{
    return custom_ ? custom_->name : ink_tool_name(tool_);
}

bool InkToolbarToolButtonControl::select_item()
{
    if (owner_ == nullptr || !owner_->is_enabled())
    {
        return false;
    }
    owner_->set_active_tool_button(this);
    return true;
}

bool InkToolbarToolButtonControl::expanded_state() const
{
    return flyout_ != nullptr && flyout_->is_open();
}

bool InkToolbarToolButtonControl::set_expanded_state(bool expanded)
{
    if (owner_ == nullptr)
    {
        return false;
    }
    if (!expanded)
    {
        if (expanded_state())
        {
            owner_->close_flyout();
        }
        return true;
    }
    InkToolbarControl::Slot* slot = owner_->slot_of(this);
    return slot != nullptr && owner_->open_slot_flyout(*slot);
}

std::string InkToolbarToolButtonControl::automation_name_fallback() const
{
    std::string name = tool_tip();
    if (checked_)
    {
        name = detail::fill(strings().ink_toolbar.selected, {name});
    }
    return name;
}

// ---- InkToolbarControl ----

InkToolbarControl::InkToolbarControl(std::span<const InkToolbarTool> tools) :
    panel_(std::make_unique<StackControl>(Orientation::Horizontal))
{
    set_parent_of(*panel_, this);
    panel_->set_margin(Thickness{InkToolbarMargin});

    for (const InkToolbarTool tool : tools)
    {
        if (slot_for(tool) != nullptr || tool == InkToolbarTool::CustomPen || tool == InkToolbarTool::CustomTool)
        {
            continue;
        }
        (void)add_slot(std::make_unique<InkToolbarToolButtonControl>(tool));
    }

    sync_flyout_placement();
    if (!slots_.empty())
    {
        active_                         = slots_.front().button->tool();
        active_button_                  = slots_.front().button;
        slots_.front().button->checked_ = true;
        if (slots_.front().button->is_pen())
        {
            attributes_ = slots_.front().button->drawing_attributes();
        }
        else if (const Slot* pen = slot_for(InkToolbarTool::BallpointPen))
        {
            attributes_ = pen->button->drawing_attributes();
        }
    }
}

InkToolbarToolButtonControl* InkToolbarControl::add_slot(std::unique_ptr<InkToolbarToolButtonControl> owned)
{
    InkToolbarToolButtonControl& b = *owned;
    Slot                         slot{.button = &b};
    b.owner_                             = this;
    const InkToolbarCustomButton* custom = b.custom();

    // A custom tool without configuration content has no flyout: the button sits in the panel.
    if (b.tool() == InkToolbarTool::CustomTool && (custom == nullptr || !custom->configuration))
    {
        slot.item = panel_->insert(slots_.size(), std::move(owned));
        slots_.push_back(slot);
        return &b;
    }

    auto flyout = std::make_unique<FlyoutControl>();
    slot.flyout = flyout.get();
    b.flyout_   = flyout.get();
    flyout->set_anchor(std::move(owned));
    {
        if (custom != nullptr && custom->configuration)
        {
            slot.flyout->set_content(custom->configuration().build());
        }
        else if (b.is_pen())
        {
            auto panel = std::make_unique<StackControl>(Orientation::Vertical);
            panel->set_min_width(InkFlyoutMinWidth);
            panel->add(ink_flyout_heading(&Strings::InkToolbarText::colors,
                                          Edges{.left   = InkFlyoutSidePadding,
                                                .top    = 8.0f,
                                                .right  = InkFlyoutSidePadding,
                                                .bottom = 12.0f}));
            auto* grid = panel->add(std::make_unique<InkPaletteGrid>(b));
            grid->set_margin(Edges{.left = 4.0f, .right = 4.0f, .bottom = 2.0f + 12.0f});
            panel->add(ink_flyout_heading(&Strings::InkToolbarText::size,
                                          Symmetric{.horizontal = InkFlyoutSidePadding, .vertical = 0.0f}));
            auto* preview = panel->add(std::make_unique<InkStrokePreview>(b));
            preview->set_margin(Edges{.left = InkFlyoutSidePadding, .right = InkFlyoutSidePadding, .bottom = 4.0f});
            auto* slider = panel->add(std::make_unique<SliderControl>(Orientation::Horizontal,
                                                                      b.min_stroke_width(),
                                                                      b.max_stroke_width(),
                                                                      b.selected_stroke_width()));
            slider->set_width(InkSliderWidth);
            slider->set_height(InkSliderHeight);
            slider->set_margin(Edges{.left = InkFlyoutSidePadding, .right = InkFlyoutSidePadding, .bottom = 12.0f});
            detail::follow_strings(*slider,
                                   [slider]
                                   { slider->set_accessibility(Accessibility{.name = strings().ink_toolbar.pen_size}); });
            slider->on_value_changed([&b](double v) { b.set_selected_stroke_width(static_cast<float>(v)); });
            slot.width_slider = slider;
            slot.flyout->set_content(std::move(panel));
        }
        else
        {
            auto panel = std::make_unique<StackControl>(Orientation::Vertical);
            panel->set_margin(Symmetric{.horizontal = 0.0f, .vertical = 2.0f});
            const auto pick = [this](InkCanvasEraseMode mode)
            {
                return [this, mode]
                {
                    close_flyout();
                    if (InkCanvasControl* canvas = target_ink_canvas())
                    {
                        canvas->set_erase_mode(mode);
                    }
                };
            };
            slot.stroke_item = panel->add(std::make_unique<InkFlyoutItem>(InkEraserGlyph,
                                                                          &Strings::InkToolbarText::stroke_eraser,
                                                                          pick(InkCanvasEraseMode::Stroke)));
            slot.point_item  = panel->add(std::make_unique<InkFlyoutItem>(InkPointEraserGlyph,
                                                                          &Strings::InkToolbarText::point_eraser,
                                                                          pick(InkCanvasEraseMode::Point)));
            slot.erase_all   = panel->add(
                std::make_unique<InkFlyoutItem>(InkEraseAllGlyph,
                                                &Strings::InkToolbarText::erase_all_ink,
                                                [this]
                                                {
                                                    close_flyout();
                                                    erase_all();
                                                }));
            slot.flyout->set_content(std::move(panel));
        }
    }
    slot.item = panel_->insert(slots_.size(), std::move(flyout));
    slots_.push_back(slot);
    return &b;
}

InkToolbarControl::Slot* InkToolbarControl::slot_of(const InkToolbarToolButtonControl* button)
{
    const auto it = std::ranges::find(slots_, button, &Slot::button);
    return it == slots_.end() ? nullptr : &*it;
}

InkToolbarToolButtonControl* InkToolbarControl::add_custom_pen(InkToolbarCustomButton button)
{
    InkToolbarToolButtonControl* added = add_slot(
        std::make_unique<InkToolbarToolButtonControl>(InkToolbarTool::CustomPen, std::move(button)));
    sync_flyout_placement();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return added;
}

InkToolbarToolButtonControl* InkToolbarControl::add_custom_tool(InkToolbarCustomButton button)
{
    InkToolbarToolButtonControl* added = add_slot(
        std::make_unique<InkToolbarToolButtonControl>(InkToolbarTool::CustomTool, std::move(button)));
    sync_flyout_placement();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return added;
}

InkToolbarToggleButtonControl* InkToolbarControl::add_custom_toggle(InkToolbarCustomButton button)
{
    auto* toggle   = panel_->add(std::make_unique<InkToolbarToggleButtonControl>(std::move(button)));
    toggle->owner_ = this;
    toggles_.push_back(toggle);
    extras_.push_back(toggle);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return toggle;
}

std::vector<InkToolbarToggleButtonControl*> InkToolbarControl::toggle_buttons() const
{
    return toggles_;
}

InkToolbarToolButtonControl* InkToolbarControl::active_tool_button() const
{
    return active_button_;
}

void InkToolbarControl::set_active_tool_button(InkToolbarToolButtonControl* button)
{
    if (Slot* slot = slot_of(button))
    {
        activate(*slot);
    }
}

InkToolbarControl::~InkToolbarControl() = default;

InkToolbarControl::Slot* InkToolbarControl::slot_for(InkToolbarTool tool)
{
    const auto it = std::ranges::find_if(slots_, [tool](const Slot& s) { return s.button->tool() == tool; });
    return it == slots_.end() ? nullptr : &*it;
}

const InkToolbarControl::Slot* InkToolbarControl::slot_for(InkToolbarTool tool) const
{
    const auto it = std::ranges::find_if(slots_, [tool](const Slot& s) { return s.button->tool() == tool; });
    return it == slots_.end() ? nullptr : &*it;
}

InkToolbarToolButtonControl* InkToolbarControl::tool_button(InkToolbarTool tool) const
{
    const Slot* slot = slot_for(tool);
    return slot != nullptr ? slot->button : nullptr;
}

void InkToolbarControl::set_active_tool(InkToolbarTool tool)
{
    if (Slot* slot = slot_for(tool))
    {
        activate(*slot);
    }
}

void InkToolbarControl::activate(Slot& target)
{
    Slot* slot = &target;
    if (slot->button->checked_)
    {
        return;
    }
    const InkToolbarTool tool = slot->button->tool();
    close_flyout();
    for (Slot& s : slots_)
    {
        s.button->checked_ = s.button == slot->button;
        s.button->mark_dirty(DirtyFlags::Visual);
    }
    active_                 = tool;
    active_button_          = slot->button;
    bool attributes_changed = false;
    if (slot->button->is_pen())
    {
        const InkDrawingAttributes next = slot->button->drawing_attributes();
        attributes_changed              = next != attributes_;
        attributes_                     = next;
    }
    apply_to_target();
    mark_dirty(DirtyFlags::Visual);
    if (on_active_tool_changed_)
    {
        on_active_tool_changed_(tool);
    }
    if (attributes_changed && on_attributes_changed_)
    {
        on_attributes_changed_(attributes_);
    }
}

bool InkToolbarControl::tool_visible(InkToolbarTool tool) const
{
    const Slot* slot = slot_for(tool);
    return slot != nullptr && slot->item->visibility() == Visibility::Visible;
}

void InkToolbarControl::set_tool_visible(InkToolbarTool tool, bool visible)
{
    Slot* slot = slot_for(tool);
    if (slot == nullptr || tool_visible(tool) == visible)
    {
        return;
    }
    if (!visible && slot->flyout != nullptr)
    {
        slot->flyout->hide();
    }
    slot->item->set_visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

Orientation InkToolbarControl::orientation() const
{
    return panel_->orientation();
}

void InkToolbarControl::set_orientation(Orientation orientation)
{
    panel_->set_orientation(orientation);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InkToolbarControl::set_button_flyout_placement(InkToolbarButtonFlyoutPlacement placement)
{
    if (placement_ == placement)
    {
        return;
    }
    placement_ = placement;
    sync_flyout_placement();
    mark_dirty(DirtyFlags::Visual);
}

void InkToolbarControl::sync_flyout_placement()
{
    PopupPlacement          side  = PopupPlacement::Below;
    PopupPlacementAlignment align = PopupPlacementAlignment::Leading;
    switch (placement_)
    {
        case InkToolbarButtonFlyoutPlacement::Top:
            side = PopupPlacement::Above;
            break;
        case InkToolbarButtonFlyoutPlacement::Left:
            side  = PopupPlacement::Left;
            align = PopupPlacementAlignment::Center;
            break;
        case InkToolbarButtonFlyoutPlacement::Right:
            side  = PopupPlacement::Right;
            align = PopupPlacementAlignment::Center;
            break;
        case InkToolbarButtonFlyoutPlacement::Auto:
        case InkToolbarButtonFlyoutPlacement::Bottom:
            break;
    }
    for (Slot& s : slots_)
    {
        if (s.flyout != nullptr)
        {
            s.flyout->set_placement(side);
            s.flyout->set_placement_alignment(align);
        }
    }
    if (stencil_ != nullptr)
    {
        stencil_->flyout_->set_placement(side);
        stencil_->flyout_->set_placement_alignment(align);
    }
}

InkCanvasControl* InkToolbarControl::target_ink_canvas() const
{
    // Only ever set from an InkCanvasControl*, so the downcast is safe.
    return static_cast<InkCanvasControl*>(target_.get());
}

void InkToolbarControl::set_target_ink_canvas(InkCanvasControl* canvas)
{
    target_.reset(canvas);
    apply_to_target();
    if (stencil_button_checked())
    {
        apply_stencil_to_target();
    }
}

void InkToolbarControl::apply_to_target()
{
    InkCanvasControl* canvas = target_ink_canvas();
    if (canvas == nullptr)
    {
        return;
    }
    if (active_ == InkToolbarTool::Eraser)
    {
        canvas->set_input_mode(InkCanvasInputMode::Erasing);
        return;
    }
    if (active_ == InkToolbarTool::CustomTool)
    {
        canvas->set_input_mode(InkCanvasInputMode::None); // the app handles the input
        return;
    }
    canvas->set_drawing_attributes(attributes_);
    canvas->set_input_mode(InkCanvasInputMode::Inking);
}

void InkToolbarControl::erase_all()
{
    if (InkCanvasControl* canvas = target_ink_canvas())
    {
        canvas->strokes().clear();
    }
    if (on_erase_all_clicked_)
    {
        on_erase_all_clicked_();
    }
}

void InkToolbarControl::button_attributes_changed(InkToolbarToolButtonControl& button)
{
    if (!button.checked_ || !button.is_pen())
    {
        return;
    }
    const InkDrawingAttributes next = button.drawing_attributes();
    if (next == attributes_)
    {
        return;
    }
    attributes_ = next;
    apply_to_target();
    if (on_attributes_changed_)
    {
        on_attributes_changed_(attributes_);
    }
}

bool InkToolbarControl::open_flyout(InkToolbarTool tool)
{
    Slot* slot = slot_for(tool);
    return slot != nullptr && open_slot_flyout(*slot);
}

bool InkToolbarControl::open_slot_flyout(Slot& target)
{
    Slot* slot = &target;
    if (slot->flyout == nullptr || slot->item->visibility() != Visibility::Visible)
    {
        return false;
    }
    if (slot->erase_all != nullptr)
    {
        slot->erase_all->set_visibility(slot->button->clear_all_visible() ? Visibility::Visible : Visibility::Collapsed);
        const InkCanvasControl* canvas = target_ink_canvas();
        const bool              point  = canvas != nullptr && canvas->erase_mode() == InkCanvasEraseMode::Point;
        static_cast<InkFlyoutItem*>(slot->stroke_item)->set_checked(!point);
        static_cast<InkFlyoutItem*>(slot->point_item)->set_checked(point);
    }
    if (slot->flyout->is_open())
    {
        return true;
    }
    close_flyout();
    if (slot->width_slider != nullptr)
    {
        const InkToolbarToolButtonControl& b = *slot->button;
        slot->width_slider->set_range(b.min_stroke_width(), b.max_stroke_width());
        slot->width_slider->set_value(b.selected_stroke_width());
    }
    slot->flyout->show();
    slot->button->mark_dirty(DirtyFlags::Visual);
    return true;
}

void InkToolbarControl::close_flyout()
{
    for (Slot& s : slots_)
    {
        if (s.flyout != nullptr && s.flyout->is_open())
        {
            s.flyout->hide();
            s.button->mark_dirty(DirtyFlags::Visual);
        }
    }
    if (stencil_ != nullptr && stencil_->flyout_->is_open())
    {
        stencil_->flyout_->hide();
        stencil_->mark_dirty(DirtyFlags::Visual);
    }
}

std::optional<InkToolbarTool> InkToolbarControl::open_flyout_tool() const
{
    for (const Slot& s : slots_)
    {
        if (s.flyout != nullptr && s.flyout->is_open())
        {
            return s.button->tool();
        }
    }
    return std::nullopt;
}

void InkToolbarControl::clicked(InkToolbarToolButtonControl& button)
{
    if (!is_enabled())
    {
        return;
    }
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        if (slots_[i].button == &button)
        {
            cursor_ = i;
        }
    }
    Slot* slot = slot_of(&button);
    if (slot == nullptr)
    {
        return;
    }
    if (!button.checked_)
    {
        activate(*slot);
        return;
    }
    if (slot->flyout != nullptr && slot->flyout->is_open())
    {
        close_flyout();
        return;
    }
    (void)open_slot_flyout(*slot);
}

void InkToolbarControl::enabled_changed()
{
    if (!is_enabled())
    {
        close_flyout();
    }
    for (Slot& s : slots_)
    {
        s.button->mark_dirty(DirtyFlags::Visual);
    }
    for (InkToolbarToggleButtonControl* toggle : toggles_)
    {
        toggle->mark_dirty(DirtyFlags::Visual);
    }
    if (stencil_ != nullptr)
    {
        stencil_->mark_dirty(DirtyFlags::Visual);
    }
    mark_dirty(DirtyFlags::Visual);
}

// The keyboard cursor's stops: the visible slots, then the visible toggles (indices past the slots).
std::vector<std::size_t> InkToolbarControl::visible_slots() const
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        if (slots_[i].item->visibility() == Visibility::Visible)
        {
            out.push_back(i);
        }
    }
    for (std::size_t i = 0; i < extras_.size(); ++i)
    {
        if (extras_[i]->visibility() == Visibility::Visible)
        {
            out.push_back(slots_.size() + i);
        }
    }
    return out;
}

Element* InkToolbarControl::cursor_element(std::size_t index) const
{
    if (index < slots_.size())
    {
        return slots_[index].button;
    }
    index -= slots_.size();
    return index < extras_.size() ? extras_[index] : nullptr;
}

void InkToolbarControl::activate_extra(Element* extra)
{
    if (extra == stencil_ && stencil_ != nullptr)
    {
        stencil_clicked();
    }
    else if (auto* toggle = dynamic_cast<InkToolbarToggleButtonControl*>(extra))
    {
        toggle->set_checked(!toggle->checked());
    }
}

InkToolbarStencilButtonControl* InkToolbarControl::add_stencil_button()
{
    if (stencil_ != nullptr)
    {
        return stencil_;
    }
    auto owned        = std::make_unique<InkToolbarStencilButtonControl>();
    stencil_          = owned.get();
    stencil_->owner_  = this;
    auto flyout       = std::make_unique<FlyoutControl>();
    stencil_->flyout_ = flyout.get();
    flyout->set_anchor(std::move(owned));
    auto panel = std::make_unique<StackControl>(Orientation::Vertical);
    panel->set_margin(Symmetric{.horizontal = 0.0f, .vertical = 2.0f});
    stencil_->ruler_entry_ = panel->add(
        std::make_unique<InkFlyoutItem>(InkRulerGlyph,
                                        &Strings::InkToolbarText::ruler,
                                        [this] { stencil_item_clicked(InkToolbarStencilKind::Ruler); }));
    stencil_->protractor_entry_ = panel->add(
        std::make_unique<InkFlyoutItem>(InkProtractorGlyph,
                                        &Strings::InkToolbarText::protractor,
                                        [this] { stencil_item_clicked(InkToolbarStencilKind::Protractor); }));
    flyout->set_content(std::move(panel));
    (void)panel_->insert(slots_.size(), std::move(flyout));
    extras_.insert(extras_.begin(), stencil_);
    sync_flyout_placement();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return stencil_;
}

void InkToolbarControl::set_stencil_button_checked(bool checked)
{
    if (stencil_ == nullptr || stencil_->checked_ == checked)
    {
        return;
    }
    stencil_->checked_ = checked;
    if (!checked && stencil_->flyout_->is_open())
    {
        stencil_->flyout_->hide();
    }
    stencil_->mark_dirty(DirtyFlags::Visual);
    apply_stencil_to_target();
    if (on_stencil_checked_changed_)
    {
        on_stencil_checked_changed_(InkToolbarStencilChangedArgs{.checked = checked, .kind = stencil_->selected_});
    }
}

void InkToolbarControl::stencil_clicked()
{
    if (!is_enabled() || stencil_ == nullptr)
    {
        return;
    }
    if (!stencil_->offers_both())
    {
        // One stencil on offer: a plain toggle, showing that one.
        stencil_->selected_ = stencil_->ruler_item_ ? InkToolbarStencilKind::Ruler : InkToolbarStencilKind::Protractor;
        set_stencil_button_checked(!stencil_->checked_);
        return;
    }
    if (!stencil_->checked_)
    {
        set_stencil_button_checked(true);
        return;
    }
    if (stencil_->flyout_->is_open())
    {
        stencil_->flyout_->hide();
    }
    else
    {
        close_flyout();
        static_cast<InkFlyoutItem*>(stencil_->ruler_entry_)->set_checked(stencil_->selected_ == InkToolbarStencilKind::Ruler);
        static_cast<InkFlyoutItem*>(stencil_->protractor_entry_)
            ->set_checked(stencil_->selected_ == InkToolbarStencilKind::Protractor);
        stencil_->flyout_->show();
    }
    stencil_->mark_dirty(DirtyFlags::Visual);
}

void InkToolbarControl::stencil_item_clicked(InkToolbarStencilKind kind)
{
    if (stencil_ == nullptr)
    {
        return;
    }
    stencil_->flyout_->hide();
    if (stencil_->checked_ && stencil_->selected_ == kind)
    {
        set_stencil_button_checked(false); // the one showing: hide it
        return;
    }
    stencil_->set_selected_stencil(kind);
    set_stencil_button_checked(true);
}

void InkToolbarControl::apply_stencil_to_target()
{
    InkCanvasControl* canvas = target_ink_canvas();
    if (canvas == nullptr || stencil_ == nullptr)
    {
        return;
    }
    const bool on            = stencil_->checked_;
    InkRuler   ruler         = canvas->ruler();
    ruler.visible            = on && stencil_->selected_ == InkToolbarStencilKind::Ruler;
    InkProtractor protractor = canvas->protractor();
    protractor.visible       = on && stencil_->selected_ == InkToolbarStencilKind::Protractor;
    canvas->set_ruler(ruler);
    canvas->set_protractor(protractor);
}

void InkToolbarControl::focus_changed(bool /*focused*/)
{
    if (is_focused())
    {
        const std::vector<std::size_t> visible = visible_slots();
        const auto active = std::ranges::find_if(visible,
                                                 [this](std::size_t i)
                                                 { return i < slots_.size() && slots_[i].button->checked_; });
        if (active != visible.end())
        {
            cursor_ = *active;
        }
        else if (!visible.empty())
        {
            cursor_ = visible.front();
        }
    }
    mark_dirty(DirtyFlags::Visual);
}

const InkToolbarToolButtonControl* InkToolbarControl::focused_button() const
{
    if (!is_focused() || cursor_ >= slots_.size())
    {
        return nullptr;
    }
    return slots_[cursor_].button;
}

void InkToolbarControl::move_cursor(int delta, bool to_end)
{
    const std::vector<std::size_t> visible = visible_slots();
    if (visible.empty())
    {
        return;
    }
    if (to_end)
    {
        cursor_ = delta < 0 ? visible.front() : visible.back();
    }
    else
    {
        const auto at  = std::ranges::find(visible, cursor_);
        auto       pos = at == visible.end() ? std::ptrdiff_t{0} : std::distance(visible.begin(), at) + delta;
        pos            = std::clamp<std::ptrdiff_t>(pos, 0, static_cast<std::ptrdiff_t>(visible.size()) - 1);
        cursor_        = visible[static_cast<std::size_t>(pos)];
    }
    mark_dirty(DirtyFlags::Visual);
}

bool InkToolbarControl::takes_arrow_key(Key key) const
{
    if (orientation() == Orientation::Horizontal)
    {
        return key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    return key == Key::ArrowUp || key == Key::ArrowDown;
}

bool InkToolbarControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled())
    {
        return handled;
    }
    const bool horizontal = orientation() == Orientation::Horizontal;
    if (e.action == KeyAction::Down)
    {
        if (e.key == (horizontal ? Key::ArrowLeft : Key::ArrowUp))
        {
            move_cursor(-1, false);
        }
        else if (e.key == (horizontal ? Key::ArrowRight : Key::ArrowDown))
        {
            move_cursor(1, false);
        }
        else if (e.key == Key::Home)
        {
            move_cursor(-1, true);
        }
        else if (e.key == Key::End)
        {
            move_cursor(1, true);
        }
        return handled;
    }
    if (e.key == Key::Space || e.key == Key::Enter)
    {
        if (cursor_ < slots_.size())
        {
            clicked(*slots_[cursor_].button);
        }
        else if (cursor_ - slots_.size() < extras_.size())
        {
            activate_extra(extras_[cursor_ - slots_.size()]);
        }
    }
    return handled;
}

Size InkToolbarControl::measure_override(Size available, const MeasureContext& context) const
{
    return panel_->measure(available, context);
}

void InkToolbarControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    panel_->arrange(final_rect);
}

void InkToolbarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    panel_->render(renderer, context);
}

Rect InkToolbarControl::focus_ring_rect(const Theme& /*theme*/) const
{
    const Element* focused = cursor_element(cursor_);
    return focused != nullptr ? focused->bounds() : Rect{};
}

void InkToolbarControl::for_each_child(ChildVisitor visit) const
{
    visit(*panel_);
}

void InkToolbarControl::automation_children(std::vector<Element*>& out) const
{
    for (const Slot& s : slots_)
    {
        if (s.item->visibility() == Visibility::Visible)
        {
            out.push_back(s.item);
        }
    }
    for (Element* extra : extras_)
    {
        if (extra->visibility() == Visibility::Visible)
        {
            out.push_back(extra == stencil_ ? stencil_->flyout_ : extra); // through its flyout, as the tools
        }
    }
}

// ---- InkToolbarStencilButtonControl ----

InkToolbarStencilButtonControl::InkToolbarStencilButtonControl()
{
    set_margin(Thickness{InkButtonMargin});
}

InkToolbarStencilButtonControl::~InkToolbarStencilButtonControl() = default;

void InkToolbarStencilButtonControl::set_selected_stencil(InkToolbarStencilKind kind)
{
    if (selected_ == kind)
    {
        return;
    }
    selected_ = kind;
    mark_dirty(DirtyFlags::Visual);
    if (checked_ && owner_ != nullptr)
    {
        owner_->apply_stencil_to_target();
    }
}

void InkToolbarStencilButtonControl::set_ruler_item_visible(bool visible)
{
    ruler_item_ = visible;
    if (ruler_entry_ != nullptr)
    {
        ruler_entry_->set_visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    }
    if (!visible && protractor_item_)
    {
        set_selected_stencil(InkToolbarStencilKind::Protractor);
    }
    mark_dirty(DirtyFlags::Visual);
}

void InkToolbarStencilButtonControl::set_protractor_item_visible(bool visible)
{
    protractor_item_ = visible;
    if (protractor_entry_ != nullptr)
    {
        protractor_entry_->set_visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    }
    if (!visible && ruler_item_)
    {
        set_selected_stencil(InkToolbarStencilKind::Ruler);
    }
    mark_dirty(DirtyFlags::Visual);
}

bool InkToolbarStencilButtonControl::extension_glyph_shown() const
{
    return checked_ && offers_both() && flyout_ != nullptr && !flyout_->is_open();
}

Size InkToolbarStencilButtonControl::measure_override(Size, const MeasureContext&) const
{
    return Size{InkButtonSize, InkButtonSize};
}

void InkToolbarStencilButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme   = context.theme;
    const bool   enabled = owner_ == nullptr || owner_->is_enabled();
    const Color text = draw_ink_button_background(renderer, theme, bounds(), ink_button_state(enabled, pressed_, hovered_), checked_);
    draw_icon(renderer,
              selected_ == InkToolbarStencilKind::Ruler ? InkRulerGlyph : InkProtractorGlyph,
              bounds(),
              text,
              InkGlyphSize,
              theme.icon_font_family);
    if (extension_glyph_shown())
    {
        const auto placement = owner_ != nullptr ? owner_->button_flyout_placement() : InkToolbarButtonFlyoutPlacement::Auto;
        draw_ink_extension(renderer, theme, bounds(), placement, enabled ? accent_text(theme) : text);
    }
}

bool InkToolbarStencilButtonControl::pointer_override(const PointerEvent& e)
{
    const bool enabled = owner_ == nullptr || owner_->is_enabled();
    const bool inside  = bounds().contains(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (hovered_ != inside)
            {
                hovered_ = inside;
                mark_dirty(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Down:
            if (enabled && e.button == PointerButton::Left && inside)
            {
                pressed_ = true;
                mark_dirty(DirtyFlags::Visual);
                return true;
            }
            return false;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_)
            {
                pressed_ = false;
                mark_dirty(DirtyFlags::Visual);
                if (inside && enabled && owner_ != nullptr)
                {
                    owner_->stencil_clicked();
                }
                return true;
            }
            return false;
        case PointerAction::Leave:
            hovered_ = false;
            pressed_ = false;
            mark_dirty(DirtyFlags::Visual);
            return false;
        case PointerAction::Wheel:
            return false;
    }
    return false;
}

std::string InkToolbarStencilButtonControl::tool_tip() const
{
    return selected_ == InkToolbarStencilKind::Ruler ? strings().ink_toolbar.ruler : strings().ink_toolbar.protractor;
}

bool InkToolbarStencilButtonControl::toggle_next()
{
    if (owner_ == nullptr || !owner_->is_enabled())
    {
        return false;
    }
    owner_->set_stencil_button_checked(!checked_);
    return true;
}

bool InkToolbarStencilButtonControl::expanded_state() const
{
    return flyout_ != nullptr && flyout_->is_open();
}

bool InkToolbarStencilButtonControl::set_expanded_state(bool expanded)
{
    if (owner_ == nullptr || flyout_ == nullptr || !offers_both())
    {
        return false;
    }
    if (expanded != flyout_->is_open())
    {
        if (!expanded)
        {
            flyout_->hide();
        }
        else
        {
            if (!checked_)
            {
                owner_->set_stencil_button_checked(true);
            }
            owner_->stencil_clicked(); // on: opens the flyout
        }
    }
    return true;
}

// ---- InkToolbarToggleButtonControl ----

InkToolbarToggleButtonControl::InkToolbarToggleButtonControl(InkToolbarCustomButton look) : look_(std::move(look))
{
    set_margin(Thickness{InkButtonMargin});
}

InkToolbarToggleButtonControl::~InkToolbarToggleButtonControl() = default;

void InkToolbarToggleButtonControl::set_checked(bool checked)
{
    if (checked_ == checked)
    {
        return;
    }
    checked_ = checked;
    mark_dirty(DirtyFlags::Visual);
    if (look_.on_checked_changed)
    {
        look_.on_checked_changed(checked);
    }
    if (on_checked_changed_)
    {
        on_checked_changed_(checked);
    }
}

Size InkToolbarToggleButtonControl::measure_override(Size, const MeasureContext&) const
{
    return Size{InkButtonSize, InkButtonSize};
}

void InkToolbarToggleButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme   = context.theme;
    const bool   enabled = owner_ == nullptr || owner_->is_enabled();
    const Color text = draw_ink_button_background(renderer, theme, bounds(), ink_button_state(enabled, pressed_, hovered_), checked_);
    draw_custom_glyph(renderer, theme, look_.icon, bounds(), text);
}

bool InkToolbarToggleButtonControl::pointer_override(const PointerEvent& e)
{
    const bool enabled = owner_ == nullptr || owner_->is_enabled();
    const bool inside  = bounds().contains(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (hovered_ != inside)
            {
                hovered_ = inside;
                mark_dirty(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Down:
            if (enabled && e.button == PointerButton::Left && inside)
            {
                pressed_ = true;
                mark_dirty(DirtyFlags::Visual);
                return true;
            }
            return false;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_)
            {
                pressed_ = false;
                mark_dirty(DirtyFlags::Visual);
                if (inside && enabled)
                {
                    set_checked(!checked_);
                }
                return true;
            }
            return false;
        case PointerAction::Leave:
            hovered_ = false;
            pressed_ = false;
            mark_dirty(DirtyFlags::Visual);
            return false;
        case PointerAction::Wheel:
            return false;
    }
    return false;
}

bool InkToolbarToggleButtonControl::toggle_next()
{
    if (owner_ != nullptr && !owner_->is_enabled())
    {
        return false;
    }
    set_checked(!checked_);
    return true;
}

std::unique_ptr<ui::Element> InkToolbar::build() &&
{
    auto                   node = std::make_unique<ui::InkToolbarControl>(tools);
    ui::InkToolbarControl* raw  = node.get();
    for (ui::InkToolbarCustomButton& pen : custom_pens)
    {
        (void)node->add_custom_pen(std::move(pen));
    }
    for (ui::InkToolbarCustomButton& tool : custom_tools)
    {
        (void)node->add_custom_tool(std::move(tool));
    }
    if (stencil_button)
    {
        (void)node->add_stencil_button();
    }
    for (ui::InkToolbarCustomButton& toggle : custom_toggles)
    {
        (void)node->add_custom_toggle(std::move(toggle));
    }

    apply(active_tool, *node, [raw](const ui::InkToolbarTool& v) { raw->set_active_tool(v); });
    apply(orientation, *node, [raw](const ui::Orientation& v) { raw->set_orientation(v); });
    apply(button_flyout_placement,
          *node,
          [raw](const ui::InkToolbarButtonFlyoutPlacement& v) { raw->set_button_flyout_placement(v); });
    apply(enabled, *node, [raw](const bool& v) { raw->set_enabled(v); });

    if (auto changed = ui::write_back(active_tool, std::move(on_active_tool_changed)))
    {
        node->on_active_tool_changed(std::move(changed));
    }
    if (on_ink_drawing_attributes_changed)
    {
        node->on_ink_drawing_attributes_changed(std::move(on_ink_drawing_attributes_changed));
    }
    if (on_erase_all_clicked)
    {
        node->on_erase_all_clicked(std::move(on_erase_all_clicked));
    }
    if (on_stencil_button_checked_changed)
    {
        node->on_stencil_button_checked_changed(std::move(on_stencil_button_checked_changed));
    }

    target.resolve(*node, [raw = node.get()](InkCanvasControl* canvas) { raw->set_target_ink_canvas(canvas); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
