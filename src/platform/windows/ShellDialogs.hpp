// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/core/NativeHandle.hpp"
#include "winui/core/Strings.hpp"
#include "winui/ui/Pickers.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ui
{
class Window;
}

namespace ui::win32
{

// The backend's own picker types: ui::pick_file() and friends (winui/ui/Pickers.hpp) are built on
// them. Nothing here is installed.
using FileTypeChoice = ui::FileType;

// ---- how the pickers' options become a dialog's type box (tested in test_storage_pickers.cpp)
/// One entry of a dialog's type box as the OS wants it: a label and a wildcard pattern.
struct FileDialogFilter
{
    std::string name;    ///< Shown in the type box, UTF-8, e.g. "Text files".
    std::string pattern; ///< One or more wildcards separated by ';', e.g. "*.txt" or "*.jpg;*.png".
};

/// The filters a picker hands the dialog, and which one starts selected (zero-based; -1 leaves
/// the dialog's own choice, the first).
struct PickerFileTypes
{
    std::vector<FileDialogFilter> filters;            ///< In type-box order.
    int                           initial_index = -1; ///< The one selected at first; -1: the dialog's.
};

/// Builds the type box from plain extensions (`filter`) or named types (`choices`, which win).
/// Throws std::invalid_argument for a malformed extension or an index out of range.
[[nodiscard]] PickerFileTypes picker_file_types(std::span<const std::string> filter,
                                                std::span<const FileType>    choices,
                                                int                          initial_index = -1,
                                                const std::string& all_files_text = strings().file_picker.all_files);

/// Returns the chosen file's path, or nullopt if the user canceled or the dialog
/// couldn't be created. `owner` is the HWND the dialog is modal to (nullptr: unowned); prefer the
/// Window overload.
[[nodiscard]] std::optional<std::filesystem::path> show_open_file_dialog(NativeHandle       owner,
                                                                         const std::string& title,
                                                                         std::span<const FileDialogFilter> filters = {});

/// The path to save to (the file is not created), or nullopt if the user canceled or the
/// dialog couldn't be created. `default_file_name` pre-fills the name box; the dialog asks
/// before overwriting an existing file.
[[nodiscard]] std::optional<std::filesystem::path> show_save_file_dialog(
    NativeHandle                      owner,
    const std::string&                title,
    std::span<const FileDialogFilter> filters           = {},
    const std::string&                default_file_name = "");

/// Returns the chosen folder's path, or nullopt if the user canceled or the dialog couldn't be
/// created. Only file-system folders can be picked, since a library or "This PC" has no path.
[[nodiscard]] std::optional<std::filesystem::path> show_folder_dialog(NativeHandle owner, const std::string& title);

// ---- owned by a ui::Window ----

/// show_open_file_dialog() modal to `owner`, whose HWND the host resolves; focus returns to the
/// window afterwards.
///
/// Example:
/// @code
/// window.set_content(ui::Button{.text = "Open...", .on_click = [&] {
///     const ui::win32::FileDialogFilter images[] = {{"Images", "*.png;*.jpg"}};
///     if (auto path = ui::win32::show_open_file_dialog(window, "Pick an image", images))
///     {
///         file_name = ui::path_to_utf8(path->filename());
///     }
/// }});
/// @endcode
[[nodiscard]] std::optional<std::filesystem::path> show_open_file_dialog(const Window&      owner,
                                                                         const std::string& title,
                                                                         std::span<const FileDialogFilter> filters = {});

/// show_save_file_dialog() modal to `owner`.
[[nodiscard]] std::optional<std::filesystem::path> show_save_file_dialog(
    const Window&                     owner,
    const std::string&                title,
    std::span<const FileDialogFilter> filters           = {},
    const std::string&                default_file_name = "");

/// show_folder_dialog() modal to `owner`.
[[nodiscard]] std::optional<std::filesystem::path> show_folder_dialog(const Window& owner, const std::string& title);

// ---- storage pickers (Microsoft.Windows.Storage.Pickers) ----

/// PickerViewMode. Accepted and ignored, as the Windows App SDK's own pickers do: they
/// validate and store it, and ConfigureDialog never hands it to the dialog, which opens in the
/// view it was last left in.
enum class PickerViewMode
{
    List,      ///< Details/list view.
    Thumbnail, ///< Large-icon view.
};

/// PickerLocationId: the known folder a picker opens in the first time (the dialog remembers
/// the last folder after that). Unspecified leaves it to the dialog.
enum class PickerLocationId
{
    DocumentsLibrary, ///< The Documents folder.
    ComputerFolder,   ///< "This PC".
    Desktop,          ///< The Desktop.
    Downloads,        ///< The Downloads folder.
    MusicLibrary = 5, ///< The Music folder (4, HomeGroup, is gone from WinRT's numbering).
    PicturesLibrary,  ///< The Pictures folder.
    VideosLibrary,    ///< The Videos folder.
    Objects3D,        ///< The 3D Objects folder.
    Unspecified,      ///< No preference: the dialog's own default.
};

/// FileOpenPicker. The dialog opens in `suggested_folder` if that exists, else wherever the
/// user last was, and the first time in `suggested_start_folder` if that exists, else
/// `suggested_start_location`. Throws std::invalid_argument for an invalid filter (see
/// picker_file_types()) or a folder path that is not absolute.
///
/// Example:
/// @code
/// window.set_content(ui::Button{.text = "Add photos...", .on_click = [&] {
///     const ui::win32::FileOpenPicker picker{
///         .suggested_start_location = ui::win32::PickerLocationId::PicturesLibrary,
///         .commit_button_text       = "Add",
///         .file_type_filter         = {".png", ".jpg"},
///     };
///     for (const std::filesystem::path& file : picker.pick_multiple_files(window))
///     {
///         photos.update([&](std::vector<std::string>& v) { v.push_back(ui::path_to_utf8(file)); });
///     }
/// }});
/// @endcode
struct FileOpenPicker
{
    PickerViewMode              view_mode                = PickerViewMode::List;          ///< Ignored.
    PickerLocationId            suggested_start_location = PickerLocationId::Unspecified; ///< First-use folder.
    std::string                 commit_button_text{};         ///< The Open button's label; empty keeps "Open".
    std::string                 title{};                      ///< The dialog caption; empty keeps the default.
    std::vector<FileTypeChoice> file_type_choices{};          ///< Named types; win over `file_type_filter`.
    std::vector<std::string>    file_type_filter{};           ///< Extensions (".txt", or "*"), one filter each.
    int                         initial_file_type_index = -1; ///< Selected type; see picker_file_types().
    std::filesystem::path       suggested_folder{};           ///< Opened every time, if it exists; absolute.
    std::filesystem::path       suggested_start_folder{};     ///< Opened the first time, if it exists; absolute.
    bool                        remember_last = true; ///< False: the start folder or location opens every time too.

    /// PickSingleFileAsync: the chosen file's path, or nullopt when the user canceled.
    [[nodiscard]] std::optional<std::filesystem::path> pick_single_file(const Window& owner) const;
    /// pick_single_file() owned by a raw HWND (nullptr: unowned).
    [[nodiscard]] std::optional<std::filesystem::path> pick_single_file(NativeHandle owner) const;
    /// PickMultipleFilesAsync: the chosen files' paths; empty when the user canceled.
    [[nodiscard]] std::vector<std::filesystem::path> pick_multiple_files(const Window& owner) const;
    /// pick_multiple_files() owned by a raw HWND (nullptr: unowned).
    [[nodiscard]] std::vector<std::filesystem::path> pick_multiple_files(NativeHandle owner) const;
};

/// FileSavePicker. Its type box is `file_type_choices` alone ("All Files" when there are none),
/// and FOS_STRICTFILETYPES holds the name to the chosen type's extension. Throws as
/// FileOpenPicker does, and for a suggested file name longer than MAX_PATH.
///
/// Example:
/// @code
/// const ui::win32::FileSavePicker picker{
///     .file_type_choices      = {{"Plain text", {".txt"}}, {"Log file", {".log"}}},
///     .default_file_extension = ".txt",
///     .suggested_file_name    = "notes",
/// };
/// if (auto path = picker.pick_save_file(*window))
/// {
///     std::ofstream{*path} << text.get();
/// }
/// @endcode
struct FileSavePicker
{
    PickerLocationId            suggested_start_location = PickerLocationId::Unspecified; ///< First-use folder.
    std::string                 commit_button_text{};         ///< The Save button's label; empty keeps "Save".
    std::string                 title{};                      ///< The dialog caption; empty keeps the default.
    std::vector<FileTypeChoice> file_type_choices{};          ///< The type box; "All Files" when empty.
    std::vector<std::string>    file_type_filter{};           ///< Extensions (".txt"), one filter each.
    int                         initial_file_type_index = -1; ///< Selected type; see picker_file_types().
    /// Appended to a typed name that has no extension (".txt").
    std::string default_file_extension{};
    /// Pre-fills the name box, UTF-8; at most MAX_PATH characters.
    std::string suggested_file_name{};
    /// Opened every time, if it exists; absolute.
    std::filesystem::path suggested_folder{};
    /// Opened the first time, if it exists; absolute.
    std::filesystem::path suggested_start_folder{};
    /// False: the start folder or location opens every time too.
    bool remember_last = true;
    /// Ask before overwriting an existing file (FOS_OVERWRITEPROMPT).
    bool show_overwrite_prompt = true;

    /// PickSaveFileAsync: the path to save to (the file is not created), or nullopt when the
    /// user canceled.
    [[nodiscard]] std::optional<std::filesystem::path> pick_save_file(const Window& owner) const;
    /// pick_save_file() owned by a raw HWND (nullptr: unowned).
    [[nodiscard]] std::optional<std::filesystem::path> pick_save_file(NativeHandle owner) const;
};

/// FolderPicker: file-system folders only (FOS_FORCEFILESYSTEM), since a library or "This PC"
/// has no path. Throws as FileOpenPicker does.
///
/// Example:
/// @code
/// ui::win32::FolderPicker picker{.title = "Choose a library folder"};
/// picker.suggested_start_folder = std::filesystem::path{"C:/Photos"};
/// if (auto folder = picker.pick_single_folder(*window))
/// {
///     library_path = ui::path_to_utf8(*folder);
/// }
/// @endcode
struct FolderPicker
{
    PickerViewMode        view_mode                = PickerViewMode::List;          ///< Ignored (see PickerViewMode).
    PickerLocationId      suggested_start_location = PickerLocationId::Unspecified; ///< First-use folder.
    std::string           commit_button_text{};     ///< The Select button's label; empty keeps the default.
    std::string           title{};                  ///< The dialog caption; empty keeps the default.
    std::filesystem::path suggested_folder{};       ///< Opened every time, if it exists; absolute.
    std::filesystem::path suggested_start_folder{}; ///< Opened the first time, if it exists; absolute.
    bool                  remember_last = true;     ///< False: the start folder or location opens every time too.

    /// PickSingleFolderAsync: the chosen folder's path, or nullopt when the user canceled.
    [[nodiscard]] std::optional<std::filesystem::path> pick_single_folder(const Window& owner) const;
    /// pick_single_folder() owned by a raw HWND (nullptr: unowned).
    [[nodiscard]] std::optional<std::filesystem::path> pick_single_folder(NativeHandle owner) const;
    /// PickMultipleFoldersAsync: the chosen folders' paths; empty when the user canceled.
    [[nodiscard]] std::vector<std::filesystem::path> pick_multiple_folders(const Window& owner) const;
    /// pick_multiple_folders() owned by a raw HWND (nullptr: unowned).
    [[nodiscard]] std::vector<std::filesystem::path> pick_multiple_folders(NativeHandle owner) const;
};

} // namespace ui::win32

#endif // _WIN32
