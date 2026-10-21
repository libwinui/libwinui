// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ShellDialogs.hpp"

#include "Win32String.hpp"
#include "winui/core/NativeHandle.hpp"
#include "winui/ui/Pickers.hpp"
#include "winui/ui/Window.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <shlobj.h>
#include <shobjidl.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace ui::win32
{
namespace
{

using Microsoft::WRL::ComPtr;

std::optional<std::filesystem::path> item_path(IShellItem& item)
{
    PWSTR path = nullptr;
    if (FAILED(item.GetDisplayName(SIGDN_FILESYSPATH, &path)))
    {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

std::optional<std::filesystem::path> result_path(IFileDialog& dialog)
{
    ComPtr<IShellItem> item;
    if (FAILED(dialog.GetResult(&item)))
    {
        return std::nullopt;
    }
    return item_path(*item.Get());
}

std::vector<std::filesystem::path> result_paths(IFileOpenDialog& dialog)
{
    std::vector<std::filesystem::path> paths;
    ComPtr<IShellItemArray>            items;
    DWORD                              count = 0;
    if (FAILED(dialog.GetResults(&items)) || FAILED(items->GetCount(&count)))
    {
        return paths;
    }
    for (DWORD i = 0; i < count; ++i)
    {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(items->GetItemAt(i, &item)))
        {
            if (auto path = item_path(*item.Get()))
            {
                paths.push_back(std::move(*path));
            }
        }
    }
    return paths;
}

// The common item dialog needs an STA and hangs in the MTA the UI thread is in, so it runs on
// its own thread while this one keeps pumping: an owned window shares the owner's input queue.
template <typename Result, typename Show>
Result show_on_sta_thread(Show&& show)
{
    const HWND  focused = GetFocus();
    Result      result{};
    std::thread worker(
        [&]
        {
            if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
            {
                result = show();
                CoUninitialize();
            }
        });

    HANDLE handle    = worker.native_handle();
    bool   quit      = false;
    WPARAM quit_code = 0;
    while (MsgWaitForMultipleObjectsEx(1, &handle, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE) != WAIT_OBJECT_0)
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                // The dialog is still open, so WM_QUIT goes back to the caller's loop once it has closed.
                quit      = true;
                quit_code = msg.wParam;
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    worker.join();

    if (focused && IsWindow(focused))
    {
        SetFocus(focused);
    }
    if (quit)
    {
        PostQuitMessage(static_cast<int>(quit_code));
    }
    return result;
}

void set_filters(IFileDialog& dialog, std::span<const FileDialogFilter> filters)
{
    if (filters.empty())
    {
        return;
    }
    // The wide strings have to outlive SetFileTypes(), which copies them.
    std::vector<std::pair<std::wstring, std::wstring>> wide;
    wide.reserve(filters.size());
    for (const FileDialogFilter& f : filters)
    {
        wide.emplace_back(widen(f.name), widen(f.pattern));
    }
    std::vector<COMDLG_FILTERSPEC> specs;
    specs.reserve(wide.size());
    for (const auto& [name, pattern] : wide)
    {
        specs.push_back({name.c_str(), pattern.c_str()});
    }
    dialog.SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
}

// ---- storage pickers ----

// PickerCommon::ValidateSingleFileTypeFilterElement.
void validate_extension(const std::string& extension)
{
    if (extension == "*")
    {
        return;
    }
    if (extension.empty() || extension.front() != '.' || extension.find_first_of(".*?", 1) != std::string::npos ||
        extension.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("storage picker: a file type must be \"*\" or \".ext\"");
    }
}

// PickerCommon::ValidateFolderPath: empty, or absolute and syntactically a path.
void validate_folder_path(const std::filesystem::path& path, const char* property)
{
    if (path.empty())
    {
        return;
    }
    if (path.native().find(L'\0') != std::wstring::npos || !path.is_absolute())
    {
        throw std::invalid_argument(property);
    }
    PIDLIST_ABSOLUTE pidl = SHSimpleIDListFromPath(path.c_str());
    if (!pidl)
    {
        throw std::invalid_argument(property);
    }
    CoTaskMemFree(pidl);
}

// PickerParameters::FormatExtensionWithWildcard.
std::string with_wildcard(const std::string& extension)
{
    return !extension.empty() && extension.front() == '*' ? extension : "*" + extension;
}

// GetKnownFolderFromId; Documents when the folder asked for is unavailable.
ComPtr<IShellItem> known_folder(PickerLocationId location)
{
    const KNOWNFOLDERID* id = nullptr;
    switch (location)
    {
        case PickerLocationId::DocumentsLibrary:
            id = &FOLDERID_Documents;
            break;
        case PickerLocationId::ComputerFolder:
            id = &FOLDERID_ComputerFolder;
            break;
        case PickerLocationId::Desktop:
            id = &FOLDERID_Desktop;
            break;
        case PickerLocationId::Downloads:
            id = &FOLDERID_Downloads;
            break;
        case PickerLocationId::MusicLibrary:
            id = &FOLDERID_MusicLibrary;
            break;
        case PickerLocationId::PicturesLibrary:
            id = &FOLDERID_PicturesLibrary;
            break;
        case PickerLocationId::VideosLibrary:
            id = &FOLDERID_VideosLibrary;
            break;
        case PickerLocationId::Objects3D:
            id = &FOLDERID_Objects3D;
            break;
        case PickerLocationId::Unspecified:
            break;
    }
    if (!id)
    {
        return nullptr;
    }
    ComPtr<IShellItem> item;
    if (FAILED(SHGetKnownFolderItem(*id, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&item))))
    {
        SHGetKnownFolderItem(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&item));
    }
    return item;
}

// PickerCommon::TryParseFolderItem: an existing folder, or nothing.
ComPtr<IShellItem> folder_item(const std::filesystem::path& path)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::is_directory(path, error))
    {
        return nullptr;
    }
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))))
    {
        return nullptr;
    }
    return item;
}

// Captured on the calling thread, where validation throws, and read on the dialog's.
struct PickerParameters
{
    std::wstring          commit_button_text;
    std::wstring          title;
    PickerLocationId      suggested_start_location = PickerLocationId::Unspecified;
    PickerFileTypes       file_types;
    std::filesystem::path suggested_folder;
    std::filesystem::path suggested_start_folder;
    bool                  remember_last = true;
};

// PickerParameters::ConfigureDialog.
void configure(IFileDialog& dialog, const PickerParameters& p)
{
    if (!p.commit_button_text.empty())
    {
        dialog.SetOkButtonLabel(p.commit_button_text.c_str());
    }
    if (!p.title.empty())
    {
        dialog.SetTitle(p.title.c_str());
    }
    // The start folder takes precedence over the start location; both are only where the
    // dialog opens when it has no memory of its own.
    ComPtr<IShellItem> start = folder_item(p.suggested_start_folder);
    if (!start)
    {
        start = known_folder(p.suggested_start_location);
    }
    if (start && p.remember_last)
    {
        dialog.SetDefaultFolder(start.Get());
    }
    else if (start)
    {
        dialog.SetFolder(start.Get()); // every time, over the dialog's memory
    }
    // The suggested folder beats both, and the dialog's memory too.
    if (ComPtr<IShellItem> folder = folder_item(p.suggested_folder))
    {
        dialog.SetFolder(folder.Get());
    }
    if (!p.file_types.filters.empty())
    {
        set_filters(dialog, p.file_types.filters);
        if (p.file_types.initial_index >= 0 && p.file_types.initial_index < static_cast<int>(p.file_types.filters.size()))
        {
            dialog.SetFileTypeIndex(static_cast<UINT>(p.file_types.initial_index + 1)); // one-based
        }
    }
}

template <typename Picker>
PickerParameters capture_common(const Picker& picker)
{
    validate_folder_path(picker.suggested_folder, "SuggestedFolder");
    validate_folder_path(picker.suggested_start_folder, "SuggestedStartFolder");
    PickerParameters p;
    p.commit_button_text       = widen(picker.commit_button_text);
    p.title                    = widen(picker.title);
    p.suggested_start_location = picker.suggested_start_location;
    p.suggested_folder         = picker.suggested_folder;
    p.suggested_start_folder   = picker.suggested_start_folder;
    p.remember_last            = picker.remember_last;
    return p;
}

PickerParameters capture(const FileOpenPicker& picker)
{
    PickerParameters p = capture_common(picker);
    p.file_types = picker_file_types(picker.file_type_filter, picker.file_type_choices, picker.initial_file_type_index);
    return p;
}

template <typename Result>
Result open_dialog(NativeHandle owner, const PickerParameters& p, FILEOPENDIALOGOPTIONS extra)
{
    return show_on_sta_thread<Result>(
        [&]() -> Result
        {
            ComPtr<IFileOpenDialog> dialog;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            {
                return Result{};
            }
            configure(*dialog.Get(), p);
            FILEOPENDIALOGOPTIONS options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | extra);
            if (FAILED(dialog->Show(owner)))
            {
                return Result{}; // canceled, or the dialog couldn't be shown
            }
            if constexpr (std::is_same_v<Result, std::vector<std::filesystem::path>>)
            {
                return result_paths(*dialog.Get());
            }
            else
            {
                return result_path(*dialog.Get());
            }
        });
}

} // namespace

std::optional<std::filesystem::path> show_open_file_dialog(HWND                              owner,
                                                           const std::string&                title,
                                                           std::span<const FileDialogFilter> filters)
{
    return show_on_sta_thread<std::optional<std::filesystem::path>>(
        [&]() -> std::optional<std::filesystem::path>
        {
            ComPtr<IFileOpenDialog> dialog;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            {
                return std::nullopt;
            }
            dialog->SetTitle(widen(title).c_str());
            set_filters(*dialog.Get(), filters);
            if (FAILED(dialog->Show(owner)))
            {
                return std::nullopt; // canceled, or the dialog couldn't be shown
            }
            return result_path(*dialog.Get());
        });
}

std::optional<std::filesystem::path> show_save_file_dialog(
    HWND                              owner,
    const std::string&                title,
    std::span<const FileDialogFilter> filters,
    const std::string&                default_file_name)
{
    return show_on_sta_thread<std::optional<std::filesystem::path>>(
        [&]() -> std::optional<std::filesystem::path>
        {
            ComPtr<IFileSaveDialog> dialog;
            if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            {
                return std::nullopt;
            }
            dialog->SetTitle(widen(title).c_str());
            if (!default_file_name.empty())
            {
                dialog->SetFileName(widen(default_file_name).c_str());
            }
            set_filters(*dialog.Get(), filters);
            if (FAILED(dialog->Show(owner)))
            {
                return std::nullopt;
            }
            return result_path(*dialog.Get());
        });
}

std::optional<std::filesystem::path> show_folder_dialog(HWND owner, const std::string& title)
{
    return show_on_sta_thread<std::optional<std::filesystem::path>>(
        [&]() -> std::optional<std::filesystem::path>
        {
            ComPtr<IFileOpenDialog> dialog;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            {
                return std::nullopt;
            }
            DWORD options = 0;
            dialog->GetOptions(&options);
            // FOS_FORCEFILESYSTEM: a library or "This PC" has no path to hand back.
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dialog->SetTitle(widen(title).c_str());
            if (FAILED(dialog->Show(owner)))
            {
                return std::nullopt;
            }
            return result_path(*dialog.Get());
        });
}

std::optional<std::filesystem::path> show_open_file_dialog(const Window&                     owner,
                                                           const std::string&                title,
                                                           std::span<const FileDialogFilter> filters)
{
    return show_open_file_dialog(owner.native_handle(), title, filters);
}

std::optional<std::filesystem::path> show_save_file_dialog(
    const Window&                     owner,
    const std::string&                title,
    std::span<const FileDialogFilter> filters,
    const std::string&                default_file_name)
{
    return show_save_file_dialog(owner.native_handle(), title, filters, default_file_name);
}

std::optional<std::filesystem::path> show_folder_dialog(const Window& owner, const std::string& title)
{
    return show_folder_dialog(owner.native_handle(), title);
}

// ---- storage pickers ----

std::optional<std::filesystem::path> FileOpenPicker::pick_single_file(NativeHandle owner) const
{
    return open_dialog<std::optional<std::filesystem::path>>(owner, capture(*this), 0);
}

std::optional<std::filesystem::path> FileOpenPicker::pick_single_file(const Window& owner) const
{
    return pick_single_file(owner.native_handle());
}

std::vector<std::filesystem::path> FileOpenPicker::pick_multiple_files(NativeHandle owner) const
{
    return open_dialog<std::vector<std::filesystem::path>>(owner, capture(*this), FOS_ALLOWMULTISELECT);
}

std::vector<std::filesystem::path> FileOpenPicker::pick_multiple_files(const Window& owner) const
{
    return pick_multiple_files(owner.native_handle());
}

std::optional<std::filesystem::path> FileSavePicker::pick_save_file(NativeHandle owner) const
{
    // MAX_PATH counts UTF-16 units, as the dialog does.
    const std::wstring file_name = widen(suggested_file_name);
    if (file_name.size() > MAX_PATH || file_name.find(L'\0') != std::wstring::npos)
    {
        throw std::invalid_argument("SuggestedFileName");
    }
    PickerParameters p = capture_common(*this);
    p.file_types       = picker_file_types(file_type_filter, file_type_choices, initial_file_type_index);
    // The dialog wants the extension without its dot, or it shows two.
    const std::wstring extension = widen(!default_file_extension.empty() && default_file_extension.front() == '.'
                                             ? std::string_view{default_file_extension}.substr(1)
                                             : std::string_view{default_file_extension});
    return show_on_sta_thread<std::optional<std::filesystem::path>>(
        [&]() -> std::optional<std::filesystem::path>
        {
            ComPtr<IFileSaveDialog> dialog;
            if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            {
                return std::nullopt;
            }
            configure(*dialog.Get(), p);
            // PickerParameters::ConfigureFileSaveDialog.
            if (!file_name.empty())
            {
                dialog->SetFileName(file_name.c_str());
            }
            FILEOPENDIALOGOPTIONS options = 0;
            dialog->GetOptions(&options);
            if (!show_overwrite_prompt)
            {
                options &= ~static_cast<FILEOPENDIALOGOPTIONS>(FOS_OVERWRITEPROMPT);
            }
            dialog->SetDefaultExtension(extension.c_str());
            dialog->SetOptions(options | FOS_STRICTFILETYPES);
            if (FAILED(dialog->Show(owner)))
            {
                return std::nullopt;
            }
            return result_path(*dialog.Get());
        });
}

std::optional<std::filesystem::path> FileSavePicker::pick_save_file(const Window& owner) const
{
    return pick_save_file(owner.native_handle());
}

std::optional<std::filesystem::path> FolderPicker::pick_single_folder(NativeHandle owner) const
{
    const PickerParameters p = capture_common(*this);
    return show_on_sta_thread<std::optional<std::filesystem::path>>(
        [&]() -> std::optional<std::filesystem::path>
        {
            ComPtr<IFileOpenDialog> dialog;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            {
                return std::nullopt;
            }
            configure(*dialog.Get(), p);
            // A folder picker replaces the options outright here.
            dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            if (FAILED(dialog->Show(owner)))
            {
                return std::nullopt;
            }
            return result_path(*dialog.Get());
        });
}

std::optional<std::filesystem::path> FolderPicker::pick_single_folder(const Window& owner) const
{
    return pick_single_folder(owner.native_handle());
}

std::vector<std::filesystem::path> FolderPicker::pick_multiple_folders(NativeHandle owner) const
{
    return open_dialog<std::vector<std::filesystem::path>>(owner,
                                                           capture_common(*this),
                                                           FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
}

std::vector<std::filesystem::path> FolderPicker::pick_multiple_folders(const Window& owner) const
{
    return pick_multiple_folders(owner.native_handle());
}

} // namespace ui::win32

// ---- the type box (ShellDialogs.hpp)
namespace ui::win32
{

PickerFileTypes picker_file_types(std::span<const std::string> filter,
                                  std::span<const FileType>    choices,
                                  int                          initial_index,
                                  const std::string&           all_files_text)
{
    if (initial_index < -1)
    {
        throw std::invalid_argument("storage picker: InitialFileTypeIndex below -1");
    }
    PickerFileTypes types;
    types.initial_index = initial_index;

    if (!choices.empty())
    {
        // The choices map: insertion order, a repeated name replacing its value in place.
        for (const FileTypeChoice& choice : choices)
        {
            std::string pattern;
            for (const std::string& extension : choice.extensions)
            {
                validate_extension(extension);
                pattern += (pattern.empty() ? "" : ";") + with_wildcard(extension);
            }
            auto existing = std::ranges::find(types.filters, choice.name, &FileDialogFilter::name);
            if (existing != types.filters.end())
            {
                existing->pattern = std::move(pattern);
            }
            else
            {
                types.filters.push_back({choice.name, std::move(pattern)});
            }
        }
    }
    else if (!filter.empty())
    {
        std::string all;
        for (const std::string& extension : filter)
        {
            validate_extension(extension);
            const std::string pattern = with_wildcard(extension);
            types.filters.push_back({pattern, pattern});
            all += (all.empty() ? "" : ";") + pattern;
        }
        if (filter.size() == 1 && all == "*")
        {
            types.filters.front() = {all_files_text, "*"};
        }
        else
        {
            types.filters.push_back({all_files_text, all});
        }
        if (types.initial_index == -1)
        {
            types.initial_index = static_cast<int>(types.filters.size()) - 1; // the union
        }
    }
    else
    {
        types.filters.push_back({all_files_text, "*"});
        if (types.initial_index == -1)
        {
            types.initial_index = 0;
        }
        return types;
    }

    if (types.initial_index > static_cast<int>(types.filters.size()) - 1)
    {
        throw std::invalid_argument("storage picker: InitialFileTypeIndex past the last file type");
    }
    return types;
}

} // namespace ui::win32

// ---- the pickers an app calls (winui/ui/Pickers.hpp)
namespace ui
{

namespace
{

win32::PickerLocationId location_id(PickerLocation location)
{
    switch (location)
    {
        case PickerLocation::Documents:
            return win32::PickerLocationId::DocumentsLibrary;
        case PickerLocation::Computer:
            return win32::PickerLocationId::ComputerFolder;
        case PickerLocation::Desktop:
            return win32::PickerLocationId::Desktop;
        case PickerLocation::Downloads:
            return win32::PickerLocationId::Downloads;
        case PickerLocation::Music:
            return win32::PickerLocationId::MusicLibrary;
        case PickerLocation::Pictures:
            return win32::PickerLocationId::PicturesLibrary;
        case PickerLocation::Videos:
            return win32::PickerLocationId::VideosLibrary;
        case PickerLocation::Objects3D:
            return win32::PickerLocationId::Objects3D;
        case PickerLocation::Unspecified:
            break;
    }
    return win32::PickerLocationId::Unspecified;
}

// ui::detail::NoIndex is the backend's -1, which picks the default entry.
int file_type_index(std::optional<std::size_t> chosen)
{
    if (!chosen)
    {
        return -1;
    }
    const std::size_t index = *chosen;
    if (index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument("storage picker: InitialFileTypeIndex past the last file type");
    }
    return static_cast<int>(index);
}

win32::FileOpenPicker open_picker(const PickFileOptions& options)
{
    win32::FileOpenPicker picker;
    picker.suggested_start_location = location_id(options.start_location);
    picker.commit_button_text       = options.commit_button_text;
    picker.title                    = options.title;
    picker.file_type_choices        = options.file_types;
    picker.file_type_filter         = options.extensions;
    picker.initial_file_type_index  = file_type_index(options.initial_file_type);
    picker.suggested_start_folder   = options.folder;
    picker.remember_last            = options.remember_last;
    return picker;
}

win32::FolderPicker folder_picker(const PickFolderOptions& options)
{
    win32::FolderPicker picker;
    picker.suggested_start_location = location_id(options.start_location);
    picker.commit_button_text       = options.commit_button_text;
    picker.title                    = options.title;
    picker.suggested_start_folder   = options.folder;
    picker.remember_last            = options.remember_last;
    return picker;
}

} // namespace

std::optional<std::filesystem::path> pick_file(const Window& owner, const PickFileOptions& options)
{
    return open_picker(options).pick_single_file(owner);
}

std::vector<std::filesystem::path> pick_files(const Window& owner, const PickFileOptions& options)
{
    return open_picker(options).pick_multiple_files(owner);
}

std::optional<std::filesystem::path> pick_save_file(const Window& owner, const PickSaveFileOptions& options)
{
    win32::FileSavePicker picker;
    picker.suggested_start_location = location_id(options.start_location);
    picker.commit_button_text       = options.commit_button_text;
    picker.title                    = options.title;
    picker.file_type_choices        = options.file_types;
    picker.file_type_filter         = options.extensions;
    picker.initial_file_type_index  = file_type_index(options.initial_file_type);
    picker.default_file_extension   = options.default_extension;
    picker.suggested_file_name      = options.suggested_name;
    picker.suggested_start_folder   = options.folder;
    picker.remember_last            = options.remember_last;
    picker.show_overwrite_prompt    = options.overwrite_prompt;
    return picker.pick_save_file(owner);
}

std::optional<std::filesystem::path> pick_folder(const Window& owner, const PickFolderOptions& options)
{
    return folder_picker(options).pick_single_folder(owner);
}

std::vector<std::filesystem::path> pick_folders(const Window& owner, const PickFolderOptions& options)
{
    return folder_picker(options).pick_multiple_folders(owner);
}

} // namespace ui

#endif // _WIN32
