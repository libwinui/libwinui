// SPDX-License-Identifier: MIT
// These controls have no spec: a NativeControl lives outside the portable core, so it enters a
// declared tree as a plain `std::unique_ptr<Element>` through `ui::Spec`.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "edit_box.hpp"
#include "gallery.hpp"
#include "rich_edit_box.hpp"

#include <array>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include <cmath>
#include <commctrl.h>
#include <cstdint>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- EditBox ----

constexpr std::array<const char*, 3> Alignments{"Left", "Center", "Right"};
constexpr std::array<const char*, 3> Casings{"Normal", "Lower", "Upper"};

struct EditBoxState
{
    native_controls::EditBox* password = nullptr;

    // The styles card rebuilds its box when the alignment changes, since the class reads ES_CENTER
    // and ES_RIGHT only at creation.
    native_controls::EditBox* styled = nullptr;
    ui::Property<std::size_t> alignment{0};
    std::string               text;
    bool                      read_only   = false;
    bool                      disabled    = false;
    bool                      number_only = false;
    ui::CharacterCasing       casing      = ui::CharacterCasing::Normal;
    int                       max_length  = 0;
    ui::Property<std::string> output{"Text: (empty)"};
};

void report(EditBoxState& s)
{
    s.output = s.text.empty() ? std::string{"Text: (empty)"}
                              : std::format("Text: {} ({} characters)", s.text, ui::code_point_count(s.text));
}

ui::Spec styled_box(EditBoxState& s, std::size_t alignment)
{
    native_controls::EditBoxOptions options;
    options.accepts_return = false;
    options.alignment      = alignment == 1   ? ui::TextAlignment::Center
                             : alignment == 2 ? ui::TextAlignment::Right
                                              : ui::TextAlignment::Left;
    auto box               = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, s.text, options);
    box->set_header("Type  here:");
    box->set_read_only(s.read_only);
    box->set_enabled(!s.disabled);
    box->set_number_only(s.number_only);
    box->set_character_casing(s.casing);
    box->set_max_length(s.max_length);
    native_controls::EditBox* raw = box.get();
    box->on_text_changed(
        [&s, raw]
        {
            s.text = raw->text();
            report(s);
        });
    s.styled = raw;
    return std::unique_ptr<ui::Element>(std::move(box));
}

ui::Spec edit_box_page(Context&)
{
    return with_state<EditBoxState>(
        [](EditBoxState& s)
        {
            native_controls::EditBoxOptions single;
            single.accepts_return = false;

            auto simple = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", single);

            auto named = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", single);
            named->set_header("Enter your name:");
            named->set_placeholder_text("Name");

            auto customized = std::make_unique<native_controls::EditBox>(ui::Size{420.0f, 0.0f},
                                                                         "I am super excited to be here!",
                                                                         single);
            customized->set_read_only(true);
            customized->set_font_family("Arial");
            customized->set_font_size(24.0f);
            customized->set_italic(true);
            customized->set_foreground(ui::Color::from_argb(0xFF5178BE));

            // An EDIT does not grow as lines are added; it keeps its size and
            // scrolls instead.
            auto multi_line = std::make_unique<native_controls::EditBox>(ui::Size{400.0f, 120.0f});

            native_controls::EditBoxOptions secret;
            secret.password = true;
            auto password   = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", secret);
            password->set_header("Password");
            password->set_placeholder_text("Enter your password");
            password->set_password_char('#');
            s.password = password.get();

            return examples({
                example({
                    .title    = "A simple EditBox.",
                    .code     = R"cpp(
                        // native_controls::EditBox (examples/native_controls/edit_box.hpp) is a
                        // ui::NativeControl around the EDIT class. A height of 0 is one line.
                        native_controls::EditBoxOptions options;
                        options.accepts_return = false;

                        auto box = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", options);

                        // It has no spec: the element itself goes into the declared tree.
                        ui::VerticalStack{
                            .content = {std::move(box)},
                        }
                    )cpp",
                    .controls = {std::move(simple)},
                }),
                example({
                    .title    = "An EditBox with a header and placeholder text.",
                    .code     = R"cpp(
                        native_controls::EditBoxOptions options;
                        options.accepts_return = false;

                        auto box = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", options);
                        box->set_header("Enter your name:");
                        box->set_placeholder_text("Name");

                        ui::VerticalStack{
                            .content = {std::move(box)},
                        }
                    )cpp",
                    .controls = {std::move(named)},
                }),
                example({
                    .title    = "A read-only EditBox with various properties set.",
                    .code     = R"cpp(
                        native_controls::EditBoxOptions options;
                        options.accepts_return = false;

                        auto box = std::make_unique<native_controls::EditBox>(ui::Size{420.0f, 0.0f},
                                                                              "I am super excited to be here!",
                                                                              options);
                        box->set_read_only(true);
                        box->set_font_family("Arial");
                        box->set_font_size(24.0f);
                        box->set_italic(true);
                        box->set_foreground(ui::Color::from_argb(0xFF5178BE));

                        ui::VerticalStack{
                            .content = {std::move(box)},
                        }
                    )cpp",
                    .controls = {std::move(customized)},
                }),
                example({
                    .title    = "A multi-line EditBox.",
                    .code     = R"cpp(
                        // Multi-line by default. The window keeps its size and scrolls as lines are added.
                        auto box = std::make_unique<native_controls::EditBox>(ui::Size{400.0f, 120.0f});

                        ui::VerticalStack{
                            .content = {std::move(box)},
                        }
                    )cpp",
                    .controls = {std::move(multi_line)},
                }),
                example({
                    .title   = "A password EditBox with a header, placeholder text and custom character.",
                    .code    = R"cpp(
                        native_controls::EditBoxOptions options;
                        options.password = true;

                        auto box = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", options);
                        box->set_header("Password");
                        box->set_placeholder_text("Enter your password");
                        box->set_password_char('#');

                        // The tree owns the box; the handler keeps a plain pointer to it.
                        native_controls::EditBox* password = box.get();

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                std::move(box),
                                ui::CheckBox{.text               = "Show password",
                                             .on_checked_changed = [password](std::optional<bool> on)
                                             { password->set_password_revealed(on == true); }},
                            },
                        }
                    )cpp",
                    .options = ui::CheckBox{.text               = "Show password",
                                            .on_checked_changed = [&s](std::optional<bool> on)
                                            { s.password->set_password_revealed(on == true); }},
                    .controls = {std::move(password)},
                }),
                example({
                    .title  = "The EDIT class's own styles.",
                    .code   = R"cpp(
                        ui::Property<std::size_t> alignment{0}; // 0 left, 1 center, 2 right
                        std::string               text;
                        native_controls::EditBox* styled = nullptr;

                        // The EDIT class reads its alignment only at creation, so the box is made
                        // again when the alignment changes.
                        ui::Match<std::size_t>{
                            .value   = alignment,
                            .content = [&](const std::size_t& index) -> ui::Spec
                            {
                                native_controls::EditBoxOptions options;
                                options.accepts_return = false;
                                options.alignment      = index == 1   ? ui::TextAlignment::Center
                                                         : index == 2 ? ui::TextAlignment::Right
                                                                      : ui::TextAlignment::Left;
                                auto box = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, text, options);
                                box->set_header("Type here:");
                                box->on_text_changed([&] { text = styled->text(); });
                                styled = box.get();
                                return std::unique_ptr<ui::Element>(std::move(box));
                            },
                        }

                        // The other styles are set on the live window.
                        styled->set_read_only(true);
                        styled->set_enabled(false);
                        styled->set_number_only(true);
                        styled->set_character_casing(ui::CharacterCasing::Upper);
                        styled->set_max_length(10); // 0 is no limit
                    )cpp",
                    .output = ui::Text{.text = s.output, .horizontal_alignment = ui::Alignment::Leading},
                    .options =
                        ui::VerticalStack{
                            .spacing = 8.0f,
                            .content =
                                {
                                    ui::CheckBox{.text               = "Read only",
                                                 .on_checked_changed =
                                                     [&s](std::optional<bool> on)
                                                 {
                                                     s.read_only = on == true;
                                                     s.styled->set_read_only(s.read_only);
                                                 }},
                                    ui::CheckBox{.text               = "Disabled",
                                                 .on_checked_changed =
                                                     [&s](std::optional<bool> on)
                                                 {
                                                     s.disabled = on == true;
                                                     s.styled->set_enabled(!s.disabled);
                                                 }},
                                    ui::CheckBox{.text               = "Numbers only",
                                                 .on_checked_changed =
                                                     [&s](std::optional<bool> on)
                                                 {
                                                     s.number_only = on == true;
                                                     s.styled->set_number_only(s.number_only);
                                                 }},
                                    ui::RadioButtons{.header = "Character casing",
                                                     .items  = std::vector<std::string>(Casings.begin(), Casings.end()),
                                                     .selected_index       = 0,
                                                     .on_selection_changed = [&s](std::optional<std::size_t> index)
                                                     {
                                                         s.casing = index == 1   ? ui::CharacterCasing::Lower
                                                                    : index == 2 ? ui::CharacterCasing::Upper
                                                                                 : ui::CharacterCasing::Normal;
                                                         s.styled->set_character_casing(s.casing);
                                                     }},
                                    ui::NumberBox{.header           = "Max length (0 is none)",
                                                  .value            = 0.0,
                                                  .min              = 0.0,
                                                  .max              = 100.0,
                                                  .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                                                  .on_value_changed = [&s](std::optional<double> value)
                                                  {
                                                      s.max_length = static_cast<int>(value.value_or(0.0));
                                                      s.styled->set_max_length(s.max_length);
                                                  }},
                                    ui::ComboBox{.header = "Text alignment",
                                                 .items  = std::vector<std::string>(Alignments.begin(), Alignments.end()),
                                                 .selected_index = 0,
                                                 .on_selection_changed = [&s](std::optional<std::size_t> index) { s.alignment = index.value_or(0); }},
                                },
                        },
                    .controls =
                        {
                            ui::Match<std::size_t>{.value   = s.alignment,
                                                    .content = [&s](const std::size_t& alignment)
                                                    { return styled_box(s, alignment); }},
                        },
                }),
            });
        });
}

// ---- RichEdit ----

ui::Color system_color(int index)
{
    const COLORREF c = GetSysColor(index);
    return ui::Color{GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f};
}

// The custom editor's font colors, by name.
struct NamedColor
{
    const char* name;
    ui::Color   color;
};
const std::array<NamedColor, 8> FontColors{{
    {"Red", ui::Color::from_argb(0xFFFF0000)},
    {"Orange", ui::Color::from_argb(0xFFFFA500)},
    {"Yellow", ui::Color::from_argb(0xFFFFFF00)},
    {"Green", ui::Color::from_argb(0xFF008000)},
    {"Blue", ui::Color::from_argb(0xFF0000FF)},
    {"Indigo", ui::Color::from_argb(0xFF4B0082)},
    {"Violet", ui::Color::from_argb(0xFFEE82EE)},
    {"Gray", ui::Color::from_argb(0xFF808080)},
}};

constexpr ui::IconGlyph OpenFileGlyph{0xE8E5, "Segoe Fluent Icons", 16.0f};
constexpr ui::IconGlyph SaveGlyph{0xE74E, "Segoe Fluent Icons", 16.0f};
constexpr ui::IconGlyph BoldGlyph{0xE8DD, "Segoe Fluent Icons", 16.0f};
constexpr ui::IconGlyph ItalicGlyph{0xE8DB, "Segoe Fluent Icons", 16.0f};

struct RichEditState
{
    native_controls::RichEditBox* editor = nullptr;
    ui::Control<ui::Flyout>       color_flyout;
    ui::Control<ui::TextBox>      find_box;
    // The color last picked from the flyout; none until one is, as on the Text page.
    std::optional<ui::Color> current;
};

// The toolbar's buttons: RelativePanel.Resources' Button style (no border, transparent,
// 8 DIPs apart) is the subtle button.
ui::Spec tool_button(const char* name, ui::IconGlyph glyph, ui::Callback<void()> on_click)
{
    return ui::Button{.variant       = ui::ButtonVariant::Subtle,
                      .margin        = ui::Edges{.right = 8.0f},
                      .tool_tip      = name,
                      .accessibility = {.name = name},
                      .on_click      = std::move(on_click),
                      .content       = ui::Icon{.source = glyph}};
}

// FindBoxRemoveHighlights: the whole document back to the editor's own colors.
void remove_highlights(RichEditState& s)
{
    if (s.editor != nullptr)
    {
        s.editor->set_range_format(0,
                                   s.editor->text().size() + 1,
                                   {.foreground = ui::colors::Transparent, .background = ui::colors::Transparent});
    }
}

// FindBoxHighlightMatches: every match in SystemColorHighlight under SystemColorHighlightText.
void highlight_matches(RichEditState& s)
{
    remove_highlights(s);
    if (s.editor == nullptr || s.find_box == nullptr)
    {
        return;
    }
    const ui::Color background = system_color(COLOR_HIGHLIGHT);
    const ui::Color foreground = system_color(COLOR_HIGHLIGHTTEXT);
    for (const auto& [start, end] : s.editor->find_all(s.find_box->text()))
    {
        s.editor->set_range_format(start, end, {.foreground = foreground, .background = background});
    }
}

std::optional<std::string> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

// OpenButton_Click and SaveButton_Click: an .rtf in, an .rtf out.
ui::Spec file_buttons(RichEditState& s, ui::Window& window, ui::Property<std::string>& status)
{
    return ui::HorizontalStack{
        .content = {
            tool_button("Open file",
                        OpenFileGlyph,
                        [&s, &window]
                        {
                            if (auto path = ui::pick_file(window,
                                                          {.title = "Open",
                                                           .file_types = {{.name = "Rich Text", .extensions = {".rtf"}}}}))
                            {
                                if (auto rtf = read_file(*path))
                                {
                                    s.editor->load_rtf(*rtf);
                                }
                            }
                        }),
            tool_button("Save file",
                        SaveGlyph,
                        [&s, &window, &status]
                        {
                            auto path = ui::pick_save_file(window,
                                                           {.title = "Save",
                                                            .file_types = {{.name = "Rich Text", .extensions = {".rtf"}}},
                                                            .suggested_name = "New Document.rtf"});
                            if (!path)
                            {
                                return;
                            }
                            if (path->extension() != ".rtf")
                            {
                                *path += ".rtf";
                            }
                            if (!write_file(*path, s.editor->save_rtf()))
                            {
                                status = "RichEdit: the file couldn't be saved.";
                            }
                        }),
        }};
}

// The font color flyout: three to a row, 32x32 swatches in 6 DIPs of margin.
ui::Spec color_flyout(RichEditState& s)
{
    ui::CellList cells;
    for (std::size_t i = 0; i < FontColors.size(); ++i)
    {
        const NamedColor named = FontColors[i];
        cells.cells.push_back(
            {i / 3,
             i % 3,
             1,
             1,
             ui::build(ui::Button{.padding       = ui::Thickness{0.0f},
                                  .margin        = ui::Thickness{6.0f},
                                  .accessibility = {.name = named.name},
                                  .on_click =
                                      [&s, named]
                                  {
                                      s.editor->set_selection_format({.foreground = named.color});
                                      if (s.color_flyout != nullptr)
                                      {
                                          s.color_flyout->hide();
                                      }
                                      s.editor->focus();
                                      s.current = named.color;
                                  },
                                  .content = Swatch{.fill = named.color, .width = 32.0f, .height = 32.0f}})});
    }
    // The tooltip is the button's own: a Flyout opens only from a button that is its anchor
    // directly, and a ToolTip element around it would hide the Flyout from the popup layer.
    ui::Spec button = ui::DropdownButton{.variant       = ui::ButtonVariant::Subtle,
                                         .tool_tip      = "Font color",
                                         .accessibility = {.name = "Font color"},
                                         .content       = ui::Icon{.source = ui::symbol_glyph(ui::Symbol::FontColor)}};
    return ui::Flyout{.placement = ui::PopupPlacement::Below,
                      .control   = s.color_flyout,
                      .anchor    = std::move(button),
                      .content   = ui::Grid{.columns = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                            .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                            .content = std::move(cells)}};
}

ui::Spec rich_edit_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    auto&       status = ctx.status;
    return with_state<RichEditState>(
        [&window, &status](RichEditState& s)
        {
            auto simple = std::make_unique<native_controls::RichEditBox>(ui::Size{300.0f, 120.0f});
            simple->set_accessibility({.name = "simple text editor"});

            auto custom_menu = std::make_unique<native_controls::RichEditBox>(ui::Size{800.0f, 200.0f});
            custom_menu->set_accessibility({.name = "editor with custom menu"});
            custom_menu->add_context_menu_item("Share", [&status] { status = "RichEdit: Share"; });

            auto editor = std::make_unique<native_controls::RichEditBox>(ui::Size{300.0f, 200.0f});
            editor->set_accessibility({.name = "Custom editor"});
            editor->set_min_width(300.0f);
            s.editor = editor.get();
            // Editor_TextChanged: what is typed keeps the picked color, once there is one.
            editor->on_text_changed(
                [&s]
                {
                    if (s.current && s.editor->selection_format().foreground != *s.current)
                    {
                        s.editor->set_selection_format({.foreground = *s.current});
                    }
                });

            ui::Spec formatting = ui::HorizontalStack{
                .content = {
                    tool_button("Bold",
                                BoldGlyph,
                                [&s]
                                {
                                    s.editor->toggle_bold();
                                    s.editor->focus();
                                }),
                    tool_button("Italic",
                                ItalicGlyph,
                                [&s]
                                {
                                    s.editor->toggle_italic();
                                    s.editor->focus();
                                }),
                    color_flyout(s),
                }};
            ui::Spec find = ui::HorizontalStack{.margin  = ui::Edges{.top = 10.0f},
                                                .content = {
                                                    ui::Text{.text               = "Find:",
                                                             .margin             = ui::Edges{.bottom = 4.0f},
                                                             .vertical_alignment = ui::Alignment::Center},
                                                    ui::TextBox{.placeholder_text = "Enter search text",
                                                                .margin           = ui::Edges{.left = 10.0f},
                                                                .width            = 224.0f,
                                                                .on_text_changed  = [&s](const std::string&)
                                                                { highlight_matches(s); },
                                                                .on_got_focus  = [&s] { highlight_matches(s); },
                                                                .on_lost_focus = [&s] { remove_highlights(s); },
                                                                .control       = s.find_box},
                                                }};

            return examples({
                example({
                    .title    = "A simple text editor using RichEdit.",
                    .code     = R"cpp(
                        // native_controls::RichEditBox (examples/native_controls/rich_edit_box.hpp) is a
                        // ui::NativeControl around a RichEdit window.
                        auto editor = std::make_unique<native_controls::RichEditBox>(ui::Size{300.0f, 120.0f});
                        editor->set_accessibility({.name = "simple text editor"});

                        // It has no spec: the element itself goes into the declared tree.
                        ui::VerticalStack{
                            .content = {std::move(editor)},
                        }
                    )cpp",
                    .stretch  = true,
                    .controls = {std::move(simple)},
                }),
                example({
                    .title    = "Adding 'Share' to RichEdit's context menu",
                    .code     = R"cpp(
                        ui::Property<std::string> status{};

                        auto editor = std::make_unique<native_controls::RichEditBox>(ui::Size{800.0f, 200.0f});
                        editor->set_accessibility({.name = "editor with custom menu"});
                        // An item of the app's own in the editor's context menu.
                        editor->add_context_menu_item("Share", [&] { status = "RichEdit: Share"; });

                        ui::VerticalStack{
                            .content = {std::move(editor)},
                        }
                    )cpp",
                    .controls = {std::move(custom_menu)},
                }),
                example({
                    .title   = "A custom editor with RichEdit.",
                    .code    = R"cpp(
                        auto box = std::make_unique<native_controls::RichEditBox>(ui::Size{300.0f, 200.0f});
                        native_controls::RichEditBox* editor = box.get();
                        ui::Control<ui::TextBox>      find_box;

                        const auto highlight = [&]
                        {
                            for (const auto& [start, end] : editor->find_all(find_box->text()))
                            {
                                editor->set_range_format(start, end, {.foreground = ui::colors::White,
                                                                      .background = ui::colors::Black});
                            }
                        };

                        ui::VerticalStack{
                            .content = {
                                ui::HorizontalStack{
                                    .content = {
                                        ui::Button{.text     = "Open file",
                                                   .on_click = [&]
                                                   {
                                                       // ... pick an .rtf with ui::pick_file() and read it ...
                                                       editor->load_rtf(rtf);
                                                   }},
                                        ui::Button{.text     = "Save file",
                                                   .on_click = [&]
                                                   {
                                                       const std::string rtf = editor->save_rtf();
                                                       // ... write it where ui::pick_save_file() says ...
                                                   }},
                                        ui::Button{.text = "Bold", .on_click = [&] { editor->toggle_bold(); }},
                                        ui::Button{.text = "Italic", .on_click = [&] { editor->toggle_italic(); }},
                                        ui::Button{.text     = "Red",
                                                   .on_click = [&]
                                                   { editor->set_selection_format({.foreground = ui::Color::from_argb(0xFFFF0000)}); }},
                                    },
                                },
                                std::move(box),
                                ui::TextBox{.placeholder_text = "Enter search text",
                                            .width            = 224,
                                            .on_text_changed  = [&] { highlight(); },
                                            .control          = find_box},
                            },
                        }
                    )cpp",
                    .stretch = true,
                    .controls =
                        {ui::Grid{.columns = {1_fr, ui::AutoSize},
                                  .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                  .margin  = ui::Edges{.bottom = 20.0f},
                                  .content =
                                      {
                                          ui::Cell{.row = 0, .column = 0, .content = file_buttons(s, window, status)},
                                          ui::Cell{.row = 0, .column = 1, .content = std::move(formatting)},
                                          ui::Cell{.row = 1, .column = 0, .column_span = 2, .content = std::move(editor)},
                                          ui::Cell{.row = 2, .column = 0, .column_span = 2, .content = std::move(find)},
                                      }}},
                }),
            });
        });
}

// ---- Native ----

// Any window class, by name: what an app writes when libwinui has no wrapper for the control it
// wants.
class HostedWindow final : public ui::NativeControl
{
public:
    using Notify  = std::function<bool(HWND self, ui::NativeNotification& notification)>;
    using Created = std::function<void(HostedWindow& control, HWND self)>;

    HostedWindow(std::string class_name, std::uint32_t style, ui::Size size, Notify notify, Created created = {}) :
        ui::NativeControl(ui::NativeControlDesc{.class_name = std::move(class_name), .style = style}, size),
        notify_(std::move(notify)),
        created_(std::move(created))
    {
    }

    bool handle_native_notification(ui::NativeNotification& notification) override
    {
        return notify_ && notify_(static_cast<HWND>(native_handle()), notification);
    }

protected:
    void on_native_created(ui::NativeHandle handle) override
    {
        if (created_)
        {
            created_(*this, static_cast<HWND>(handle));
        }
    }

private:
    Notify  notify_;
    Created created_;
};

std::string long_date(const SYSTEMTIME& time)
{
    WCHAR buffer[128]{};
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &time, nullptr, buffer, 128, nullptr) == 0)
    {
        return std::format("{:04}-{:02}-{:02}", time.wYear, time.wMonth, time.wDay);
    }
    return ui::from_utf16(reinterpret_cast<const char16_t*>(buffer)); // WCHAR is UTF-16 on Windows
}

struct NativeControlState
{
    ui::Property<std::string> calendar{"No date selected yet."};
    ui::Property<std::string> picker{"No date changed yet."};
    ui::Property<std::string> trackbar{"Position: 50"};
    native_controls::EditBox* scrolled = nullptr;
};

ui::Spec native_page(Context&)
{
    // The month calendar, date picker and trackbar classes are comctl32's, registered on demand.
    INITCOMMONCONTROLSEX classes{sizeof(INITCOMMONCONTROLSEX), ICC_DATE_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&classes);

    return with_state<NativeControlState>(
        [](NativeControlState& s)
        {
            auto calendar = std::make_unique<HostedWindow>(
                MONTHCAL_CLASSA,
                0,
                ui::Size{230.0f, 160.0f},
                [&s](HWND, ui::NativeNotification& n)
                {
                    const auto* header = reinterpret_cast<const NMHDR*>(n.lparam);
                    if (n.message != WM_NOTIFY || header->code != MCN_SELECT)
                    {
                        return false;
                    }
                    s.calendar = "Selected: " + long_date(reinterpret_cast<const NMSELCHANGE*>(n.lparam)->stSelStart);
                    return true;
                },
                [](HostedWindow& control, HWND self)
                {
                    // The size the calendar needs for one month in its own font, in pixels.
                    RECT needed{};
                    if (MonthCal_GetMinReqRect(self, &needed))
                    {
                        const float scale = static_cast<float>(GetDpiForWindow(self)) / 96.0f;
                        control.set_native_size(ui::Size{static_cast<float>(needed.right - needed.left) / scale,
                                                         static_cast<float>(needed.bottom - needed.top) / scale});
                    }
                });

            auto picker = std::make_unique<HostedWindow>(DATETIMEPICK_CLASSA,
                                                         DTS_LONGDATEFORMAT,
                                                         ui::Size{260.0f, 24.0f},
                                                         [&s](HWND, ui::NativeNotification& n)
                                                         {
                                                             const auto* header = reinterpret_cast<const NMHDR*>(n.lparam);
                                                             if (n.message != WM_NOTIFY || header->code != DTN_DATETIMECHANGE)
                                                             {
                                                                 return false;
                                                             }
                                                             const auto* change = reinterpret_cast<const NMDATETIMECHANGE*>(
                                                                 n.lparam);
                                                             if (change->dwFlags == GDT_VALID)
                                                             {
                                                                 s.picker = "Picked: " + long_date(change->st);
                                                             }
                                                             return true;
                                                         });

            auto trackbar = std::make_unique<HostedWindow>(
                TRACKBAR_CLASSA,
                TBS_HORZ | TBS_AUTOTICKS,
                ui::Size{260.0f, 32.0f},
                [&s](HWND self, ui::NativeNotification& n)
                {
                    if (n.message != WM_HSCROLL)
                    {
                        return false;
                    }
                    s.trackbar = std::format("Position: {}", SendMessageW(self, TBM_GETPOS, 0, 0));
                    return true;
                },
                [](HostedWindow&, HWND self)
                {
                    SendMessageW(self, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                    SendMessageW(self, TBM_SETTICFREQ, 10, 0);
                    SendMessageW(self, TBM_SETPOS, TRUE, 50);
                });

            auto scrolled = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 100.0f},
                                                                       "A stock Win32 EDIT control inside a "
                                                                       "Scroll. "
                                                                       "Scroll "
                                                                       "the view: the window is cut off at "
                                                                       "its edges instead of painting over the rest of "
                                                                       "the "
                                                                       "page.\r\n\r\nRight-click for the "
                                                                       "framework's own menu, in the app's palette.");
            s.scrolled    = scrolled.get();

            auto in_tab = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 80.0f},
                                                                     "This EDIT control lives in the second tab. "
                                                                     "Select the "
                                                                     "first one: it hides.");

            return examples(
                {
                    example({
                        .title    = "The system month calendar, by its class name.",
                        .code     = R"cpp(
                            // Hosts any window class by name and answers its notifications.
                            class MonthCalendar final : public ui::NativeControl
                            {
                            public:
                                explicit MonthCalendar(ui::Property<std::string>& selected) :
                                    ui::NativeControl(ui::NativeControlDesc{.class_name = MONTHCAL_CLASSA, .style = 0},
                                                      ui::Size{230.0f, 160.0f}),
                                    selected_(selected)
                                {
                                }

                                // What the window sends its parent. True means handled.
                                bool handle_native_notification(ui::NativeNotification& n) override
                                {
                                    const auto* header = reinterpret_cast<const NMHDR*>(n.lparam);
                                    if (n.message != WM_NOTIFY || header->code != MCN_SELECT)
                                    {
                                        return false;
                                    }
                                    const SYSTEMTIME& day = reinterpret_cast<const NMSELCHANGE*>(n.lparam)->stSelStart;
                                    selected_ = std::format("Selected: {:04}-{:02}-{:02}", day.wYear, day.wMonth, day.wDay);
                                    return true;
                                }

                            protected:
                                // The window exists: ask it for the size one month needs, in pixels.
                                void on_native_created(ui::NativeHandle handle) override
                                {
                                    const HWND self = static_cast<HWND>(handle);
                                    RECT       needed{};
                                    if (MonthCal_GetMinReqRect(self, &needed))
                                    {
                                        const float scale = static_cast<float>(GetDpiForWindow(self)) / 96.0f;
                                        set_native_size(ui::Size{static_cast<float>(needed.right - needed.left) / scale,
                                                                 static_cast<float>(needed.bottom - needed.top) / scale});
                                    }
                                }

                            private:
                                ui::Property<std::string>& selected_;
                            };

                            ui::Property<std::string> selected{"No date selected yet."};

                            ui::VerticalStack{
                                .content = {
                                    std::make_unique<MonthCalendar>(selected),
                                    ui::Text{.text = selected},
                                },
                            }
                        )cpp",
                        .output   = ui::Text{.text = s.calendar, .horizontal_alignment = ui::Alignment::Leading},
                        .controls = {std::move(calendar)},
                    }),
                    example({
                        .title    = "The system date picker.",
                        .code     = R"cpp(
                            // HostedWindow is the page's ui::NativeControl subclass: a class name, a style,
                            // a size and a lambda that answers the window's notifications.
                            ui::Property<std::string> picked{"No date changed yet."};

                            auto picker = std::make_unique<HostedWindow>(
                                DATETIMEPICK_CLASSA,
                                DTS_LONGDATEFORMAT,
                                ui::Size{260.0f, 24.0f},
                                [&](HWND, ui::NativeNotification& n)
                                {
                                    const auto* header = reinterpret_cast<const NMHDR*>(n.lparam);
                                    if (n.message != WM_NOTIFY || header->code != DTN_DATETIMECHANGE)
                                    {
                                        return false;
                                    }
                                    const auto* change = reinterpret_cast<const NMDATETIMECHANGE*>(n.lparam);
                                    if (change->dwFlags == GDT_VALID)
                                    {
                                        const SYSTEMTIME& day = change->st;
                                        picked = std::format("Picked: {:04}-{:02}-{:02}", day.wYear, day.wMonth, day.wDay);
                                    }
                                    return true;
                                });

                            ui::VerticalStack{
                                .content = {
                                    std::move(picker),
                                    ui::Text{.text = picked},
                                },
                            }
                        )cpp",
                        .output   = ui::Text{.text = s.picker, .horizontal_alignment = ui::Alignment::Leading},
                        .controls = {std::move(picker)},
                    }),
                    example({
                        .title    = "A trackbar reporting its position.",
                        .code     = R"cpp(
                            ui::Property<std::string> position{"Position: 50"};

                            // HostedWindow is the page's ui::NativeControl subclass. The second lambda runs
                            // once the window exists, from on_native_created().
                            auto trackbar = std::make_unique<HostedWindow>(
                                TRACKBAR_CLASSA,
                                TBS_HORZ | TBS_AUTOTICKS,
                                ui::Size{260.0f, 32.0f},
                                [&](HWND self, ui::NativeNotification& n)
                                {
                                    if (n.message != WM_HSCROLL)
                                    {
                                        return false;
                                    }
                                    position = std::format("Position: {}", SendMessageW(self, TBM_GETPOS, 0, 0));
                                    return true;
                                },
                                [](HostedWindow&, HWND self)
                                {
                                    SendMessageW(self, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                                    SendMessageW(self, TBM_SETTICFREQ, 10, 0);
                                    SendMessageW(self, TBM_SETPOS, TRUE, 50);
                                });

                            ui::VerticalStack{
                                .content = {
                                    std::move(trackbar),
                                    ui::Text{.text = position},
                                },
                            }
                        )cpp",
                        .output   = ui::Text{.text = s.trackbar, .horizontal_alignment = ui::Alignment::Leading},
                        .controls = {std::move(trackbar)},
                    }),
                    example(
                        {
                            .title   = "A native window clipped by a Scroll.",
                            .code    = R"cpp(
                                auto box = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 100.0f},
                                                                                      "A stock Win32 EDIT control inside a Scroll.");
                                native_controls::EditBox* scrolled = box.get();

                                // The window scrolls with the content and is cut off at the view's edges.
                                ui::Scroll{
                                    .width   = 340,
                                    .height  = 140,
                                    .content = ui::VerticalStack{
                                        .spacing = 12,
                                        .content = {
                                            ui::Text{.text = "Scroll down to the EditBox, then on past it."},
                                            std::move(box),
                                            ui::Text{.text = "The end of the scrolled content."},
                                            ui::Text{.text = " ", .height = 60},
                                        },
                                    },
                                }

                                // Collapsing the element hides its window too.
                                ui::CheckBox{.text               = "Collapse the EditBox",
                                             .on_checked_changed = [scrolled](std::optional<bool> on)
                                             {
                                                 scrolled->set_visibility(on == true ? ui::Visibility::Collapsed
                                                                                     : ui::Visibility::Visible);
                                             }}
                            )cpp",
                            .options = ui::CheckBox{.text = "Collapse the EditBox",
                                                    .on_checked_changed =
                                                        [&s](std::optional<bool> on)
                                                    {
                                                        s.scrolled->set_visibility(on == true ? ui::Visibility::Collapsed
                                                                                              : ui::Visibility::Visible);
                                                    }},
                            .controls =
                                {
                                    ui::Scroll{.width  = 340.0f,
                                               .height = 140.0f,
                                               .content =
                                                   ui::
                                                       VerticalStack{
                                                           .spacing = 12.0f,
                                                           .content =
                                                               {
                                                                   ui::Text{.text = "Scroll down to the EditBox, "
                                                                                    "then on past it.",
                                                                            .wrapping = ui::TextWrapping::WrapWholeWords},
                                                                   std::move(scrolled),
                                                                   ui::Text{.text = "The end of the scrolled content.",
                                                                            .wrapping = ui::TextWrapping::WrapWholeWords},
                                                                   ui::Text{.text = " ", .height = 60.0f},
                                                               },
                                                       }},
                                },
                        }),
                    example({
                        .title = "A native window in a tab that is not selected.",
                        .code  = R"cpp(
                            auto in_tab = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 80.0f},
                                                                                     "This EDIT control lives in the second tab.");

                            // The window is shown only while its tab is the selected one.
                            ui::Tabs{
                                .can_reorder_tabs          = false,
                                .width                     = 400,
                                .height                    = 180,
                                .items = {
                                    ui::Tab{.header   = "Framework",
                                            .closable = false,
                                            .content  = ui::Text{.text = "Only framework content here.", .margin = ui::Thickness{12.0f}}},
                                    ui::Tab{.header = "Native", .closable = false, .content = std::move(in_tab)},
                                },
                            }
                        )cpp",
                        .controls = {ui::Tabs{.can_reorder_tabs = false,
                                              .width            = 400.0f,
                                              .height           = 180.0f,
                                              .items =
                                                  {
                                                      ui::Tab{.header   = "Framework",
                                                              .closable = false,
                                                              .content  = ui::Text{.text   = "Only framework content "
                                                                                             "here.",
                                                                                   .margin = ui::Thickness{12.0f}}},
                                                      ui::Tab{.header = "Native", .closable = false, .content = std::move(in_tab)},
                                                  }}},
                    }),
                });
        });
}

} // namespace

std::span<const Page> native_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"EditBox",
         "A stock Win32 EDIT window: a single-line or multi-line plain text field, hosted the same way as "
         "everything else.",
         edit_box_page},
        {"RichEdit",
         "A real RichEdit window (RICHEDIT50W): formatted text, RTF in and out, hosted the same way as everything "
         "else.",
         rich_edit_page},
        {"Native",
         "Any window class, hosted by its name: its notifications answered, clipped and hidden by the tree "
         "around it.",
         native_page},
    });
    return Pages;
}

} // namespace gallery
