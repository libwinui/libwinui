// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Index.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ui
{

class Window;

/// The known folder a picker opens in when no `folder` is given, the first time or every time
/// as `remember_last` says. Unspecified leaves it to the dialog.
enum class PickerLocation
{
    Unspecified, ///< No preference: the dialog's own default.
    Documents,   ///< The Documents folder.
    Computer,    ///< "This PC".
    Desktop,     ///< The Desktop.
    Downloads,   ///< The Downloads folder.
    Music,       ///< The Music folder.
    Pictures,    ///< The Pictures folder.
    Videos,      ///< The Videos folder.
    Objects3D,   ///< The 3D Objects folder.
};

/// One entry of a picker's type box: a name and the extensions it stands for (".txt"; "*" for
/// everything).
///
/// Example:
/// @code
/// const ui::FileType images{.name = "Images", .extensions = {".png", ".jpg", ".jpeg"}};
/// @endcode
struct FileType
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    std::string                                       name; ///< The type box label, UTF-8.
    std::vector<std::string> extensions;                    ///< Each "." plus letters, or "*"; becomes "*.png;*.jpg".
};

/// What pick_file() and pick_files() are asked for. Every field is optional.
///
/// Example:
/// @code
/// ui::Property<std::string> file_name;
/// if (auto path = ui::pick_file(window, {.title      = "Pick an image",
///                                        .file_types = {{.name = "Images", .extensions = {".png", ".jpg"}}}}))
/// {
///     file_name = ui::path_to_utf8(path->filename());
/// }
/// @endcode
struct PickFileOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    std::string                                       title{}; ///< The dialog caption; empty keeps the default.
    std::string commit_button_text{};                          ///< The Open button's label; empty keeps "Open".
    /// Named types for the type box. They win over `extensions`.
    std::vector<FileType> file_types{};
    /// Plain extensions (".txt", or "*"), one type-box entry each, plus one for all of them and
    /// one for all files. Used when `file_types` is empty.
    std::vector<std::string> extensions{};
    /// Which type-box entry starts selected, zero-based. Unset (the default) selects the
    /// first named type, or with `extensions` the entry for all of them.
    std::optional<std::size_t> initial_file_type{};
    /// The known folder to open in when `folder` is empty.
    PickerLocation start_location = PickerLocation::Unspecified;
    /// The folder to open in, if it exists; absolute. Wins over `start_location`.
    std::filesystem::path folder{};
    /// True: `folder` (or `start_location`) opens the first time, and after that the dialog
    /// reopens wherever the user last picked from. False: it opens every time.
    bool remember_last = true;
};

/// What pick_save_file() is asked for. Every field is optional.
///
/// Example:
/// @code
/// auto path = ui::pick_save_file(window, {.file_types     = {{.name = "Text", .extensions = {".txt"}}},
///                                         .suggested_name = "Notes"});
/// @endcode
struct PickSaveFileOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    std::string                                       title{}; ///< The dialog caption; empty keeps the default.
    std::string commit_button_text{};                          ///< The Save button's label; empty keeps "Save".
    /// Named types for the type box. They win over `extensions`; "All Files" when both are empty.
    std::vector<FileType> file_types{};
    /// Plain extensions (".txt"), one type-box entry each, plus one for all of them. Used when
    /// `file_types` is empty.
    std::vector<std::string> extensions{};
    /// Which type-box entry starts selected, zero-based. Unset (the default) selects the
    /// first named type, or with `extensions` the entry for all of them.
    std::optional<std::size_t> initial_file_type{};
    /// Appended when the user types a name without one (".txt"); empty: the selected type's first.
    std::string default_extension{};
    /// The name the dialog opens with.
    std::string suggested_name{};
    /// The known folder to open in when `folder` is empty.
    PickerLocation start_location = PickerLocation::Unspecified;
    /// The folder to open in, if it exists; absolute. Wins over `start_location`.
    std::filesystem::path folder{};
    /// True: `folder` (or `start_location`) opens the first time, and after that the dialog
    /// reopens wherever the user last picked from. False: it opens every time.
    bool remember_last = true;
    /// Ask before replacing an existing file.
    bool overwrite_prompt = true;
};

/// What pick_folder() and pick_folders() are asked for. Every field is optional.
///
/// Example:
/// @code
/// if (auto folder = ui::pick_folder(window, {.title = "Choose a library"}))
/// {
///     window.set_title(ui::path_to_utf8(folder->filename()));
/// }
/// @endcode
struct PickFolderOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    std::string                                       title{}; ///< The dialog caption; empty keeps the default.
    std::string commit_button_text{};                          ///< The Select button's label; empty keeps the default.
    /// The known folder to open in when `folder` is empty.
    PickerLocation start_location = PickerLocation::Unspecified;
    /// The folder to open in, if it exists; absolute. Wins over `start_location`.
    std::filesystem::path folder{};
    /// True: `folder` (or `start_location`) opens the first time, and after that the dialog
    /// reopens wherever the user last picked from. False: it opens every time.
    bool remember_last = true;
};

/// The system's "open file" dialog, modal to `owner`. Returns the chosen file, or nothing if the
/// user canceled. Blocks until the dialog closes; the window keeps painting meanwhile. Throws
/// std::invalid_argument for a malformed extension, index or folder.
///
/// Example:
/// @code
/// ui::Property<std::string> file_name;
/// window.set_content(ui::Button{.text = "Open...", .on_click = [&] {
///     if (auto path = ui::pick_file(window, {.title      = "Pick an image",
///                                            .file_types = {{.name = "Images", .extensions = {".png", ".jpg"}}}}))
///     {
///         file_name = ui::path_to_utf8(path->filename());
///     }
/// }});
/// @endcode
[[nodiscard]] std::optional<std::filesystem::path> pick_file(const Window& owner, const PickFileOptions& options = {});

/// As pick_file(), letting the user choose several files. Empty if the user canceled.
[[nodiscard]] std::vector<std::filesystem::path> pick_files(const Window& owner, const PickFileOptions& options = {});

/// The system's "save file" dialog, modal to `owner`. Returns the path to write, or nothing if
/// the user canceled. It creates no file.
[[nodiscard]] std::optional<std::filesystem::path> pick_save_file(const Window&              owner,
                                                                  const PickSaveFileOptions& options = {});

/// The system's folder dialog, modal to `owner`. Returns the chosen folder, or nothing if the
/// user canceled.
[[nodiscard]] std::optional<std::filesystem::path> pick_folder(const Window& owner, const PickFolderOptions& options = {});

/// As pick_folder(), letting the user choose several folders. Empty if the user canceled.
[[nodiscard]] std::vector<std::filesystem::path> pick_folders(const Window& owner, const PickFolderOptions& options = {});

} // namespace ui
