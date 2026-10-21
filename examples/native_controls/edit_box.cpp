// SPDX-License-Identifier: MIT
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "edit_box.hpp"

#include "strings.hpp"

#include <algorithm>
#include <vector>
#include <windows.h>

#include <cmath>
#include <cwchar>

// commctrl.h needs <windows.h> in scope and does not include it; the clang-format include
// sort keeps it in this block.
#include <commctrl.h>

// SetWindowTheme, for the dark visual style; it needs both headers above in scope.
#include <uxtheme.h>

namespace native_controls
{
namespace
{

constexpr UINT_PTR SubclassId = 1;

// Before the window exists (and so before its font does), a box with no height of its own is
// given this: one line of the 9 pt message font at 96 DPI, as a dialog lays an EDIT out.
constexpr float EstimatedLineHeight = 23.0f;

// EDIT answers WM_CONTEXTMENU itself and never tells its parent. Forward it like RichEdit does,
// so the framework's menu shows. No reference data, so it stays safe if the EditBox dies first.
LRESULT CALLBACK edit_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
{
    switch (msg)
    {
        case WM_CONTEXTMENU:
            return SendMessageW(GetParent(hwnd), WM_CONTEXTMENU, wp, lp);
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &edit_subclass, id);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

EditBoxOptions normalized(EditBoxOptions options)
{
    if (options.password)
    {
        options.accepts_return = false; // ES_PASSWORD is a single-line style
    }
    return options;
}

// EM_SETPASSWORDCHAR takes one UTF-16 unit, so a mask past the BMP falls back to the default.
WPARAM password_wparam(char32_t mask)
{
    return mask <= 0xFFFF && (mask < 0xD800 || mask > 0xDFFF) ? static_cast<WPARAM>(mask) : WPARAM{0x25CF};
}

ui::NativeControlDesc make_desc(std::string initial_text, const EditBoxOptions& options)
{
    // No LoadLibraryW here, unlike RichEditBox's Msftedit.dll: EDIT is one of the window
    // classes user32 registers for every process before it runs a line of its code.
    ui::NativeControlDesc desc;
    desc.class_name   = "EDIT";
    desc.initial_text = std::move(initial_text);
    // WS_EX_CLIENTEDGE, not WS_BORDER: a themed EDIT draws its visual-style border into the
    // client edge, and a WS_BORDER box under the light style shows no border at all.
    desc.ex_style = WS_EX_CLIENTEDGE;
    DWORD style   = 0;
    if (options.accepts_return)
    {
        style |= ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL;
        if (options.wrapping == ui::TextWrapping::NoWrap)
        {
            style |= ES_AUTOHSCROLL | WS_HSCROLL; // without it a multi-line EDIT wraps
        }
    }
    else
    {
        style |= ES_AUTOHSCROLL;
    }
    if (options.password)
    {
        style |= ES_PASSWORD;
    }
    switch (options.alignment)
    {
        case ui::TextAlignment::Center:
            style |= ES_CENTER;
            break;
        case ui::TextAlignment::Right:
            style |= ES_RIGHT;
            break;
        case ui::TextAlignment::Left:
        case ui::TextAlignment::Justify:
            break;
    }
    desc.style = style;
    return desc;
}

[[nodiscard]] COLORREF to_colorref(const ui::Color& c)
{
    const auto channel = [](float v) { return static_cast<BYTE>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return RGB(channel(c.r), channel(c.g), channel(c.b));
}

// `top` composited over an opaque `bottom`: the one solid color a GDI brush can paint.
[[nodiscard]] ui::Color over(const ui::Color& top, const ui::Color& bottom)
{
    const float a = top.a;
    return ui::Color{top.r * a + bottom.r * (1.0f - a),
                     top.g * a + bottom.g * (1.0f - a),
                     top.b * a + bottom.b * (1.0f - a),
                     1.0f};
}

} // namespace

EditBox::EditBox(ui::Size size, std::string initial_text, EditBoxOptions options) :
    ui::NativeControl(make_desc(initial_text, normalized(options)),
                      ui::Size{size.width, size.height > 0.0f ? size.height : EstimatedLineHeight}),
    options_(normalized(options)),
    requested_height_(size.height),
    pending_text_(std::move(initial_text))
{
}

EditBox::~EditBox()
{
    // ~NativeControl destroys the window after this, so hand the font back before deleting it.
    if (void* handle = native_handle())
    {
        SendMessageW(static_cast<HWND>(handle), WM_SETFONT, 0, FALSE);
    }
    if (font_)
    {
        DeleteObject(static_cast<HFONT>(font_));
    }
    if (brush_)
    {
        DeleteObject(static_cast<HBRUSH>(brush_));
    }
}

void EditBox::on_native_created(ui::NativeHandle handle)
{
    HWND hwnd = static_cast<HWND>(handle);

    // Without a font a stock EDIT draws in SYSTEM_FONT, the Windows 3.1 bitmap face.
    apply_font(handle);

    if (text_pending_)
    {
        SendMessageW(hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(widen(pending_text_).c_str()));
        text_pending_ = false;
    }
    if (!placeholder_.empty())
    {
        SendMessageW(hwnd, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(widen(placeholder_).c_str()));
    }
    if (read_only_)
    {
        SendMessageW(hwnd, EM_SETREADONLY, TRUE, 0);
    }
    if (max_length_ > 0)
    {
        SendMessageW(hwnd, EM_SETLIMITTEXT, static_cast<WPARAM>(max_length_), 0);
    }
    apply_style_bit(ES_LOWERCASE, casing_ == ui::CharacterCasing::Lower);
    apply_style_bit(ES_UPPERCASE, casing_ == ui::CharacterCasing::Upper);
    apply_style_bit(ES_NUMBER, number_only_);
    if (options_.password)
    {
        SendMessageW(hwnd, EM_SETPASSWORDCHAR, password_revealed_ ? 0 : password_wparam(password_char_), 0);
    }
    if (!enabled_)
    {
        EnableWindow(hwnd, FALSE);
    }
    if (theme_seen_)
    {
        apply_theme(handle);
    }

    SetWindowSubclass(hwnd, &edit_subclass, SubclassId, 0);
}

void EditBox::apply_font(void* handle)
{
    HWND           hwnd = static_cast<HWND>(handle);
    const unsigned dpi  = GetDpiForWindow(hwnd);

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
    {
        return;
    }
    LOGFONTW font = metrics.lfMessageFont;
    if (!font_family_.empty())
    {
        wcsncpy_s(font.lfFaceName, widen(font_family_).c_str(), _TRUNCATE);
    }
    if (font_size_ > 0.0f)
    {
        // A negative height is the em size (character height), which is what a DIP FontSize is.
        font.lfHeight = -static_cast<LONG>(std::lround(font_size_ * static_cast<float>(dpi) / 96.0f));
    }
    if (font_weight_)
    {
        font.lfWeight = static_cast<LONG>(*font_weight_);
    }
    font.lfItalic = italic_ ? TRUE : FALSE;

    HFONT created = CreateFontIndirectW(&font);
    if (!created)
    {
        return;
    }
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(created), TRUE);
    if (font_)
    {
        DeleteObject(static_cast<HFONT>(font_));
    }
    font_     = created;
    font_dpi_ = dpi;

    if (requested_height_ <= 0.0f)
    {
        // One line of this font plus what a dialog's EDIT keeps around it: the client edge and
        // the class's own margins, about 8 px at 100%.
        TEXTMETRICW metric{};
        if (HDC dc = GetDC(hwnd))
        {
            HGDIOBJ old = SelectObject(dc, created);
            GetTextMetricsW(dc, &metric);
            SelectObject(dc, old);
            ReleaseDC(hwnd, dc);
        }
        if (metric.tmHeight > 0)
        {
            const float scale = static_cast<float>(dpi) / 96.0f;
            const float px    = static_cast<float>(metric.tmHeight) + std::round(8.0f * scale);
            set_native_size(ui::Size{native_size().width, px / scale});
        }
    }
}

void EditBox::apply_style_bit(std::uint32_t bit, bool on) const
{
    // ES_LOWERCASE, ES_UPPERCASE and ES_NUMBER are the styles the class documents as changeable
    // after creation with SetWindowLong; the rest are read only when it is created.
    auto* handle = native_handle();
    if (!handle)
    {
        return;
    }
    HWND           hwnd  = static_cast<HWND>(handle);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR next  = on ? (style | static_cast<LONG_PTR>(bit)) : (style & ~static_cast<LONG_PTR>(bit));
    if (next != style)
    {
        SetWindowLongPtrW(hwnd, GWL_STYLE, next);
    }
}

void EditBox::apply_theme(void* handle) const
{
    HWND hwnd = static_cast<HWND>(handle);
    // DarkMode_Explorer darkens the scroll bars; text and fill come from
    // handle_native_notification(). The window's own host has put the process into the matching
    // immersive mode by the time a control is themed.
    SetWindowTheme(hwnd, dark_ ? L"DarkMode_Explorer" : nullptr, nullptr);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}

void EditBox::set_text(const std::string& text)
{
    if (auto* handle = native_handle())
    {
        const std::wstring wide = widen(text);
        SendMessageW(static_cast<HWND>(handle), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        // A multi-line EDIT sends no EN_CHANGE for WM_SETTEXT (a single-line one does, which
        // reaches on_native_content_changed()), and the handler runs for changes from code too.
        if (options_.accepts_return && on_text_changed_)
        {
            on_text_changed_();
        }
        return;
    }
    pending_text_ = text;
    text_pending_ = true;
}

std::string EditBox::text() const
{
    auto* handle = native_handle();
    if (!handle)
    {
        return pending_text_;
    }
    HWND      hwnd   = static_cast<HWND>(handle);
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0)
    {
        return {};
    }
    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, buffer.data(), length + 1);
    buffer.resize(static_cast<std::size_t>(length));
    return narrow(buffer);
}

void EditBox::set_header(std::string header)
{
    if (header_.set_text(std::move(header)))
    {
        mark_dirty(ui::DirtyFlags::Layout | ui::DirtyFlags::Visual);
    }
}

void EditBox::set_placeholder_text(std::string text)
{
    placeholder_ = std::move(text);
    if (auto* handle = native_handle())
    {
        const std::wstring wide = widen(placeholder_);
        SendMessageW(static_cast<HWND>(handle), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(wide.c_str()));
    }
}

void EditBox::set_read_only(bool read_only)
{
    read_only_ = read_only;
    if (auto* handle = native_handle())
    {
        SendMessageW(static_cast<HWND>(handle), EM_SETREADONLY, read_only ? TRUE : FALSE, 0);
    }
}

void EditBox::set_enabled(bool enabled)
{
    if (enabled == enabled_)
    {
        return;
    }
    enabled_ = enabled;
    if (auto* handle = native_handle())
    {
        EnableWindow(static_cast<HWND>(handle), enabled ? TRUE : FALSE);
    }
    mark_dirty(ui::DirtyFlags::Visual); // the header grays with it
}

void EditBox::set_max_length(int length)
{
    max_length_ = std::max(0, length);
    if (auto* handle = native_handle())
    {
        // 0 restores the class's own limit (0x7FFFFFFE characters on one line, none on several).
        SendMessageW(static_cast<HWND>(handle), EM_SETLIMITTEXT, static_cast<WPARAM>(max_length_), 0);
    }
}

void EditBox::set_character_casing(ui::CharacterCasing casing)
{
    casing_ = casing;
    apply_style_bit(ES_LOWERCASE, casing == ui::CharacterCasing::Lower);
    apply_style_bit(ES_UPPERCASE, casing == ui::CharacterCasing::Upper);
}

void EditBox::set_number_only(bool number_only)
{
    number_only_ = number_only;
    apply_style_bit(ES_NUMBER, number_only);
}

void EditBox::set_password_char(char32_t mask)
{
    password_char_ = mask;
    if (auto* handle = native_handle(); handle && options_.password && !password_revealed_)
    {
        SendMessageW(static_cast<HWND>(handle), EM_SETPASSWORDCHAR, password_wparam(mask), 0);
        InvalidateRect(static_cast<HWND>(handle), nullptr, TRUE);
    }
}

void EditBox::set_password_revealed(bool revealed)
{
    password_revealed_ = revealed;
    if (auto* handle = native_handle(); handle && options_.password)
    {
        // A zero password character shows the text as typed; the box stays a password box
        // (no Cut or Copy) all the while.
        SendMessageW(static_cast<HWND>(handle), EM_SETPASSWORDCHAR, revealed ? 0 : password_wparam(password_char_), 0);
        InvalidateRect(static_cast<HWND>(handle), nullptr, TRUE);
    }
}

void EditBox::set_font_family(std::string family)
{
    font_family_ = std::move(family);
    if (auto* handle = native_handle())
    {
        apply_font(handle);
    }
}

void EditBox::set_font_size(float size)
{
    font_size_ = std::max(0.0f, size);
    if (auto* handle = native_handle())
    {
        apply_font(handle);
    }
}

void EditBox::set_font_weight(std::optional<ui::FontWeight> weight)
{
    font_weight_ = weight;
    if (auto* handle = native_handle())
    {
        apply_font(handle);
    }
}

void EditBox::set_italic(bool italic)
{
    italic_ = italic;
    if (auto* handle = native_handle())
    {
        apply_font(handle);
    }
}

void EditBox::set_foreground(std::optional<ui::Color> color)
{
    foreground_ = color;
    if (auto* handle = native_handle())
    {
        InvalidateRect(static_cast<HWND>(handle), nullptr, TRUE);
    }
}

void EditBox::set_background(std::optional<ui::Color> color)
{
    background_ = color;
    if (auto* handle = native_handle())
    {
        InvalidateRect(static_cast<HWND>(handle), nullptr, TRUE);
    }
}

std::pair<std::size_t, std::size_t> EditBox::selection() const
{
    auto* handle = native_handle();
    if (!handle)
    {
        return {0, 0};
    }
    DWORD start = 0;
    DWORD end   = 0;
    SendMessageW(static_cast<HWND>(handle), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    return {start, end};
}

void EditBox::select(std::size_t start, std::size_t end)
{
    if (auto* handle = native_handle())
    {
        HWND hwnd = static_cast<HWND>(handle);
        SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
        SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    }
}

void EditBox::select_all()
{
    if (auto* handle = native_handle())
    {
        SendMessageW(static_cast<HWND>(handle), EM_SETSEL, 0, static_cast<LPARAM>(-1));
    }
}

void EditBox::focus()
{
    if (auto* handle = native_handle())
    {
        SetFocus(static_cast<HWND>(handle));
    }
}

ui::Size EditBox::measure_override(ui::Size available, const ui::MeasureContext& context) const
{
    const ui::Size header = header_.measure(available.width, context);
    return ui::ControlHeader::above(native_size(), header);
}

void EditBox::arrange_override(const ui::Rect&, const ui::ArrangeContext&)
{
    header_.arrange(*this); // the window goes where the box is, below the header
}

void EditBox::render_override(ui::Renderer& renderer, const ui::RenderContext& context) const
{
    const ui::Theme& theme = context.theme;
    header_.render(renderer, theme, enabled_);

    // The dark fill is the Fluent TextBox rest fill over the base background, made opaque
    // because GDI paints with a solid brush.
    const bool      dark = theme.dark;
    const ui::Color fill = over(theme.palette.control_fill_default,
                                over(theme.palette.solid_background_base, ui::colors::Black));
    const ui::Color text = over(theme.palette.text_fill_primary, fill);
    if (theme_seen_ && dark == dark_ && fill == theme_fill_ && text == theme_text_)
    {
        return;
    }
    theme_seen_ = true;
    dark_       = dark;
    theme_fill_ = fill;
    theme_text_ = text;
    if (auto* handle = native_handle())
    {
        apply_theme(handle);
    }
}

void EditBox::sync_native(ui::NativeHost& host)
{
    ui::NativeControl::sync_native(host);
    // The window moved to a monitor with another scale: the font was made for the old one.
    if (auto* handle = native_handle(); handle && font_ && GetDpiForWindow(static_cast<HWND>(handle)) != font_dpi_)
    {
        apply_font(handle);
    }
}

bool EditBox::handle_native_notification(ui::NativeNotification& notification)
{
    // An enabled, editable EDIT asks for WM_CTLCOLOREDIT; a read-only or disabled one for
    // WM_CTLCOLORSTATIC, whose system answer is the dialog color.
    if (notification.message != WM_CTLCOLOREDIT && notification.message != WM_CTLCOLORSTATIC)
    {
        return false;
    }
    const bool               editable   = notification.message == WM_CTLCOLOREDIT;
    std::optional<ui::Color> foreground = foreground_;
    std::optional<ui::Color> background = background_;
    if (dark_)
    {
        foreground = foreground.value_or(theme_text_);
        background = background.value_or(theme_fill_);
    }
    if (!foreground && !background)
    {
        return false; // the system's colors
    }

    HDC dc = reinterpret_cast<HDC>(notification.wparam);
    if (foreground && enabled_)
    {
        SetTextColor(dc, to_colorref(*foreground));
    }
    if (!background)
    {
        // Only the text color is ours: the system's fill, as DefWindowProc would have answered.
        const int system = editable ? COLOR_WINDOW : COLOR_3DFACE;
        SetBkColor(dc, GetSysColor(system));
        notification.result = reinterpret_cast<std::intptr_t>(GetSysColorBrush(system));
        return true;
    }
    const ui::Color fill = over(*background, ui::colors::White);
    if (!brush_ || !(brush_color_ == fill))
    {
        if (brush_)
        {
            DeleteObject(static_cast<HBRUSH>(brush_));
        }
        brush_       = CreateSolidBrush(to_colorref(fill));
        brush_color_ = fill;
    }
    SetBkColor(dc, to_colorref(fill));
    notification.result = reinterpret_cast<std::intptr_t>(brush_);
    return true;
}

void EditBox::on_native_content_changed()
{
    if (on_text_changed_)
    {
        on_text_changed_();
    }
}

bool EditBox::handle_context_menu(std::optional<ui::Point> position, ui::ContextMenuHost& host)
{
    auto* handle = native_handle();
    if (!handle)
    {
        return false;
    }
    HWND hwnd = static_cast<HWND>(handle);

    // EDIT moves the caret on WM_RBUTTONDOWN, before the forwarded WM_CONTEXTMENU, so this
    // selection is the one the user expects the items to act on.
    DWORD selection_start = 0;
    DWORD selection_end   = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start), reinterpret_cast<LPARAM>(&selection_end));
    const bool has_selection = selection_start != selection_end;
    const bool read_only     = (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY) != 0;
    const bool has_text      = GetWindowTextLengthW(hwnd) > 0;
    const bool can_undo      = !read_only && SendMessageW(hwnd, EM_CANUNDO, 0, 0) != 0;
    // A password box never hands its text out (the class refuses WM_COPY/WM_CUT itself).
    const bool can_copy = has_selection && !options_.password;
    // No EM_CANPASTE on this class (that one is RichEdit's), so ask the clipboard directly for
    // the only format a plain EDIT accepts.
    const bool can_paste = !read_only && IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;

    enum class Command
    {
        None,
        Undo,
        Cut,
        Copy,
        Paste,
        Delete,
        SelectAll,
    };
    std::vector<ui::ContextMenuItem> items;
    std::vector<Command>             commands;
    const auto                       add = [&](std::string text, bool enabled, Command command)
    {
        items.push_back(ui::ContextMenuItem{std::move(text), enabled, false});
        commands.push_back(command);
    };
    const auto separator = [&]
    {
        items.push_back(ui::ContextMenuItem::separator());
        commands.push_back(Command::None);
    };

    // RichEditBox's list minus Redo and spelling: EDIT keeps one undo step and cannot show squiggles.
    add("Undo", can_undo, Command::Undo);
    separator();
    add("Cut", can_copy && !read_only, Command::Cut);
    add("Copy", can_copy, Command::Copy);
    add("Paste", can_paste, Command::Paste);
    add("Delete", has_selection && !read_only, Command::Delete);
    separator();
    add("Select All", has_text, Command::SelectAll);

    // Keyboard-invoked (Shift+F10 or the Menu key): anchor just inside the control's top-left.
    const ui::Point anchor = position ? *position : ui::Point{bounds().x + 8.0f, bounds().y + 8.0f};

    const std::optional<std::size_t> choice = host.show(anchor, items);
    if (!choice || *choice >= commands.size())
    {
        return true; // a menu *was* shown; the user dismissed it
    }
    switch (commands[*choice])
    {
        case Command::Undo:
            SendMessageW(hwnd, WM_UNDO, 0, 0);
            break;
        case Command::Cut:
            SendMessageW(hwnd, WM_CUT, 0, 0);
            break;
        case Command::Copy:
            SendMessageW(hwnd, WM_COPY, 0, 0);
            break;
        case Command::Paste:
            SendMessageW(hwnd, WM_PASTE, 0, 0);
            break;
        case Command::Delete:
            SendMessageW(hwnd, WM_CLEAR, 0, 0);
            break;
        case Command::SelectAll:
            SendMessageW(hwnd, EM_SETSEL, 0, static_cast<LPARAM>(-1));
            break;
        case Command::None:
            break;
    }
    return true;
}

} // namespace native_controls
