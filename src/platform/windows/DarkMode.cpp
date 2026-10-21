// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "DarkMode.hpp"

#include "ProcAddress.hpp"

#include <windows.h>

namespace ui::win32
{
namespace
{

// uxtheme ordinal 135 takes this enum from 1903 on (1809's takes a bool). ForceDark/ForceLight
// so menus follow the app's Theme, which the user can flip apart from the OS setting.
enum class PreferredAppMode
{
    Default    = 0,
    AllowDark  = 1,
    ForceDark  = 2,
    ForceLight = 3,
};

using SetPreferredAppModeFn         = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForAppFn         = bool(WINAPI*)(bool);
using RefreshImmersiveColorPolicyFn = void(WINAPI*)();
using FlushMenuThemesFn             = void(WINAPI*)();
using RtlGetNtVersionNumbersFn      = void(WINAPI*)(DWORD*, DWORD*, DWORD*);


struct DarkModeApi
{
    SetPreferredAppModeFn         set_preferred_app_mode  = nullptr; // 1903+ (build 18362)
    AllowDarkModeForAppFn         allow_dark_mode_for_app = nullptr; // 1809 only (build 17763)
    RefreshImmersiveColorPolicyFn refresh_color_policy    = nullptr;
    FlushMenuThemesFn             flush_menu_themes       = nullptr;

    [[nodiscard]] bool usable() const
    {
        return (set_preferred_app_mode || allow_dark_mode_for_app) && flush_menu_themes;
    }
};

[[nodiscard]] const DarkModeApi& api()
{
    static const DarkModeApi resolved = []
    {
        DarkModeApi a{};
        const DWORD build = os_build();
        if (build < 17763) // pre-1809: no immersive dark mode at all
        {
            return a;
        }
        // Never freed: uxtheme stays loaded for the process anyway, and the pointers outlive this scope.
        const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!uxtheme)
        {
            return a;
        }
        if (build >= 18362)
        {
            a.set_preferred_app_mode = proc_address<SetPreferredAppModeFn>(uxtheme, MAKEINTRESOURCEA(135));
        }
        else
        {
            a.allow_dark_mode_for_app = proc_address<AllowDarkModeForAppFn>(uxtheme, MAKEINTRESOURCEA(135));
        }
        a.refresh_color_policy = proc_address<RefreshImmersiveColorPolicyFn>(uxtheme, MAKEINTRESOURCEA(104));
        a.flush_menu_themes    = proc_address<FlushMenuThemesFn>(uxtheme, MAKEINTRESOURCEA(136));
        return a;
    }();
    return resolved;
}

} // namespace

bool set_app_dark_mode(bool dark)
{
    const DarkModeApi& a = api();
    if (!a.usable())
    {
        return false;
    }

    // -1 so the first call always applies: the process starts in PreferredAppMode::Default.
    static int applied = -1;
    if (applied == static_cast<int>(dark))
    {
        return true;
    }
    applied = static_cast<int>(dark);

    if (a.set_preferred_app_mode)
    {
        a.set_preferred_app_mode(dark ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
    }
    else
    {
        // 1809's AllowDarkModeForApp only permits dark, so menus stay light unless the OS is dark too.
        a.allow_dark_mode_for_app(dark);
    }
    if (a.refresh_color_policy)
    {
        a.refresh_color_policy();
    }
    a.flush_menu_themes(); // drops uxtheme's cached menu theme so the next menu re-opens it
    return true;
}

// GetVersionExW lies without a compatibility manifest; ntdll's RtlGetNtVersionNumbers does not.
// Undocumented but exported since XP; a wrong answer costs a light menu and the older Fluent.
unsigned long os_build()
{
    static const DWORD build = []
    {
        DWORD         major = 0, minor = 0, raw = 0;
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll)
        {
            return DWORD{0};
        }
        const auto fn = proc_address<RtlGetNtVersionNumbersFn>(ntdll, "RtlGetNtVersionNumbers");
        if (!fn)
        {
            return DWORD{0};
        }
        fn(&major, &minor, &raw);
        return major == 10 ? (raw & 0x0FFFFFFFu) : DWORD{0}; // the high nibble is a checked-build flag
    }();
    return build;
}

} // namespace ui::win32

#endif // _WIN32
