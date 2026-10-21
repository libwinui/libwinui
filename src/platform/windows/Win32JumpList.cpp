// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Backend.hpp"
#include "Win32AppIdentity.hpp"
#include "Win32Icon.hpp"
#include "Win32String.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/ui/JumpList.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <objectarray.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
// clang-format on

#include <charconv>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <cstdint>

namespace ui::win32
{

namespace
{

using Microsoft::WRL::ComPtr;

// System.Title, the text a shell link shows in a jump list. Spelled out here rather than taken
// from propkey.h, whose constants need propsys.lib.
constexpr PROPERTYKEY TitleKey{{0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}}, 2};

// Where a task's icon comes from: a file and an index in it, as IShellLink takes it.
struct IconLocation
{
    std::filesystem::path file;
    int                   index = 0;
};

struct PreparedTask
{
    std::wstring title;
    std::wstring arguments;
    std::wstring description;
    IconLocation icon;
};

// `pixels` written to `path` as an icon file holding one PNG image; false when that failed.
bool write_ico(const Bitmap& pixels, const std::filesystem::path& path)
{
    try
    {
        const std::vector<std::uint8_t> png = pixels.encode_png();
        std::vector<std::uint8_t>       ico;
        const auto                      put16 = [&ico](std::uint16_t v)
        {
            ico.push_back(static_cast<std::uint8_t>(v & 0xFF));
            ico.push_back(static_cast<std::uint8_t>(v >> 8));
        };
        const auto put32 = [&put16](std::uint32_t v)
        {
            put16(static_cast<std::uint16_t>(v & 0xFFFF));
            put16(static_cast<std::uint16_t>(v >> 16));
        };
        put16(0); // reserved
        put16(1); // an icon, not a cursor
        put16(1); // one image
        ico.push_back(static_cast<std::uint8_t>(pixels.width() >= 256 ? 0 : pixels.width()));
        ico.push_back(static_cast<std::uint8_t>(pixels.height() >= 256 ? 0 : pixels.height()));
        ico.push_back(0); // no palette
        ico.push_back(0); // reserved
        put16(1);         // planes
        put16(32);        // bits per pixel
        put32(static_cast<std::uint32_t>(png.size()));
        put32(22); // the image follows the 6-byte header and the 16-byte entry
        ico.insert(ico.end(), png.begin(), png.end());

        std::ofstream out{path, std::ios::binary | std::ios::trunc};
        out.write(reinterpret_cast<const char*>(ico.data()), static_cast<std::streamsize>(ico.size()));
        return static_cast<bool>(out);
    } catch (const std::exception&)
    {
        return false;
    }
}

// Where the shell finds `icon`: the executable's own icon for an empty one, a numbered resource
// in place, anything else drawn into an icon file under %LOCALAPPDATA%\libwinui\jumplist.
IconLocation locate_icon(const WindowIcon& icon, const std::filesystem::path& exe, std::size_t index)
{
    if (icon.empty())
    {
        return {exe, 0};
    }
    if (icon.image.empty() && icon.glyph.codepoint == 0 && icon.resource.starts_with('#'))
    {
        int id = 0;
        const auto [end, error] = std::from_chars(icon.resource.data() + 1, icon.resource.data() + icon.resource.size(), id);
        if (error == std::errc{} && end == icon.resource.data() + icon.resource.size())
        {
            return {exe, -id}; // a negative index names a resource ID
        }
    }

    const std::filesystem::path folder = cache_folder(L"jumplist");
    if (folder.empty())
    {
        return {exe, 0};
    }
    ComPtr<ID2D1Factory>   d2d;
    ComPtr<IDWriteFactory> dwrite;
    if (icon.image.empty() && icon.glyph.codepoint != 0)
    {
        (void)D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
        (void)DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                  __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    }
    const HICON handle = create_icon(icon, 32, d2d.Get(), dwrite.Get());
    if (handle == nullptr)
    {
        return {exe, 0};
    }
    const Bitmap pixels = icon_pixels(handle);
    DestroyIcon(handle);
    const std::filesystem::path file = folder / std::format(L"{}-task{}.ico", exe.stem().wstring(), index);
    if (pixels.empty() || !write_ico(pixels, file))
    {
        return {exe, 0};
    }
    return {file, 0};
}

ComPtr<IShellLinkW> make_link(const PreparedTask& task, const std::filesystem::path& exe)
{
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()))))
    {
        return nullptr;
    }
    link->SetPath(exe.c_str());
    link->SetArguments(task.arguments.c_str());
    if (!task.description.empty())
    {
        link->SetDescription(task.description.c_str());
    }
    link->SetIconLocation(task.icon.file.c_str(), task.icon.index);

    ComPtr<IPropertyStore> properties;
    if (FAILED(link.As(&properties)))
    {
        return nullptr;
    }
    PROPVARIANT title{};
    if (FAILED(InitPropVariantFromString(task.title.c_str(), &title)))
    {
        return nullptr;
    }
    const bool stored = SUCCEEDED(properties->SetValue(TitleKey, title)) && SUCCEEDED(properties->Commit());
    PropVariantClear(&title);
    return stored ? link : nullptr;
}

// Writes the jump list. Runs on a thread of its own in a single-threaded apartment, which the
// shell's list objects are made for, whatever apartment the UI thread is in.
void commit(const std::vector<PreparedTask>& tasks, const std::filesystem::path& exe, const std::wstring& app_id)
{
    ComPtr<ICustomDestinationList> list;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(list.GetAddressOf()))))
    {
        return;
    }
    if (!app_id.empty())
    {
        list->SetAppID(app_id.c_str());
    }
    if (tasks.empty())
    {
        list->DeleteList(app_id.empty() ? nullptr : app_id.c_str());
        return;
    }

    UINT                 min_slots = 0;
    ComPtr<IObjectArray> removed;
    if (FAILED(list->BeginList(&min_slots, IID_PPV_ARGS(removed.GetAddressOf()))))
    {
        return;
    }
    // Keeps the Recent section a custom list would otherwise replace; it only shows files of a
    // type the app is registered to open, so this fails harmlessly for the others.
    (void)list->AppendKnownCategory(KDC_RECENT);

    ComPtr<IObjectCollection> collection;
    if (FAILED(CoCreateInstance(CLSID_EnumerableObjectCollection,
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(collection.GetAddressOf()))))
    {
        list->AbortList();
        return;
    }
    for (const PreparedTask& task : tasks)
    {
        if (const ComPtr<IShellLinkW> link = make_link(task, exe))
        {
            collection->AddObject(link.Get());
        }
    }
    ComPtr<IObjectArray> array;
    if (FAILED(collection.As(&array)) || FAILED(list->AddUserTasks(array.Get())))
    {
        list->AbortList();
        return;
    }
    list->CommitList();
}

// Runs `work` on a new thread in a single-threaded apartment and waits for it.
template <typename F>
void on_sta_thread(F&& work)
{
    std::thread thread{[&work]
                       {
                           const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                           work();
                           if (SUCCEEDED(init))
                           {
                               CoUninitialize();
                           }
                       }};
    thread.join();
}

class Win32JumpListHost final : public JumpListHost
{
public:
    void set_tasks(std::vector<JumpTask> tasks) override
    {
        // The icons are drawn here, with the caller's factories; the shell objects on their own
        // thread.
        const std::filesystem::path exe = module_path();
        std::vector<PreparedTask>   prepared;
        prepared.reserve(tasks.size());
        for (std::size_t i = 0; i < tasks.size(); ++i)
        {
            prepared.push_back({widen(tasks[i].title),
                                widen(tasks[i].arguments),
                                widen(tasks[i].description),
                                locate_icon(tasks[i].icon, exe, i)});
        }
        const std::wstring app_id = explicit_app_user_model_id();
        on_sta_thread([&] { commit(prepared, exe, app_id); });
    }

    void add_recent_file(const std::filesystem::path& path) override
    {
        std::error_code             error;
        const std::filesystem::path absolute = std::filesystem::absolute(path, error);
        if (error)
        {
            return;
        }
        const std::wstring app_id = explicit_app_user_model_id();
        on_sta_thread(
            [&]
            {
                if (app_id.empty())
                {
                    SHAddToRecentDocs(SHARD_PATHW, absolute.c_str());
                    return;
                }
                // Recorded under the app's own ID, so it lands in this app's Recent section.
                ComPtr<IShellItem> item;
                if (SUCCEEDED(SHCreateItemFromParsingName(absolute.c_str(), nullptr, IID_PPV_ARGS(item.GetAddressOf()))))
                {
                    SHARDAPPIDINFO info{item.Get(), app_id.c_str()};
                    SHAddToRecentDocs(SHARD_APPIDINFO, &info);
                }
            });
    }
};

} // namespace

} // namespace ui::win32

namespace ui::detail
{

JumpListHost& platform_jump_list_host()
{
    static win32::Win32JumpListHost host;
    return host;
}

} // namespace ui::detail

#endif // _WIN32
