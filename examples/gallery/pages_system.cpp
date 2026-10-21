// SPDX-License-Identifier: MIT
#include "gallery.hpp"
#include "winui/core/Utf8Path.hpp"

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <cstdint>

using ui::operator""_fr;

namespace gallery
{
namespace
{
// ---- Clipboard ----

// DataPackageOperation as the Clipboard sample's switch names it: exactly one of the three, or
// "None" (Explorer's Ctrl+C asks for Copy | Link, which that switch calls None too).
const char* operation_name(ui::DataPackageOperation operation)
{
    switch (operation)
    {
        case ui::DataPackageOperation::Copy:
            return "Copy";
        case ui::DataPackageOperation::Move:
            return "Move";
        case ui::DataPackageOperation::Link:
            return "Link";
        default:
            return "None";
    }
}

std::string format_list(const std::vector<std::string>& formats)
{
    std::string list;
    for (const std::string& format : formats)
    {
        list += "   • " + format + "\n";
    }
    return list;
}

// The theme's success color.
ui::Color success_text(const ui::Theme& theme)
{
    return theme.colors().success;
}

struct ClipboardState
{
    ui::Control<ui::RichEditBox> editor;
    Timer*                       timer  = nullptr;
    ui::ImageControl*            pasted = nullptr;

    ui::Property<std::string> confirmation{};
    ui::Property<std::string> pasted_text{"Click the button!"};
    ui::Property<std::string> image_status{};
    ui::Property<std::string> files_status{};

    std::string               options_text{"Text with clipboard options"};
    bool                      allow_history = true;
    bool                      allow_roaming = true;
    ui::Property<std::string> options_status{};

    ui::Property<std::string> other_status{};
    ui::Subscription          watch; // content_changed(), while *Monitor content_changed()* is on
};

ui::Spec clipboard_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<ClipboardState>(
        [&window](ClipboardState& s)
        {
            ui::Spec editor = ui::RichEditBox{.text          = "This text will be copied to the clipboard.",
                                              .width         = 800.0f,
                                              .height        = 100.0f,
                                              .accessibility = {.name = "editor with custom menu"},
                                              .control       = s.editor};

            auto timer = std::make_unique<Timer>();
            s.timer    = timer.get();

            auto pasted = std::make_unique<ui::ImageControl>();
            pasted->set_stretch(ui::Stretch::UniformToFill);
            pasted->set_width(200.0f);
            pasted->set_height(150.0f);
            pasted->set_horizontal_alignment(ui::Alignment::Leading);
            pasted->set_visibility(ui::Visibility::Collapsed);
            pasted->set_accessibility({.name = "Pasted image from clipboard"});
            s.pasted = pasted.get();

            const std::string history_state = std::format("Clipboard history: {}",
                                                          ui::clipboard().is_history_enabled() ? "enabled" : "disabled");
            const std::string roaming_state = std::format("Clipboard roaming: {}",
                                                          ui::clipboard().is_roaming_enabled() ? "enabled" : "disabled");

            return examples({
                example({
                    .title    = "Copy text to the Clipboard",
                    .code     = R"cpp(
                        ui::Clipboard& clipboard = ui::clipboard();

                        ui::Control<ui::RichEditBox> editor;
                        ui::Property<std::string>    confirmation{};

                        ui::VerticalStack{
                            .content = {
                                ui::HorizontalStack{
                                    .content = {
                                        ui::Button{.text     = "Copy Text to the Clipboard",
                                                   .on_click = [&]
                                                   {
                                                       ui::ClipboardContent content;
                                                       content.text = editor->text();
                                                       clipboard.set_content(content);
                                                       confirmation = "Text copied to clipboard!";
                                                   }},
                                        ui::Text{.text = confirmation},
                                    },
                                },
                                ui::RichEditBox{.text    = "This text will be copied to the clipboard.",
                                                .width   = 800,
                                                .height  = 100,
                                                .control = editor},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::HorizontalStack{
                                     .content =
                                         {
                                             ui::Button{.text     = "Copy Text to the Clipboard",
                                                        .margin   = ui::Edges{.bottom = 10.0f},
                                                        .on_click = [&s]
                                                        {
                                                            ui::ClipboardContent content;
                                                            content.text = s.editor->text();
                                                            ui::clipboard().set_content(content);
                                                            s.confirmation = "Text copied to clipboard!";
                                                            // Hidden again after 2 seconds.
                                                            s.timer->start(2000.0, [&s] { s.confirmation = ""; });
                                                        }},
                                             ui::Text{.text   = s.confirmation,
                                                      .margin = ui::Edges{.left = 20.0f, .top = 5.0f}},
                                         }},
                                 std::move(editor),
                                 std::move(timer)},
                }),
                example({
                    .title    = "Paste text from the Clipboard",
                    .code     = R"cpp(
                        ui::Clipboard&            clipboard = ui::clipboard();
                        ui::Property<std::string> pasted_text{"Click the button!"};

                        ui::VerticalStack{
                            .content = {
                                ui::Button{.text     = "Paste Text from the Clipboard",
                                           .on_click = [&]
                                           {
                                               // Nothing when the clipboard holds no text.
                                               if (auto text = clipboard.text())
                                               {
                                                   pasted_text = *text;
                                               }
                                           }},
                                ui::Text{.text = pasted_text, .wrapping = ui::TextWrapping::Wrap},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::Button{.text     = "Paste Text from the Clipboard",
                                            .margin   = ui::Edges{.bottom = 10.0f},
                                            .horizontal_alignment  = ui::Alignment::Leading,
                                            .on_click = [&s]
                                            {
                                                if (auto text = ui::clipboard().text())
                                                {
                                                    s.pasted_text = *text;
                                                }
                                            }},
                                 ui::Text{.inlines = {{.text = "Clipboard:", .underline = true}},
                                          .margin  = ui::Edges{.left = 5.0f, .top = 5.0f}},
                                 ui::Text{.text     = s.pasted_text,
                                          .wrapping = ui::TextWrapping::Wrap,
                                          .margin   = ui::Edges{.left = 5.0f, .top = 5.0f}}},
                }),
                example({
                    .title    = "Copy and paste an image",
                    .code     = R"cpp(
                        ui::Clipboard&            clipboard = ui::clipboard();
                        ui::Control<ui::Image>    pasted;
                        ui::Property<bool>        has_pasted{false};
                        ui::Property<std::string> status{};

                        ui::VerticalStack{
                            .spacing = 10,
                            .content = {
                                ui::Image{.source = photo, .stretch = ui::Stretch::UniformToFill, .width = 200, .height = 150},
                                ui::Button{.text     = "Copy Image to Clipboard",
                                           .on_click = [&]
                                           {
                                               ui::ClipboardContent content;
                                               content.bitmap = photo;
                                               status = clipboard.set_content(content) ? "Image copied to clipboard."
                                                                                        : "Error copying image to clipboard.";
                                           }},
                                ui::Button{.text     = "Paste Image from Clipboard",
                                           .on_click = [&]
                                           {
                                               if (!clipboard.contains(ui::StandardDataFormat::Bitmap))
                                               {
                                                   status     = "Bitmap format is not available in the clipboard.";
                                                   has_pasted = false;
                                               }
                                               else if (auto picture = clipboard.bitmap())
                                               {
                                                   pasted->set_source(ui::ImageSource{std::move(*picture)});
                                                   has_pasted = true;
                                                   status     = "Image pasted from clipboard.";
                                               }
                                           }},
                                ui::Text{.text = status},
                                ui::Image{.stretch = ui::Stretch::UniformToFill,
                                          .width   = 200,
                                          .height  = 150,
                                          .visible = has_pasted,
                                          .control = pasted},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::VerticalStack{
                        .spacing = 10.0f,
                        .content =
                            {
                                ui::Image{.source        = ui::ImageSource{sample_photo(1)}, // the mountain photo
                                          .stretch       = ui::Stretch::UniformToFill,
                                          .width         = 200.0f,
                                          .height        = 150.0f,
                                          .horizontal_alignment       = ui::Alignment::Leading,
                                          .accessibility = {.name = "Source image to copy"}},
                                ui::HorizontalStack{
                                    .spacing = 8.0f,
                                    .content =
                                        {
                                            ui::Button{.text     = "Copy Image to Clipboard",
                                                       .on_click = [&s]
                                                       {
                                                           ui::ClipboardContent content;
                                                           content.bitmap = sample_photo(1);
                                                           s.image_status = ui::clipboard().set_content(content)
                                                                                ? "Image copied to clipboard."
                                                                                : "Error copying image to clipboard.";
                                                       }},
                                            ui::Button{.text     = "Paste Image from Clipboard",
                                                       .on_click = [&s]
                                                       {
                                                           if (!ui::clipboard().contains(ui::StandardDataFormat::Bitmap))
                                                           {
                                                               s.image_status = "Bitmap format is not available in the clipboard.";
                                                               s.pasted->set_visibility(ui::Visibility::Collapsed);
                                                               return;
                                                           }
                                                           if (auto picture = ui::clipboard().bitmap())
                                                           {
                                                               s.pasted->set_source(ui::ImageSource{std::move(*picture)});
                                                               s.pasted->set_visibility(ui::Visibility::Visible);
                                                               s.image_status = "Image pasted from clipboard.";
                                                           }
                                                           else
                                                           {
                                                               s.image_status = "Error pasting image: it could not be decoded.";
                                                           }
                                                       }},
                                        }},
                                ui::Text{.text = s.image_status, .foreground = success_text},
                                std::move(pasted),
                            }}},
                }),
                example({
                    .title    = "Copy and paste files",
                    .code     = R"cpp(
                        ui::Clipboard&            clipboard = ui::clipboard();
                        ui::Property<std::string> status{};

                        ui::VerticalStack{
                            .spacing = 10,
                            .content = {
                                ui::Button{.text     = "Copy Files to Clipboard",
                                           .on_click = [&]
                                           {
                                               const auto files = ui::pick_files(window, {.extensions = {"*"}});
                                               if (files.empty())
                                               {
                                                   return;
                                               }
                                               ui::ClipboardContent content;
                                               content.storage_items       = files;
                                               content.requested_operation = ui::DataPackageOperation::Copy;
                                               status = clipboard.set_content(content)
                                                            ? std::format("{} file(s) copied to clipboard.", files.size())
                                                            : std::string{"Error copying files to clipboard."};
                                           }},
                                ui::Button{.text     = "Paste Files from Clipboard",
                                           .on_click = [&]
                                           {
                                               if (!clipboard.contains(ui::StandardDataFormat::StorageItems))
                                               {
                                                   status = "StorageItems format is not available in the clipboard.";
                                                   return;
                                               }
                                               // requested_operation() tells a copy from a cut.
                                               std::string output;
                                               for (const std::filesystem::path& item : clipboard.storage_items())
                                               {
                                                   output += ui::path_to_utf8(item.filename()) + "\n";
                                               }
                                               status = std::move(output);
                                           }},
                                ui::Text{.text = status, .wrapping = ui::TextWrapping::Wrap},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::VerticalStack{
                        .spacing = 10.0f,
                        .content =
                            {
                                ui::HorizontalStack{
                                    .spacing = 8.0f,
                                    .content =
                                        {
                                            ui::Button{.text     = "Copy Files to Clipboard",
                                                       .on_click = [&s, &window]
                                                       {
                                                           const std::vector<std::filesystem::path> files = ui::pick_files(window, {.extensions = {"*"}});
                                                           if (files.empty())
                                                           {
                                                               return;
                                                           }
                                                           ui::ClipboardContent content;
                                                           content.storage_items       = files;
                                                           content.requested_operation = ui::DataPackageOperation::Copy;
                                                           s.files_status              = ui::clipboard().set_content(content)
                                                                                             ? std::format("{} file(s) copied to clipboard.", files.size())
                                                                                             : std::string{"Error copying files to clipboard."};
                                                       }},
                                            ui::Button{.text     = "Paste Files from Clipboard",
                                                       .on_click = [&s]
                                                       {
                                                           if (!ui::clipboard().contains(ui::StandardDataFormat::StorageItems))
                                                           {
                                                               s.files_status = "StorageItems format is not available in the clipboard.";
                                                               return;
                                                           }
                                                           const std::vector<std::filesystem::path> items =
                                                               ui::clipboard().storage_items();
                                                           std::string output =
                                                               std::format("Requested operation: {}\nFile(s) on clipboard ({}):\n",
                                                                           operation_name(ui::clipboard().requested_operation()),
                                                                           items.size());
                                                           for (const std::filesystem::path& item : items)
                                                           {
                                                               output += "  • " + ui::path_to_utf8(item.filename()) + "\n";
                                                           }
                                                           s.files_status = std::move(output);
                                                       }},
                                        }},
                                ui::Text{.text = s.files_status, .wrapping = ui::TextWrapping::Wrap},
                            }}},
                }),
                example({
                    .title    = "Clipboard history and roaming options",
                    .code     = R"cpp(
                        ui::Clipboard&            clipboard = ui::clipboard();
                        ui::Property<std::string> text{"Text with clipboard options"};
                        ui::Property<bool>        allow_history{true};
                        ui::Property<bool>        allow_roaming{true};
                        ui::Property<std::string> status{};

                        ui::VerticalStack{
                            .spacing = 10,
                            .content = {
                                ui::TextBox{.placeholder_text = "Enter text to copy with options", .text = text},
                                ui::ToggleSwitch{.header = "Allow in History", .checked = allow_history},
                                ui::ToggleSwitch{.header = "Allow Roaming", .checked = allow_roaming},
                                ui::Button{.text     = "Copy with Options",
                                           .on_click = [&]
                                           {
                                               ui::ClipboardContent content;
                                               content.text = text.get();
                                               const ui::ClipboardContentOptions options{
                                                   .is_allowed_in_history = allow_history.get(),
                                                   .is_roamable           = allow_roaming.get()};
                                               status = clipboard.set_content(content, options)
                                                            ? "Text copied to clipboard."
                                                            : "Error copying content to clipboard.";
                                           }},
                                ui::Text{.text = status, .wrapping = ui::TextWrapping::Wrap},
                                // Whether the user has the two features turned on at all.
                                ui::Text{.text = clipboard.is_history_enabled() ? "Clipboard history: enabled"
                                                                                 : "Clipboard history: disabled"},
                                ui::Text{.text = clipboard.is_roaming_enabled() ? "Clipboard roaming: enabled"
                                                                                 : "Clipboard roaming: disabled"},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::VerticalStack{
                        .spacing = 10.0f,
                        .content =
                            {
                                ui::TextBox{.placeholder_text = "Enter text to copy with options",
                                            .text             = s.options_text,
                                            .width            = 400.0f,
                                            .horizontal_alignment          = ui::Alignment::Leading,
                                            .accessibility    = {.name = "Text to copy with options"},
                                            .on_text_changed = [&s](const std::string& text) { s.options_text = text; }},
                                ui::HorizontalStack{
                                    .spacing = 16.0f,
                                    .content =
                                        {
                                            ui::ToggleSwitch{.header        = "Allow in History",
                                                             .checked            = true,
                                                             .accessibility = {.name = "Allow clipboard content in history"},
                                                             .on_checked_changed = [&s](bool on) { s.allow_history = on; }},
                                            ui::ToggleSwitch{.header        = "Allow Roaming",
                                                             .checked            = true,
                                                             .accessibility = {.name = "Allow clipboard content to roam"},
                                                             .on_checked_changed = [&s](bool on) { s.allow_roaming = on; }},
                                        }},
                                ui::Button{.text     = "Copy with Options",
                                           .horizontal_alignment  = ui::Alignment::Leading,
                                           .on_click = [&s]
                                           {
                                               if (s.options_text.empty())
                                               {
                                                   s.options_status = "Please enter text to copy.";
                                                   return;
                                               }
                                               ui::ClipboardContent content;
                                               content.text = s.options_text;
                                               const ui::ClipboardContentOptions options{.is_allowed_in_history = s.allow_history,
                                                                                     .is_roamable = s.allow_roaming};
                                               s.options_status =
                                                   ui::clipboard().set_content(content, options)
                                                       ? std::format("Text copied to clipboard. History: {}. Roaming: {}.",
                                                                     options.is_allowed_in_history ? "allowed" : "excluded",
                                                                     options.is_roamable ? "allowed" : "excluded")
                                                       : std::string{"Error copying content to clipboard."};
                                           }},
                                ui::Text{.text = s.options_status, .wrapping = ui::TextWrapping::Wrap},
                                ui::HorizontalStack{.spacing  = 16.0f,
                                               .content = {ui::Text{.text = history_state}, ui::Text{.text = roaming_state}}},
                            }}},
                }),
                example({
                    .title    = "Other clipboard operations",
                    .code     = R"cpp(
                        ui::Clipboard&            clipboard = ui::clipboard();
                        ui::Property<std::string> status{};
                        ui::Subscription          watch; // observes while it lives

                        const auto list = [](const std::vector<std::string>& formats)
                        {
                            std::string text;
                            for (const std::string& format : formats)
                            {
                                text += format + "\n";
                            }
                            return text;
                        };

                        ui::VerticalStack{
                            .spacing = 10,
                            .content = {
                                ui::Button{.text     = "Show Clipboard Formats",
                                           .on_click = [&] { status = list(clipboard.available_formats()); }},
                                ui::Button{.text     = "Clear Clipboard",
                                           .on_click = [&]
                                           {
                                               clipboard.clear();
                                               status = "Clipboard has been cleared.";
                                           }},
                                ui::ToggleSwitch{.header     = "Watch for clipboard changes",
                                                 .on_checked_changed = [&](bool on)
                                                 {
                                                     if (!on)
                                                     {
                                                         watch.reset();
                                                         return;
                                                     }
                                                     watch = clipboard.content_changed().observe(
                                                         [&](const std::uint64_t&)
                                                         {
                                                             status = "Clipboard content changed!\n" +
                                                                      list(clipboard.available_formats());
                                                         });
                                                 }},
                                ui::Text{.text = status, .wrapping = ui::TextWrapping::Wrap},
                            },
                        }
                    )cpp",
                    .stacked  = true,
                    .controls = {ui::VerticalStack{
                        .spacing = 10.0f,
                        .content =
                            {
                                ui::HorizontalStack{
                                    .spacing = 8.0f,
                                    .content =
                                        {
                                            ui::Button{.text     = "Show Clipboard Formats",
                                                       .on_click = [&s]
                                                       {
                                                           const std::vector<std::string> formats = ui::clipboard().available_formats();
                                                           s.other_status = formats.empty()
                                                                                ? std::string{"The clipboard is empty.\n"}
                                                                                : "Available formats on the clipboard:\n" + format_list(formats);
                                                       }},
                                            ui::Button{.text     = "Clear Clipboard",
                                                       .on_click = [&s]
                                                       {
                                                           ui::clipboard().clear();
                                                           s.other_status = "Clipboard has been cleared.";
                                                       }},
                                        }},
                                ui::ToggleSwitch{.header        = "Watch for clipboard changes",
                                                 .horizontal_alignment       = ui::Alignment::Leading,
                                                 .accessibility = {.name = "Monitor clipboard content changed event"},
                                                 .on_checked_changed    = [&s](bool on)
                                                 {
                                                     if (!on)
                                                     {
                                                         s.watch.reset();
                                                         s.other_status = "Stopped monitoring clipboard changes.";
                                                         return;
                                                     }
                                                     s.watch = ui::clipboard().content_changed().observe(
                                                         [&s](const std::uint64_t&)
                                                         {
                                                             const std::vector<std::string> formats =
                                                                 ui::clipboard().available_formats();
                                                             s.other_status =
                                                                 "Clipboard content changed!\n" +
                                                                 (formats.empty() ? std::string{"Clipboard is now empty.\n"}
                                                                                  : "New formats:\n" + format_list(formats));
                                                         });
                                                     s.other_status = "Monitoring clipboard changes...";
                                                 }},
                                ui::Text{.text = s.other_status, .wrapping = ui::TextWrapping::Wrap},
                            }}},
                }),
            });
        });
}

// ---- Notifications ----

struct NotificationsState
{
    ui::Property<std::string> clicked{"Not clicked yet."};
    int                       sent = 0;
};

ui::Spec notifications_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<NotificationsState>(
        [&window](NotificationsState& s)
        {
            return examples({
                example({
                    .title = "Send a notification",
                    .code  = R"cpp(
                        ui::Property<std::string> clicked{"Not clicked yet."};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Button{.text     = "Send a notification",
                                           .on_click = [&]
                                           {
                                               ui::notify({.title    = "Export finished",
                                                           .text     = "report.pdf was saved to Documents.",
                                                           .on_click = [&]
                                                           {
                                                               clicked = "You clicked the notification.";
                                                               window.activate();
                                                           }});
                                           }},
                                ui::Text{.text = clicked},
                            },
                        }
                    )cpp",
                    .controls =
                        {ui::VerticalStack{.spacing = 8.0f,
                                           .content =
                                               {
                                                   ui::Button{.text                 = "Send a notification",
                                                              .horizontal_alignment = ui::Alignment::Leading,
                                                              .on_click =
                                                                  [&s, &window]
                                                              {
                                                                  const int number = ++s.sent;
                                                                  ui::notify(
                                                                      {.title    = "Export finished",
                                                                       .text     = "report.pdf was saved to Documents.",
                                                                       .on_click = [&s, &window, number]
                                                                       {
                                                                           s.clicked = std::
                                                                               format("You clicked notification {}.",
                                                                                      number);
                                                                           window.activate();
                                                                       }});
                                                              }},
                                                   ui::Text{.text = s.clicked},
                                               }}},
                }),
                example({
                    .title = "A warning",
                    .code  = R"cpp(
                        ui::Button{.text     = "Warn",
                                   .on_click = []
                                   {
                                       ui::notify({.title = "Disk almost full",
                                                   .text  = "Less than 1 GB is left on drive C.",
                                                   .icon  = ui::NotificationIcon::Warning});
                                   }}
                    )cpp",
                    .controls =
                        {ui::Button{.text                 = "Warn",
                                    .horizontal_alignment = ui::Alignment::Leading,
                                    .on_click =
                                        []
                                    {
                                        ui::notify({.title = "Disk almost full",
                                                    .text  = "Less than 1 GB is left on drive C.",
                                                    .icon  = ui::NotificationIcon::Warning});
                                    }}},
                }),
            });
        });
}

// ---- Single Instance ----

struct SingleInstanceState
{
    ui::Property<std::string> status{"This gallery has not claimed the name."};
    ui::Property<std::string> jump_list{"The gallery sets no jump list until you ask."};
};

// What the claimed name hands over. The claim lasts as long as the process, so this does too:
// the page, and its state, can be gone by the time a second gallery starts.
struct Handovers
{
    ui::Property<std::string> started{};
    int                       count = 0;
};

Handovers& handovers()
{
    static Handovers instance;
    return instance;
}

// The two tasks the jump-list card sets; each starts the gallery again with its own argument.
std::vector<ui::JumpTask> gallery_jump_tasks()
{
    return {
        ui::JumpTask{.title       = "Say hello",
                     .arguments   = "--gallery-task hello",
                     .description = "Hands \"hello\" to the running gallery",
                     .icon        = {.glyph = ui::IconGlyph{0xE8BD}, .foreground = ui::colors::White}},
        ui::JumpTask{.title       = "Say goodbye",
                     .arguments   = "--gallery-task goodbye",
                     .description = "Hands \"goodbye\" to the running gallery"},
    };
}

ui::Spec single_instance_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<SingleInstanceState>(
        [&window, &ctx](SingleInstanceState& s)
        {
            return examples({
                example({
                    .title = "One running copy of the program",
                    .code  = R"cpp(
                        int main()
                        {
                            ui::Window window{{.title = "Notes"}};
                            // A second start hands its command line to the first and gets false.
                            if (!ui::claim_single_instance("com.example.notes",
                                                           [&window](ui::InstanceActivation started)
                                                           {
                                                               // started.arguments[0] is the program
                                                               window.activate();
                                                           }))
                            {
                                return 0;
                            }
                            return ui::run(window);
                        }
                    )cpp",
                    .controls =
                        {ui::VerticalStack{.spacing = 8.0f,
                                           .content =
                                               {
                                                   ui::Button{.text                 = "Claim the gallery's name",
                                                              .horizontal_alignment = ui::Alignment::Leading,
                                                              .on_click =
                                                                  [&s, &window]
                                                              {
                                                                  const bool first = ui::claim_single_instance(
                                                                      "libwinui.gallery",
                                                                      [&window](ui::InstanceActivation started)
                                                                      {
                                                                          std::string arguments;
                                                                          for (std::size_t i = 1;
                                                                               i < started.arguments.size();
                                                                               ++i)
                                                                          {
                                                                              arguments += " " + started.arguments[i];
                                                                          }
                                                                          Handovers& h = handovers();
                                                                          h.started    = std::
                                                                              format("Start {} handed over{}{}.",
                                                                                     ++h.count,
                                                                                     arguments.empty() ? "" : ":",
                                                                                     arguments.empty() ? " no arguments"
                                                                                                       : arguments);
                                                                          window.activate();
                                                                      });
                                                                  s.status = first ? "This gallery holds the name. "
                                                                                     "Start a second gallery and claim "
                                                                                     "it there: it hands its command "
                                                                                     "line to this one."
                                                                                   : "Another gallery holds the name, "
                                                                                     "and was handed this one's "
                                                                                     "command line.";
                                                              }},
                                                   ui::Text{.text = s.status},
                                                   ui::Text{.text = handovers().started},
                                               }}},
                }),
                example({
                    .title = "Tasks in the jump list",
                    .code  = R"cpp(
                        // A task starts the program again with its arguments; with the name claimed,
                        // that start hands them to the running copy and ends.
                        ui::set_jump_list({
                            ui::JumpTask{.title       = "Say hello",
                                         .arguments   = "--gallery-task hello",
                                         .description = "Hands \"hello\" to the running gallery",
                                         .icon        = {.glyph = ui::IconGlyph{0xE8BD}}},
                            ui::JumpTask{.title = "Say goodbye", .arguments = "--gallery-task goodbye"},
                        });

                        // Opened files go to the Recent section, for file types the program opens.
                        ui::add_recent_file(path);
                    )cpp",
                    .output   = ui::Text{.text = s.jump_list, .wrapping = ui::TextWrapping::Wrap},
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Button{.text                 = "Set the jump list",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .on_click =
                                               [&s, &ctx]
                                           {
                                               ui::set_jump_list(gallery_jump_tasks());
                                               const bool first = ui::claim_single_instance(
                                                   "libwinui.gallery",
                                                   report_handover(ctx.status, ctx.window));
                                               s.jump_list = first ? "Right-click the gallery's taskbar button "
                                                                     "and pick a task. It starts a second "
                                                                     "gallery, which hands its argument to this "
                                                                     "one: it shows in the status strip."
                                                                   : "Another gallery holds the name, so the "
                                                                     "tasks go to that one.";
                                           }},
                                ui::Button{.text                 = "Clear the jump list",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .on_click =
                                               [&s]
                                           {
                                               ui::set_jump_list({});
                                               s.jump_list = "The jump list has no tasks.";
                                           }},
                            }}},
                }),
            });
        });
}

// ---- Storage Pickers ----

// Every ui::PickerLocation, in the order the dropdown lists them.
constexpr std::array<std::pair<const char*, ui::PickerLocation>, 9> Locations{{
    {"Documents", ui::PickerLocation::Documents},
    {"Computer", ui::PickerLocation::Computer},
    {"Desktop", ui::PickerLocation::Desktop},
    {"Downloads", ui::PickerLocation::Downloads},
    {"Music", ui::PickerLocation::Music},
    {"Pictures", ui::PickerLocation::Pictures},
    {"Videos", ui::PickerLocation::Videos},
    {"Objects3D", ui::PickerLocation::Objects3D},
    {"Unspecified", ui::PickerLocation::Unspecified},
}};
constexpr std::array<const char*, 3> FileTypes{"All Files (*)", "Text      Files (*.txt)", "Images (*.jpg, *.png)"};
constexpr std::array<const char*, 3> Extensions{".txt", ".json", ".xml"};

std::vector<std::string> location_names()
{
    std::vector<std::string> names;
    names.reserve(Locations.size());
    for (const auto& [name, id] : Locations)
    {
        names.emplace_back(name);
    }
    return names;
}

template <std::size_t N>
std::vector<std::string> names(const std::array<const char*, N>& items)
{
    return std::vector<std::string>(items.begin(), items.end());
}

// The file-type ComboBox's tag, as each open card reads it: ".txt", the two image types, or
// everything, which the single-file card leaves unsaid and the multiple-files card says as "*".
std::vector<std::string> file_type_filter(std::size_t index, bool say_all)
{
    switch (index)
    {
        case 1:
            return {".txt"};
        case 2:
            return {".jpg", ".png"};
        default:
            return say_all ? std::vector<std::string>{"*"} : std::vector<std::string>{};
    }
}

// One open-file card's options.
struct OpenOptions
{
    std::size_t file_type = 0;
    std::string commit_text;
    std::size_t location  = 0;
    std::size_t view_mode = 0;
};

struct PickersState
{
    OpenOptions               single{.commit_text = "Pick File"};
    ui::Property<bool>        single_enabled{true};
    ui::Property<std::string> single_output{"No file picked"};

    OpenOptions               multiple{.commit_text = "Pick Files"};
    ui::Property<bool>        multiple_enabled{true};
    ui::Property<std::string> multiple_output{"No files picked"};

    std::string               content{"Hello, libwinui!"};
    bool                      txt               = false;
    bool                      json              = false;
    bool                      xml               = false;
    std::size_t               default_extension = 0;
    std::string               suggested_name{"NewDocument"};
    std::string               save_commit_text{"Save File"};
    std::size_t               save_location = 0;
    ui::Property<std::string> suggested_folder{};
    bool                      remember_last = true;
    ui::Property<bool>        save_enabled{true};
    ui::Property<bool>        folder_button_enabled{true};
    ui::Property<std::string> save_output{"No file saved"};

    std::string               folder_commit_text{"Pick Folder"};
    std::size_t               folder_location  = 0;
    std::size_t               folder_view_mode = 0;
    ui::Property<bool>        folder_enabled{true};
    ui::Property<std::string> folder_output{"No folder picked"};
};

ui::PickFileOptions open_picker(const OpenOptions& o, bool say_all)
{
    return ui::PickFileOptions{.commit_button_text = o.commit_text,
                               .extensions         = file_type_filter(o.file_type, say_all),
                               .start_location     = Locations[o.location].second};
}

// A card's "File type", "Commit button text" and "Suggested start location" options. Any of
// the three may be left out.
ui::Spec picker_options(OpenOptions* open, std::string* commit, const char* placeholder, std::size_t* location)
{
    ui::SpecList children;
    if (open != nullptr)
    {
        children.nodes.push_back(
            ui::ComboBox{.header               = "File type",
                         .items                = names(FileTypes),
                         .selected_index       = 0,
                         .width                = 200.0f,
                         .on_selection_changed = [open](std::optional<std::size_t> i)
                         { open->file_type = i.value_or(0); }}
                .build());
    }
    children.nodes.push_back(
        ui::TextBox{.header           = "Commit button text",
                    .placeholder_text = std::string{placeholder},
                    .text             = *commit,
                    .on_text_changed  = [commit](const std::string& text) { *commit = text; }}
            .build());
    children.nodes.push_back(
        ui::ComboBox{.header               = "Suggested start location",
                     .items                = location_names(),
                     .selected_index       = 0,
                     .width                = 200.0f,
                     .on_selection_changed = [location](std::optional<std::size_t> i) { *location = i.value_or(0); }}
            .build());
    return ui::VerticalStack{.spacing = 8.0f, .content = std::move(children)};
}

// Runs a pick with its button disabled, so it cannot be clicked twice, as each sample does.
// An invalid option is reported where the result would be.
template <typename Pick>
void picking(ui::Property<bool>& enabled, ui::Property<std::string>& output, Pick pick)
{
    enabled = false;
    try
    {
        pick();
    } catch (const std::invalid_argument& e)
    {
        output = std::string{"Invalid picker option: "} + e.what();
    }
    enabled = true;
}

bool write_text_file(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file{path, std::ios::binary}; // UTF-8, no BOM
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

ui::Spec storage_pickers_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<PickersState>(
        [&window](PickersState& s)
        {
            return examples({
                ui::InfoBar{.message = "The picker reopens in the last selected location and view. The suggested start "
                                       "location is only applied the first time the picker is opened (for example, "
                                       "right after installation or when no previous selection exists). The view mode "
                                       "is accepted and not applied, as in the Windows App SDK's own pickers.",
                            .is_open     = true,
                            .closable    = false,
                            .margin      = ui::Edges{.top = 8.0f}},
                example({
                    .title   = "Pick single file",
                    .code    = R"cpp(
                        ui::Property<std::string> output{"No file picked"};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Button{.text     = "Pick a single file",
                                           .on_click = [&]
                                           {
                                               // Blocks until the dialog closes; nothing when it was canceled.
                                               const auto file = ui::pick_file(window,
                                                                               {.commit_button_text = "Pick File",
                                                                                .extensions         = {".jpg", ".png"},
                                                                                .start_location = ui::PickerLocation::Documents});
                                               output = file ? "Picked: " + ui::path_to_utf8(*file)
                                                             : std::string{"No file selected."};
                                           }},
                                ui::Text{.text = output},
                            },
                        }
                    )cpp",
                    .options = picker_options(&s.single, &s.single.commit_text, "Open", &s.single.location),
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Button{.text     = "Pick a single file",
                                           .horizontal_alignment  = ui::Alignment::Leading,
                                           .enabled  = s.single_enabled,
                                           .on_click = [&s, &window]
                                           {
                                               picking(s.single_enabled,
                                                       s.single_output,
                                                       [&]
                                                       {
                                                           const auto file = ui::pick_file(window, open_picker(s.single, false));
                                                           s.single_output = file ? "Picked: " + ui::path_to_utf8(*file) : std::string{"No file selected."};
                                                       });
                                           }},
                                ui::Text{.text = s.single_output},
                            }}},
                }),
                example({
                    .title    = "Pick multiple files",
                    .code     = R"cpp(
                        ui::Property<std::string> output{"No files picked"};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Button{.text     = "Pick multiple files",
                                           .on_click = [&]
                                           {
                                               // "*" is every file; empty when the dialog was canceled.
                                               const auto files = ui::pick_files(window,
                                                                                 {.commit_button_text = "Pick Files",
                                                                                  .extensions         = {"*"},
                                                                                  .start_location = ui::PickerLocation::Documents});
                                               std::string picked;
                                               for (const std::filesystem::path& file : files)
                                               {
                                                   picked += "- Picked: " + ui::path_to_utf8(file) + "\n";
                                               }
                                               output = files.empty() ? std::string{"No files selected."} : picked;
                                           }},
                                ui::Text{.text = output},
                            },
                        }
                    )cpp",
                    .options  = picker_options(&s.multiple,
                                              &s.multiple.commit_text,
                                              "Open",
                                              &s.multiple.location),
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Button{.text     = "Pick multiple files",
                                           .horizontal_alignment  = ui::Alignment::Leading,
                                           .enabled  = s.multiple_enabled,
                                           .on_click = [&s, &window]
                                           {
                                               picking(s.multiple_enabled,
                                                       s.multiple_output,
                                                       [&]
                                                       {
                                                           const auto files = ui::pick_files(window, open_picker(s.multiple, true));
                                                           std::string output;
                                                           for (const std::filesystem::path& file : files)
                                                           {
                                                               output += "- Picked: " + ui::path_to_utf8(file) + "\n";
                                                           }
                                                           s.multiple_output = files.empty() ? std::string{"No files selected."} : output;
                                                       });
                                           }},
                                ui::Text{.text = s.multiple_output},
                            }}},
                }),
                example({
                    .title = "Save file",
                    .code  = R"cpp(
                        ui::Property<std::string> content{"Hello, libwinui!"};
                        ui::Property<std::string> output{"No file saved"};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::TextBox{.header         = "File content",
                                            .text           = content,
                                            .accepts_return = true,
                                            .wrapping       = ui::TextWrapping::Wrap,
                                            .width          = 500,
                                            .height         = 200},
                                ui::Button{.text     = "Save a file",
                                           .on_click = [&]
                                           {
                                               const auto path = ui::pick_save_file(
                                                   window,
                                                   {.commit_button_text = "Save File",
                                                    .file_types         = {{.name = "Text Files", .extensions = {".txt"}}, {.name = "JSON Files", .extensions = {".json"}}},
                                                    .default_extension  = ".txt",
                                                    .suggested_name     = "NewDocument",
                                                    .start_location     = ui::PickerLocation::Documents});
                                               if (!path)
                                               {
                                                   output = "File save canceled.";
                                                   return;
                                               }
                                               std::ofstream file{*path, std::ios::binary};
                                               file << content.get();
                                               output = "File saved to: " + ui::path_to_utf8(*path);
                                           }},
                                ui::Text{.text = output},
                            },
                        }
                    )cpp",
                    .options =
                        ui::VerticalStack{
                            .spacing = 8.0f,
                            .content =
                                {
                                    ui::Text{.text = "File types:"},
                                    ui::CheckBox{.text               = "Text Files (*.txt)",
                                                 .on_checked_changed = [&s](std::optional<bool> on) { s.txt = on == true; }},
                                    ui::CheckBox{.text               = "JSON Files (*.json)",
                                                 .on_checked_changed = [&s](std::optional<bool> on) { s.json = on == true; }},
                                    ui::CheckBox{.text               = "XML Files (*.xml)",
                                                 .on_checked_changed = [&s](std::optional<bool> on) { s.xml = on == true; }},
                                    ui::ComboBox{.header               = "Default extension",
                                                 .items                = names(Extensions),
                                                 .selected_index       = 0,
                                                 .width                = 200.0f,
                                                 .on_selection_changed = [&s](std::optional<std::size_t> i) { s.default_extension = i.value_or(0); }},
                                    ui::TextBox{.header          = "Suggested file name",
                                                .text            = s.suggested_name,
                                                .on_text_changed = [&s](const std::string& text) { s.suggested_name = text; }},
                                    ui::TextBox{.header           = "Commit button text",
                                                .placeholder_text = "Save",
                                                .text             = s.save_commit_text,
                                                .on_text_changed  = [&s](const std::string& text) { s.save_commit_text = text; }},
                                    ui::ComboBox{.header               = "Suggested start location",
                                                 .items                = location_names(),
                                                 .selected_index       = 0,
                                                 .width                = 200.0f,
                                                 .on_selection_changed = [&s](std::optional<std::size_t> i) { s.save_location = i.value_or(0); }},
                                    ui::Grid{
                                        .columns = {1_fr, ui::AutoSize},
                                        .content =
                                            {
                                                ui::Cell{.row     = 0,
                                                 .column  = 0,
                                                 .content = ui::TextBox{.header           = "Suggested folder ",
                                                                        .placeholder_text = "Optional",
                                                                        .text             = s.suggested_folder,
                                                                        .read_only        = true,
                                                                        .width            = 148.0f}},
                                                ui::Cell{.row     = 0,
                                                 .column  = 1,
                                                 .content = ui::Button{
                                                     .margin             = ui::Edges{.left = 8.0f},
                                                     .vertical_alignment = ui::Alignment::Trailing,
                                                     .tool_tip           = "Select folder",
                                                     .enabled            = s.folder_button_enabled,
                                                     .accessibility      = {.name = "Select folder"},
                                                     .on_click           = [&s, &window]
                                                     {
                                                         picking(s.folder_button_enabled,
                                                                 s.save_output,
                                                                 [&]
                                                                 {
                                                                     if (auto folder = ui::pick_folder(window, {.commit_button_text = "Select folder"}))
                                                                     {
                                                                         s.suggested_folder = ui::path_to_utf8(*folder);
                                                                     }
                                                                 });
                                                     },
                                                     .content = ui::Icon{.source = ui::IconGlyph{0xF89A, "Segoe Fluent Icons", 14.0f}}}},
                                            }},
                                    ui::CheckBox{.text               = "Remember the last folder",
                                                 .checked            = true,
                                                 .on_checked_changed = [&s](std::optional<bool> on) { s.remember_last = on.value_or(false); }},
                                }},
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::TextBox{.header          = "File content",
                                            .text            = s.content,
                                            .spell_check     = false,
                                            .accepts_return  = true,
                                            .wrapping        = ui::TextWrapping::Wrap,
                                            .width           = 500.0f,
                                            .height          = 200.0f,
                                            .horizontal_alignment         = ui::Alignment::Leading,
                                            .on_text_changed = [&s](const std::string& text) { s.content = text; }},
                                ui::Button{.text     = "Save a file",
                                           .horizontal_alignment  = ui::Alignment::Leading,
                                           .enabled  = s.save_enabled,
                                           .on_click = [&s, &window]
                                           {
                                               picking(s.save_enabled,
                                                       s.save_output,
                                                       [&]
                                                       {
                                                           ui::PickSaveFileOptions picker{.commit_button_text = s.save_commit_text,
                                                                                           .default_extension  = Extensions[s.default_extension],
                                                                                           .suggested_name     = s.suggested_name,
                                                                                           .start_location     = Locations[s.save_location].second,
                                                                                           .folder = ui::path_from_utf8(s.suggested_folder.get()),
                                                                                           .remember_last = s.remember_last};
                                                           if (s.txt)
                                                           {
                                                               picker.file_types.push_back({.name = "Text Files", .extensions = {".txt"}});
                                                           }
                                                           if (s.json)
                                                           {
                                                               picker.file_types.push_back({.name = "JSON Files", .extensions = {".json"}});
                                                           }
                                                           if (s.xml)
                                                           {
                                                               picker.file_types.push_back({.name = "XML Files", .extensions = {".xml"}});
                                                           }
                                                           if (auto path = ui::pick_save_file(window, picker))
                                                           {
                                                               s.save_output = write_text_file(*path, s.content)
                                                                                   ? "File saved to: " + ui::path_to_utf8(*path)
                                                                                   : "Could not write " + ui::path_to_utf8(*path);
                                                           }
                                                           else
                                                           {
                                                               s.save_output = "File save canceled.";
                                                           }
                                                       });
                                           }},
                                ui::Text{.text = s.save_output},
                            }}},
                }),
                example({
                    .title    = "Pick folder",
                    .code     = R"cpp(
                        ui::Property<std::string> output{"No folder picked"};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Button{.text     = "Pick a folder",
                                           .on_click = [&]
                                           {
                                               const auto folder = ui::pick_folder(window,
                                                                                   {.commit_button_text = "Pick Folder",
                                                                                    .start_location = ui::PickerLocation::Documents});
                                               output = folder ? "Picked: " + ui::path_to_utf8(*folder)
                                                               : std::string{"No folder selected."};
                                           }},
                                ui::Text{.text = output},
                            },
                        }
                    )cpp",
                    .options  = picker_options(nullptr,
                                              &s.folder_commit_text,
                                              "Select Folder",
                                              &s.folder_location),
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Button{.text     = "Pick a folder",
                                           .horizontal_alignment  = ui::Alignment::Leading,
                                           .enabled  = s.folder_enabled,
                                           .on_click = [&s, &window]
                                           {
                                               picking(s.folder_enabled,
                                                       s.folder_output,
                                                       [&]
                                                       {
                                                           const auto folder = ui::pick_folder(window,
                                                                                               {.commit_button_text = s.folder_commit_text,
                                                                                                .start_location     = Locations[s.folder_location].second});
                                                           s.folder_output   = folder ? "Picked: " + ui::path_to_utf8(*folder)
                                                                                      : std::string{"No folder selected."};
                                                       });
                                           }},
                                ui::Text{.text = s.folder_output},
                            }}},
                }),
            });
        });
}

// ---- Drag and Drop ----

struct DropState
{
    ui::Property<std::string> dropped{"Nothing dropped yet."};
};

ui::Color drop_zone_border(const ui::Theme& theme)
{
    return theme.surfaces.field_border;
}

ui::Spec drop_files_page(Context& /*ctx*/)
{
    return with_state<DropState>(
        [](DropState& s)
        {
            std::unique_ptr<ui::Element> zone =
                ui::Border{
                    .border_brush         = drop_zone_border,
                    .border_thickness     = ui::Thickness{1.0f},
                    .corner_radius        = 8.0f,
                    .padding              = ui::Thickness{24.0f},
                    .width                = 360.0f,
                    .horizontal_alignment = ui::Alignment::Leading,
                    .content              = ui::Text{.text = "Drop files here"},
                }
                    .build();
            zone->set_allow_drop(true);
            zone->on_drag_over(
                [](const ui::DragEventArgs& e)
                {
                    if (e.data.files.empty())
                    {
                        return ui::DragResponse{};
                    }
                    return ui::DragResponse{.operation = ui::DataPackageOperation::Copy,
                                            .caption   = std::format("Add {} file(s)", e.data.files.size())};
                });
            zone->on_drop(
                [&s](const ui::DragEventArgs& e)
                {
                    std::string list;
                    for (const std::filesystem::path& file : e.data.files)
                    {
                        list += "- " + ui::path_to_utf8(file.filename()) + "\n";
                    }
                    s.dropped = std::format("Dropped {} file(s):\n{}", e.data.files.size(), list);
                });
            return examples({
                example({
                    .title    = "Drop files from the file manager",
                    .code     = R"cpp(
                        ui::Property<std::string> dropped{"Nothing dropped yet."};

                        // A drop target is any control with allow_drop set.
                        std::unique_ptr<ui::Element> zone = ui::Border{
                            .border_thickness = ui::Thickness{1},
                            .padding          = ui::Thickness{24},
                            .content          = ui::Text{.text = "Drop files here"},
                        }.build();
                        zone->set_allow_drop(true);
                        zone->on_drag_over([](const ui::DragEventArgs& e)
                        {
                            if (e.data.files.empty())
                            {
                                return ui::DragResponse{}; // refused
                            }
                            return ui::DragResponse{.operation = ui::DataPackageOperation::Copy,
                                                    .caption   = std::format("Add {} file(s)", e.data.files.size())};
                        });
                        zone->on_drop([&](const ui::DragEventArgs& e)
                        {
                            dropped = std::format("Dropped {} file(s)", e.data.files.size());
                        });
                    )cpp",
                    .controls = {ui::VerticalStack{.spacing = 8.0f,
                                                   .content =
                                                       {
                                                           ui::Spec{std::move(zone)},
                                                           ui::Text{.text = s.dropped},
                                                       }}},
                }),
            });
        });
}

// ---- Message Box ----

struct MessageBoxState
{
    ui::Property<std::string> question_answer{"No answer yet."};
    ui::Property<std::string> error_answer{};
};

const char* answer_name(ui::MessageBoxResult result)
{
    switch (result)
    {
        case ui::MessageBoxResult::Ok:
            return "OK";
        case ui::MessageBoxResult::Cancel:
            return "Cancel";
        case ui::MessageBoxResult::Yes:
            return "Yes";
        case ui::MessageBoxResult::No:
            return "No";
        case ui::MessageBoxResult::Retry:
            return "Retry";
    }
    return "";
}

// ---- Printing ------------------------------------------------------------------------------------

struct PrintingState
{
    ui::Property<std::string> document_result{"Nothing printed yet."};
    ui::Property<std::string> control_result{"Nothing printed yet."};
};

const char* print_result_name(ui::PrintResult result)
{
    switch (result)
    {
        case ui::PrintResult::Printed:
            return "The pages went to the printer.";
        case ui::PrintResult::Canceled:
            return "Printing was canceled.";
        case ui::PrintResult::Failed:
            return "The printer refused the job.";
    }
    return "";
}

// A three-page document: a chapter title, a paragraph and a page number on each page.
ui::PrintJob sample_document(ui::PrintDialog dialog)
{
    return ui::PrintJob{
        .title      = "libwinui sample document",
        .page_count = [](const ui::PrintPage&, const ui::PrintContext&) -> std::size_t { return 3; },
        .draw_page =
            [](ui::Renderer& renderer, const ui::PrintPage& page, const ui::PrintContext& context)
        {
            // Half an inch inside what the printer can reach.
            const ui::Rect area{page.printable.x + 48.0f,
                                page.printable.y + 48.0f,
                                page.printable.width - 96.0f,
                                page.printable.height - 96.0f};
            ui::TextStyle  title = context.theme.text_style;
            title.font_size      = 28.0f;
            title.weight         = ui::FontWeight::SemiBold;
            title.horizontal     = ui::TextAlign::Leading;
            title.vertical       = ui::TextAlign::Leading;
            renderer.draw_text(std::format("Chapter {}", page.index + 1),
                               ui::Rect{area.x, area.y, area.width, 40.0f},
                               ui::colors::Black,
                               title);

            ui::TextStyle body = context.theme.text_style;
            body.horizontal    = ui::TextAlign::Leading;
            body.vertical      = ui::TextAlign::Leading;
            body.wrapping      = ui::TextWrapping::Wrap;
            renderer.draw_text(
                "Printed pages are drawn with the same renderer as the screen, so text stays "
                "text and lines stay sharp at any printer resolution. Each page is drawn when "
                "the preview or the printer asks for it.",
                ui::Rect{area.x, area.y + 56.0f, area.width, area.height - 100.0f},
                ui::colors::Black,
                body);

            ui::TextStyle footer = context.theme.text_style;
            footer.vertical      = ui::TextAlign::Trailing;
            renderer.draw_text(std::format("Page {} of 3", page.index + 1), area, ui::Color::from_rgb(96, 96, 96), footer);
        },
        .dialog = dialog,
    };
}

// The card the second sample shows on screen and prints.
ui::Spec shipping_label()
{
    return ui::VerticalStack{
        .spacing   = 4.0f,
        .max_width = 320.0f,
        .content =
            {
                ui::Text{.text = "Shipping label", .role = ui::TextRole::Subtitle},
                ui::Text{.text = "Jane Doe"},
                ui::Text{.text = "12 Harbor Road"},
                ui::Text{.text = "Springfield"},
                ui::CheckBox{.text = "Fragile", .checked = true},
            },
    };
}

ui::Spec printing_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<PrintingState>(
        [&window](PrintingState& s)
        {
            return examples({
                example({
                    .title    = "Print a document",
                    .code     = R"cpp(
                        ui::Property<std::string> result;

                        ui::Button{.text     = "Print...",
                                   .on_click = [&]
                                   {
                                       // Waits until the pages are spooled or the user cancels.
                                       const ui::PrintResult printed = ui::print(window, ui::PrintJob{
                                           .title      = "Sample document",
                                           .page_count = [](const ui::PrintPage&, const ui::PrintContext&) -> std::size_t
                                           { return 3; },
                                           .draw_page  = [](ui::Renderer& renderer, const ui::PrintPage& page,
                                                            const ui::PrintContext& context)
                                           {
                                               ui::TextStyle style = context.theme.text_style;
                                               style.font_size     = 28;
                                               renderer.draw_text(std::format("Chapter {}", page.index + 1),
                                                                  page.printable, ui::colors::Black, style);
                                           },
                                       });
                                       result = printed == ui::PrintResult::Printed ? "Printed." : "Not printed.";
                                   }}
                    )cpp",
                    .output   = ui::Text{.text = s.document_result, .horizontal_alignment = ui::Alignment::Leading},
                    .controls = {ui::HorizontalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Button{.text     = "Print with a preview...",
                                           .on_click = [&s, &window]
                                           {
                                               s.document_result = print_result_name(
                                                   ui::print(window, sample_document(ui::PrintDialog::Preview)));
                                           }},
                                ui::Button{.text     = "Print with the classic dialog...",
                                           .on_click = [&s, &window]
                                           {
                                               s.document_result = print_result_name(
                                                   ui::print(window, sample_document(ui::PrintDialog::Classic)));
                                           }},
                            }}},
                }),
                example({
                    .title    = "Print a control",
                    .code     = R"cpp(
                        ui::Spec shipping_label();   // the card shown above

                        ui::Button{.text     = "Print the label...",
                                   .on_click = [&]
                                   {
                                       ui::print(window, ui::PrintJob{
                                           .title      = "Shipping label",
                                           .page_count = [](const ui::PrintPage&, const ui::PrintContext&) -> std::size_t
                                           { return 1; },
                                           .draw_page  = [](ui::Renderer& renderer, const ui::PrintPage& page,
                                                            const ui::PrintContext& context)
                                           {
                                               // Built for the page, laid out in the printable area and drawn.
                                               std::unique_ptr<ui::Element> label = ui::build(shipping_label());
                                               ui::draw_element(renderer, *label, page.printable, context);
                                           },
                                       });
                                   }}
                    )cpp",
                    .output   = ui::Text{.text = s.control_result, .horizontal_alignment = ui::Alignment::Leading},
                    .controls = {ui::VerticalStack{
                        .spacing = 12.0f,
                        .content =
                            {
                                shipping_label(),
                                ui::Button{
                                    .text                 = "Print the label...",
                                    .horizontal_alignment = ui::Alignment::Leading,
                                    .on_click             = [&s, &window]
                                    {
                                        s.control_result = print_result_name(ui::print(
                                            window,
                                            ui::PrintJob{
                                                .title = "Shipping label",
                                                .page_count = [](const ui::PrintPage&, const ui::PrintContext&) -> std::size_t
                                                { return 1; },
                                                .draw_page =
                                                    [](ui::Renderer& renderer, const ui::PrintPage& page, const ui::PrintContext& context)
                                                {
                                                    std::unique_ptr<ui::Element> label = ui::build(shipping_label());
                                                    ui::draw_element(renderer, *label, page.printable, context);
                                                },
                                            }));
                                    }},
                            }}},
                }),
            });
        });
}

ui::Spec message_box_page(Context& ctx)
{
    ui::Window& window = ctx.window;
    return with_state<MessageBoxState>(
        [&window](MessageBoxState& s)
        {
            return examples({
                example({
                    .title    = "Ask a question",
                    .code     = R"cpp(
                        ui::Property<std::string> answer{"No answer yet."};

                        ui::VerticalStack{
                            .spacing = 8,
                            .content = {
                                ui::Button{.text     = "Close the document",
                                           .on_click = [&]
                                           {
                                               // Waits until the user picks a button.
                                               const ui::MessageBoxResult result = ui::message_box(window,
                                                   {.title   = "Notes",
                                                    .heading = "Save changes to todo.txt?",
                                                    .text    = "Your changes are lost if you don't save them.",
                                                    .buttons = ui::MessageBoxButtons::YesNoCancel,
                                                    .icon    = ui::MessageBoxIcon::Warning});
                                               answer = result == ui::MessageBoxResult::Yes ? "Saved." : "Not saved.";
                                           }},
                                ui::Text{.text = answer},
                            },
                        }
                    )cpp",
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Button{.text                 = "Close the document",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .on_click             = [&s, &window]
                                           {
                                               const ui::MessageBoxResult result = ui::message_box(
                                                   window,
                                                   {.title   = "Notes",
                                                    .heading = "Save changes to todo.txt?",
                                                    .text    = "Your changes are lost if you don't save them.",
                                                    .buttons = ui::MessageBoxButtons::YesNoCancel,
                                                    .icon    = ui::MessageBoxIcon::Warning});
                                               s.question_answer = std::format("You chose {}.", answer_name(result));
                                           }},
                                ui::Text{.text = s.question_answer},
                            }}},
                }),
                example({
                    .title    = "Report an error",
                    .code     = R"cpp(
                        ui::Button{.text     = "Connect",
                                   .on_click = [&]
                                   {
                                       ui::message_box(window,
                                                       {.title   = "Mail",
                                                        .heading = "The server did not answer.",
                                                        .text    = "Check the connection and try again.",
                                                        .buttons = ui::MessageBoxButtons::RetryCancel,
                                                        .icon    = ui::MessageBoxIcon::Error});
                                   }}
                    )cpp",
                    .controls = {ui::VerticalStack{
                        .spacing = 8.0f,
                        .content =
                            {
                                ui::Button{.text                 = "Connect",
                                           .horizontal_alignment = ui::Alignment::Leading,
                                           .on_click             = [&s, &window]
                                           {
                                               const ui::MessageBoxResult result = ui::message_box(
                                                   window,
                                                   {.title   = "Mail",
                                                    .heading = "The server did not answer.",
                                                    .text    = "Check the connection and try again.",
                                                    .buttons = ui::MessageBoxButtons::RetryCancel,
                                                    .icon    = ui::MessageBoxIcon::Error});
                                               s.error_answer = std::format("You chose {}.", answer_name(result));
                                           }},
                                ui::Text{.text = s.error_answer},
                            }}},
                }),
            });
        });
}

} // namespace

std::span<const Page> system_pages()
{
    // No content island page: libwinui has no content islands.
    static const auto Pages = std::to_array<Page>({
        {"Clipboard", "Copy and paste text, images, and files to and from the system Clipboard.", clipboard_page},
        {"Drag and Drop", "Take files dropped on the window from the file manager or another app.", drop_files_page},
        {"Message Box", "Ask a question or report a problem in the operating system's own message box.", message_box_page},
        {"Notifications", "Show a toast in the corner of the screen that also goes to the notification center.", notifications_page},
        {"Printing", "Print a document or a control through the system print dialog, with a live preview.", printing_page},
        {"Single Instance",
         "Keep one running copy of the program, hand it the command line of every later start, and add tasks "
         "to its jump list.",
         single_instance_page},
        {"Storage Pickers", "Select files and folders with modern system pickers.", storage_pickers_page},
    });
    return Pages;
}

ui::Callback<void(ui::InstanceActivation)> report_handover(ui::Property<std::string>& status, ui::Window& window)
{
    return [&status, &window](ui::InstanceActivation started)
    {
        std::string arguments;
        for (std::size_t i = 1; i < started.arguments.size(); ++i)
        {
            arguments += " " + started.arguments[i];
        }
        status = arguments.empty() ? std::string{"A second start handed over no arguments."}
                                   : "A second start handed over:" + arguments;
        window.activate();
    };
}

} // namespace gallery
