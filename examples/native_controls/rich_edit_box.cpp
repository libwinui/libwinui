// SPDX-License-Identifier: MIT
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "rich_edit_box.hpp"

#include "strings.hpp"
#include "winui/core/Utf8.hpp"

#include <algorithm>
#include <richedit.h>
#include <richole.h> // IRichEditOle: EM_GETOLEINTERFACE, on the way to ITextDocument
#include <string>
#include <string_view>
#include <tom.h> // ITextDocument::Undo(tomSuspend), keeps squiggles off the undo stack
#include <vector>
#include <windows.h>

#include <cmath>
#include <cstring>

namespace native_controls
{
namespace
{

// MSFTEDIT.DLL registers RICHEDIT50W when it is loaded and has no init call like
// InitCommonControlsEx, so it is loaded once, before the first RichEditBox.
void ensure_richedit_loaded()
{
    static const HMODULE module = LoadLibraryW(L"Msftedit.dll");
    (void)module;
}

ui::NativeControlDesc make_desc(std::string initial_text)
{
    ensure_richedit_loaded();
    ui::NativeControlDesc desc;
    desc.class_name   = "RICHEDIT50W";
    desc.initial_text = std::move(initial_text);
    desc.style        = WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    return desc;
}

// Pure red: RichEdit snaps an RTF underline color onto a fixed 16-color palette and drops
// anything else, so this cannot be a themed error red.
constexpr const char* SquiggleColorTable = "{\\colortbl ;\\red255\\green0\\blue0;}";

// SFF_SELECTION replaces the selection with this fragment, so it must reproduce the word
// exactly, or it edits the document and shifts every later offset.
std::string escape_rtf(std::wstring_view text)
{
    std::string out;
    out.reserve(text.size() * 2);
    for (wchar_t ch : text)
    {
        if (ch == L'\\' || ch == L'{' || ch == L'}')
        {
            out += '\\';
            out += static_cast<char>(ch);
        }
        else if (ch >= 0x20 && ch < 0x7F)
        {
            out += static_cast<char>(ch);
        }
        else
        {
            out += "\\u";
            out += std::to_string(static_cast<int>(static_cast<short>(ch)));
            out += '?'; // the single fallback character RichEdit skips (RTF's default \uc1)
        }
    }
    return out;
}

// The control's text in its own positions: UTF-16, every paragraph mark one '\r'.
std::wstring raw_text_wide(HWND hwnd)
{
    GETTEXTLENGTHEX length_request{};
    length_request.flags    = GTL_NUMCHARS | GTL_PRECISE;
    length_request.codepage = 1200; // UTF-16, matching every offset the control reports
    const auto length       = static_cast<LONG>(
        SendMessageW(hwnd, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length_request), 0));
    if (length <= 0)
    {
        return {};
    }

    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    GETTEXTEX    text_request{};
    text_request.cb       = static_cast<DWORD>((length + 1) * sizeof(wchar_t));
    text_request.flags    = GT_DEFAULT; // not GT_USECRLF, see raw_text() in the header
    text_request.codepage = 1200;
    const auto copied     = static_cast<LONG>(
        SendMessageW(hwnd, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&text_request), reinterpret_cast<LPARAM>(buffer.data())));
    buffer.resize(static_cast<std::size_t>(copied < 0 ? 0 : copied));
    return buffer;
}

// A byte offset of the UTF-8 `text` as the control's UTF-16 position, and back.
LONG to_position(std::string_view text, std::size_t offset)
{
    return static_cast<LONG>(ui::utf16_length(text.substr(0, std::min(offset, text.size()))));
}

std::size_t to_offset(std::string_view text, LONG position)
{
    return ui::utf8_offset_from_utf16(text, static_cast<std::size_t>(std::max<LONG>(0, position)));
}

// EM_STREAMIN pulls its bytes through this rather than taking a buffer.
struct RtfSource
{
    const char* data = nullptr;
    std::size_t size = 0;
    std::size_t pos  = 0;
};

DWORD CALLBACK read_rtf(DWORD_PTR cookie, LPBYTE buffer, LONG wanted, LONG* written)
{
    auto*             source    = reinterpret_cast<RtfSource*>(cookie);
    const std::size_t remaining = source->size - source->pos;
    const std::size_t take      = std::min(remaining, static_cast<std::size_t>(wanted));
    std::memcpy(buffer, source->data + source->pos, take);
    source->pos += take;
    *written = static_cast<LONG>(take);
    return 0;
}

// FR_DOWN (commdlg.h, which WIN32_LEAN_AND_MEAN leaves out): search forward.
constexpr WPARAM FindDown = 0x00000001;

// EM_STREAMOUT pushes its bytes through this.
DWORD CALLBACK write_rtf(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* written)
{
    auto* out = reinterpret_cast<std::string*>(cookie);
    out->append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(count));
    *written = count;
    return 0;
}

COLORREF to_colorref(const ui::Color& c)
{
    const auto byte = [](float v) { return static_cast<BYTE>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return RGB(byte(c.r), byte(c.g), byte(c.b));
}

ui::Color from_colorref(COLORREF c)
{
    return ui::Color{GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f};
}

CHARFORMAT2W to_charformat(const RichEditBox::CharacterFormat& format)
{
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    if (format.bold)
    {
        cf.dwMask |= CFM_BOLD;
        cf.dwEffects |= *format.bold ? CFE_BOLD : 0;
    }
    if (format.italic)
    {
        cf.dwMask |= CFM_ITALIC;
        cf.dwEffects |= *format.italic ? CFE_ITALIC : 0;
    }
    if (format.underline)
    {
        cf.dwMask |= CFM_UNDERLINE;
        cf.dwEffects |= *format.underline ? CFE_UNDERLINE : 0;
    }
    // A fully transparent color is the control's own (CFE_AUTOCOLOR / CFE_AUTOBACKCOLOR).
    if (format.foreground)
    {
        cf.dwMask |= CFM_COLOR;
        if (format.foreground->a <= 0.0f)
        {
            cf.dwEffects |= CFE_AUTOCOLOR;
        }
        cf.crTextColor = to_colorref(*format.foreground);
    }
    if (format.background)
    {
        cf.dwMask |= CFM_BACKCOLOR;
        if (format.background->a <= 0.0f)
        {
            cf.dwEffects |= CFE_AUTOBACKCOLOR;
        }
        cf.crBackColor = to_colorref(*format.background);
    }
    return cf;
}

} // namespace

RichEditBox::RichEditBox(ui::Size size, std::string initial_text) :
    ui::NativeControl(make_desc(std::move(initial_text)), size)
{
}

std::pair<std::size_t, std::size_t> RichEditBox::selection() const
{
    auto* handle = native_handle();
    if (!handle)
    {
        return {0, 0};
    }
    CHARRANGE range{};
    SendMessageW(static_cast<HWND>(handle), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    const std::string text = raw_text(handle);
    return {to_offset(text, range.cpMin), to_offset(text, range.cpMax)};
}

void RichEditBox::select(std::size_t start, std::size_t end)
{
    if (auto* handle = native_handle())
    {
        const std::string text = raw_text(handle);
        CHARRANGE         range{to_position(text, start), to_position(text, end)};
        SendMessageW(static_cast<HWND>(handle), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    }
}

RichEditBox::CharacterFormat RichEditBox::selection_format() const
{
    CharacterFormat format;
    auto*           handle = native_handle();
    if (!handle)
    {
        return format;
    }
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    SendMessageW(static_cast<HWND>(handle), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    // A mask bit is set only where the whole selection agrees.
    if (cf.dwMask & CFM_BOLD)
    {
        format.bold = (cf.dwEffects & CFE_BOLD) != 0;
    }
    if (cf.dwMask & CFM_ITALIC)
    {
        format.italic = (cf.dwEffects & CFE_ITALIC) != 0;
    }
    if (cf.dwMask & CFM_UNDERLINE)
    {
        format.underline = (cf.dwEffects & CFE_UNDERLINE) != 0;
    }
    if ((cf.dwMask & CFM_COLOR) && !(cf.dwEffects & CFE_AUTOCOLOR))
    {
        format.foreground = from_colorref(cf.crTextColor);
    }
    if ((cf.dwMask & CFM_BACKCOLOR) && !(cf.dwEffects & CFE_AUTOBACKCOLOR))
    {
        format.background = from_colorref(cf.crBackColor);
    }
    return format;
}

void RichEditBox::set_selection_format(const CharacterFormat& format)
{
    if (auto* handle = native_handle())
    {
        CHARFORMAT2W cf = to_charformat(format);
        SendMessageW(static_cast<HWND>(handle), EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }
}

void RichEditBox::set_range_format(std::size_t start, std::size_t end, const CharacterFormat& format)
{
    auto* handle = native_handle();
    if (!handle)
    {
        return;
    }
    HWND hwnd = static_cast<HWND>(handle);
    // Formatting a range means selecting it; the user's own selection is put back, unseen.
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    CHARRANGE saved{};
    SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&saved));
    const std::string text = raw_text(handle);
    CHARRANGE         range{to_position(text, start), to_position(text, end)};
    SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W cf = to_charformat(format);
    SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&saved));
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void RichEditBox::toggle_bold()
{
    const CharacterFormat current = selection_format();
    set_selection_format(CharacterFormat{.bold = !current.bold.value_or(false)});
}

void RichEditBox::toggle_italic()
{
    const CharacterFormat current = selection_format();
    set_selection_format(CharacterFormat{.italic = !current.italic.value_or(false)});
}

std::vector<std::pair<std::size_t, std::size_t>> RichEditBox::find_all(std::string_view text) const
{
    std::vector<std::pair<std::size_t, std::size_t>> matches;
    auto*                                            handle = native_handle();
    if (!handle || text.empty())
    {
        return matches;
    }
    const std::string  content = raw_text(handle);
    const std::wstring needle  = widen(text);
    FINDTEXTEXW        find{};
    find.chrg.cpMin = 0;
    find.chrg.cpMax = -1;
    find.lpstrText  = needle.c_str();
    for (;;)
    {
        const auto at = static_cast<LONG>(
            SendMessageW(static_cast<HWND>(handle), EM_FINDTEXTEXW, FindDown, reinterpret_cast<LPARAM>(&find)));
        if (at < 0 || find.chrgText.cpMax <= at)
        {
            break;
        }
        matches.emplace_back(to_offset(content, find.chrgText.cpMin), to_offset(content, find.chrgText.cpMax));
        find.chrg.cpMin = find.chrgText.cpMax;
    }
    return matches;
}

void RichEditBox::load_rtf(std::string_view rtf)
{
    if (auto* handle = native_handle())
    {
        RtfSource  source{rtf.data(), rtf.size(), 0};
        EDITSTREAM stream{};
        stream.dwCookie    = reinterpret_cast<DWORD_PTR>(&source);
        stream.pfnCallback = read_rtf;
        SendMessageW(static_cast<HWND>(handle), EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
        spell_dirty_ = true;
    }
}

std::string RichEditBox::save_rtf() const
{
    std::string out;
    if (auto* handle = native_handle())
    {
        EDITSTREAM stream{};
        stream.dwCookie    = reinterpret_cast<DWORD_PTR>(&out);
        stream.pfnCallback = write_rtf;
        SendMessageW(static_cast<HWND>(handle), EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    }
    return out;
}

void RichEditBox::focus()
{
    if (auto* handle = native_handle())
    {
        SetFocus(static_cast<HWND>(handle));
    }
}

void RichEditBox::add_context_menu_item(std::string text, std::function<void()> on_click)
{
    extra_menu_items_.push_back(ExtraMenuItem{std::move(text), std::move(on_click)});
}

void RichEditBox::set_text(const std::string& text)
{
    if (auto* handle = native_handle())
    {
        const std::wstring wide = widen(text);
        SendMessageW(static_cast<HWND>(handle), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
}

std::string RichEditBox::text() const
{
    auto* handle = native_handle();
    if (!handle)
    {
        return {};
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

// ---- spell checking ---------------------------------------------------------------

void RichEditBox::on_native_created(ui::NativeHandle handle)
{
    HWND hwnd = static_cast<HWND>(handle);

    // Without advanced typography RichEdit draws CFU_UNDERLINEWAVE as a straight CFU_UNDERLINE.
    SendMessageW(hwnd, EM_SETTYPOGRAPHYOPTIONS, TO_ADVANCEDTYPOGRAPHY, TO_ADVANCEDTYPOGRAPHY);

    // EN_CHANGE is the only way the framework sees an edit, since keys go straight to this HWND.
    // Added to the control's own event mask rather than replacing it.
    const auto mask = static_cast<DWORD>(SendMessageW(hwnd, EM_GETEVENTMASK, 0, 0));
    SendMessageW(hwnd, EM_SETEVENTMASK, 0, static_cast<LPARAM>(mask | ENM_CHANGE));
}

void RichEditBox::set_spell_check_enabled(bool enabled)
{
    if (enabled == spell_check_enabled_)
    {
        return;
    }
    spell_check_enabled_ = enabled;
    spell_dirty_         = true;
    // Turning it off runs a pass too: that is what clears the marks already drawn.
    run_spell_check();
}

void RichEditBox::sync_spell_checker(ui::SpellChecker& checker)
{
    const bool new_checker = spell_checker_ != &checker;
    spell_checker_         = &checker;
    // generation() moves when a word is added or ignored anywhere, or the language changes,
    // which is how "Add to dictionary" in some other field clears a squiggle here too.
    if (new_checker || spell_dirty_ || spell_generation_ != checker.generation())
    {
        run_spell_check();
    }
}

void RichEditBox::on_native_content_changed()
{
    spell_dirty_ = true;
    if (on_text_changed_)
    {
        on_text_changed_();
    }
}

std::string RichEditBox::raw_text(void* handle) const
{
    return narrow(raw_text_wide(static_cast<HWND>(handle)));
}

void RichEditBox::run_spell_check()
{
    spell_dirty_ = false;
    void* handle = native_handle();
    if (!handle)
    {
        return; // no window yet; the first sync after creation runs this again
    }

    std::vector<ui::SpellingError> found;
    std::string                    text;
    if (spell_checker_)
    {
        spell_generation_ = spell_checker_->generation();
        // highlight_enabled() is the user's own "Highlight misspelled words" switch; a field
        // opting out through set_spell_check_enabled(false) is the app's separate say.
        if (spell_check_enabled_ && spell_checker_->available() && spell_checker_->highlight_enabled())
        {
            text = raw_text(handle);
            if (!text.empty())
            {
                found = spell_checker_->check(text);
            }
        }
    }

    if (found == spelling_errors_ && text == checked_text_)
    {
        return; // typing inside an already-correct word changes neither: don't touch the doc
    }
    spelling_errors_ = std::move(found);
    checked_text_    = std::move(text);
    apply_spelling_marks(handle);
}

const ui::SpellingError* RichEditBox::spelling_error_at(std::size_t index) const
{
    for (const ui::SpellingError& error : spelling_errors_)
    {
        if (error.start <= index && index <= error.start + error.length)
        {
            return &error;
        }
    }
    return nullptr;
}

void RichEditBox::apply_spelling_marks(void* handle) const
{
    HWND hwnd = static_cast<HWND>(handle);

    // Formatting is an undoable edit in RichEdit; ITextDocument::Undo(tomSuspend) keeps the marks
    // off the undo stack. Best effort: without the OLE interface the marks still draw.
    IRichEditOle* ole = nullptr;
    SendMessageW(hwnd, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole));
    ITextDocument* document = nullptr;
    if (ole)
    {
        ole->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void**>(&document));
    }
    long undo_count = 0;
    if (document)
    {
        document->Undo(tomSuspend, &undo_count);
    }

    // Silence EN_CHANGE for the duration, or applying the marks re-enters as "the text
    // changed" and schedules the very pass that is running.
    const auto events = static_cast<DWORD>(SendMessageW(hwnd, EM_SETEVENTMASK, 0, 0));
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    CHARRANGE saved_selection{};
    SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&saved_selection));

    // Clear wholesale: RichEdit carries formatting forward at the insertion point, so text typed
    // onto a marked word inherits the wave.
    {
        CHARRANGE all{0, -1};
        SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        CHARFORMAT2W format{};
        format.cbSize         = sizeof(format);
        format.dwMask         = CFM_UNDERLINETYPE;
        format.bUnderlineType = CFU_UNDERLINENONE;
        SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    }

    // The red wave has to arrive as RTF, because RichEdit 8 discards CHARFORMAT2W::bUnderlineColor.
    // The fragment carries no font, size or color, so the word keeps its own.
    const auto mark = [&](LONG from, LONG to, std::string_view word)
    {
        CHARRANGE range{from, to};
        SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));

        const std::string rtf = std::string{"{\\rtf1\\ansi"} + SquiggleColorTable + "\\ulwave\\ulc1 " +
                                escape_rtf(widen(word)) + "}";
        RtfSource         source{rtf.c_str(), rtf.size(), 0};
        EDITSTREAM        stream{};
        stream.dwCookie    = reinterpret_cast<DWORD_PTR>(&source);
        stream.pfnCallback = read_rtf;
        SendMessageW(hwnd, EM_STREAMIN, SF_RTF | SFF_SELECTION, reinterpret_cast<LPARAM>(&stream));
    };

    // Last error first, so a mark that changes length never shifts the offsets still to be marked.
    for (auto error = spelling_errors_.rbegin(); error != spelling_errors_.rend(); ++error)
    {
        const std::size_t end = error->start + error->length;
        if (end > checked_text_.size())
        {
            continue; // the document moved under the error list; the next pass re-checks it
        }
        mark(to_position(checked_text_, error->start),
             to_position(checked_text_, end),
             std::string_view{checked_text_}.substr(error->start, error->length));
    }

    SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&saved_selection));
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd, nullptr, TRUE);
    SendMessageW(hwnd, EM_SETEVENTMASK, 0, static_cast<LPARAM>(events));
    if (document)
    {
        document->Undo(tomResume, &undo_count);
        document->Release();
    }
    if (ole)
    {
        ole->Release();
    }
}

bool RichEditBox::handle_context_menu(std::optional<ui::Point> position, ui::ContextMenuHost& host)
{
    auto* handle = native_handle();
    if (!handle)
    {
        return false;
    }
    HWND hwnd = static_cast<HWND>(handle);

    // RichEdit moves the caret on WM_RBUTTONDOWN, before the menu opens, so this selection is
    // the one the user expects the items to act on.
    CHARRANGE selection{};
    SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    const bool has_selection = selection.cpMin != selection.cpMax;
    const bool read_only     = (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY) != 0;
    const bool has_text      = GetWindowTextLengthW(hwnd) > 0;
    const bool can_undo      = !read_only && SendMessageW(hwnd, EM_CANUNDO, 0, 0) != 0;
    const bool can_redo      = !read_only && SendMessageW(hwnd, EM_CANREDO, 0, 0) != 0;
    // 0 asks "can anything be pasted right now?", for every format the control accepts and
    // its read-only state too.
    const bool can_paste = SendMessageW(hwnd, EM_CANPASTE, 0, 0) != 0;

    enum class Command
    {
        None,
        Suggestion,
        AddToDictionary,
        Ignore,
        Undo,
        Redo,
        Cut,
        Copy,
        Paste,
        Delete,
        SelectAll,
        Extra,
    };
    std::vector<ui::ContextMenuItem>             items;
    std::vector<std::pair<Command, std::size_t>> commands; // parallel to items; size_t: suggestion index
    const auto add = [&](std::string text, bool enabled, Command command, std::size_t payload = 0)
    {
        items.push_back(ui::ContextMenuItem{std::move(text), enabled, false});
        commands.emplace_back(command, payload);
    };
    const auto separator = [&]
    {
        items.push_back(ui::ContextMenuItem::separator());
        commands.emplace_back(Command::None, 0);
    };

    // The click is in window-client DIPs; EM_CHARFROMPOS wants the control's client pixels.
    // A keyboard-invoked menu has no point and uses the caret.
    std::optional<ui::SpellingError> error;
    std::string                      word;
    std::vector<std::string>         suggestions;
    if (spell_checker_ && spell_check_enabled_ && !spelling_errors_.empty())
    {
        LONG index = -1;
        if (position)
        {
            const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
            POINTL      point{static_cast<LONG>((position->x - bounds().x) * scale),
                              static_cast<LONG>((position->y - bounds().y) * scale)};
            index = static_cast<LONG>(SendMessageW(hwnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&point)));
        }
        else
        {
            index = selection.cpMax;
        }
        if (index >= 0)
        {
            if (const ui::SpellingError* hit = spelling_error_at(to_offset(checked_text_, index)))
            {
                error = *hit;
            }
        }
    }
    if (error && error->start + error->length <= checked_text_.size())
    {
        word = checked_text_.substr(error->start, error->length);
        if (error->action == ui::SpellingAction::Replace && !error->replacement.empty())
        {
            suggestions.push_back(error->replacement);
        }
        for (std::string& suggestion : spell_checker_->suggest(word))
        {
            if (suggestions.size() >= MaxSpellingSuggestions)
            {
                break;
            }
            if (std::find(suggestions.begin(), suggestions.end(), suggestion) == suggestions.end())
            {
                suggestions.push_back(std::move(suggestion));
            }
        }
        for (std::size_t i = 0; i < suggestions.size(); ++i)
        {
            add(suggestions[i], true, Command::Suggestion, i);
        }
        if (suggestions.empty())
        {
            add("(No spelling suggestions)", false, Command::None);
        }
        separator();
        add("Add to dictionary", true, Command::AddToDictionary);
        add("Ignore", true, Command::Ignore);
        separator();
    }
    else
    {
        error.reset(); // a hit whose range no longer fits the text this menu is built over
    }

    // Same wording and order as TextBox's menu, plus Undo/Redo (RichEdit keeps its own undo
    // stack) and Delete.
    add("Undo", can_undo, Command::Undo);
    add("Redo", can_redo, Command::Redo);
    separator();
    add("Cut", has_selection && !read_only, Command::Cut);
    add("Copy", has_selection, Command::Copy);
    add("Paste", can_paste, Command::Paste);
    add("Delete", has_selection && !read_only, Command::Delete);
    separator();
    add("Select All", has_text, Command::SelectAll);
    if (!extra_menu_items_.empty())
    {
        separator();
        for (std::size_t i = 0; i < extra_menu_items_.size(); ++i)
        {
            add(extra_menu_items_[i].text, true, Command::Extra, i);
        }
    }

    // Keyboard-invoked (Shift+F10 or the Menu key): anchor inside the top-left, not at the caret,
    // which GetCaretPos only gives in device pixels with no DPI scale at hand.
    const ui::Point anchor = position ? *position : ui::Point{bounds().x + 8.0f, bounds().y + 8.0f};

    const std::optional<std::size_t> choice = host.show(anchor, items);
    if (!choice || *choice >= commands.size())
    {
        return true; // a menu *was* shown; the user dismissed it
    }
    const auto [command, payload] = commands[*choice];
    switch (command)
    {
        case Command::Suggestion:
        {
            // Through the control rather than around it: EM_REPLACESEL with wParam TRUE puts the
            // correction on the RichEdit's own undo stack, so the Undo item above covers it.
            const std::wstring replacement = widen(suggestions[payload]);
            CHARRANGE          range{to_position(checked_text_, error->start),
                                     to_position(checked_text_, error->start + error->length)};
            SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));
            spell_dirty_ = true;
            break;
        }
        case Command::AddToDictionary:
            spell_checker_->add_to_dictionary(word);
            spell_dirty_ = true; // generation() also moves, but say it outright
            break;
        case Command::Ignore:
            spell_checker_->ignore_for_session(word);
            spell_dirty_ = true;
            break;
        case Command::Undo:
            SendMessageW(hwnd, WM_UNDO, 0, 0);
            break;
        case Command::Redo:
            SendMessageW(hwnd, EM_REDO, 0, 0);
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
        case Command::Extra:
            if (payload < extra_menu_items_.size() && extra_menu_items_[payload].on_click)
            {
                const std::function<void()> handler = extra_menu_items_[payload].on_click;
                handler();
            }
            break;
        case Command::None:
            break;
    }
    return true;
}

} // namespace native_controls
