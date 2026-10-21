// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Win32ThemeSource.hpp"

#include "DarkMode.hpp"
#include "Win32Apartment.hpp"

#include <dwmapi.h>
#include <optional>
#include <windows.h>

#include <cstdint>

// UISettings is the only source of the accent tonal ramp (AccentLight1..3/Dark1..3); the
// registry does not hold it.
#include "winui/core/Color.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <winrt/Windows.System.Power.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace ui::win32
{
namespace
{

// A switch under the Personalize key. A missing key or value (older Windows, a locked-down
// machine) means on, which is Windows' own default for both switches read here.
[[nodiscard]] bool personalize_switch(const wchar_t* name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
    {
        return true;
    }

    DWORD value = 1;
    DWORD size  = sizeof(value);
    DWORD type  = 0;
    const bool ok = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
                    type == REG_DWORD;
    RegCloseKey(key);
    return !ok || value != 0;
}

// Fallback when UISettings is unreachable. Stored 0xAABBGGRR, red and blue swapped
// against Color::from_argb.
[[nodiscard]] std::optional<Color> accent_color_from_registry()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD      value = 0;
    DWORD      size  = sizeof(value);
    DWORD      type  = 0;
    const bool ok    = RegQueryValueExW(key, L"AccentColor", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
                           ERROR_SUCCESS &&
                       type == REG_DWORD;
    RegCloseKey(key);
    if (!ok)
    {
        return std::nullopt;
    }

    const auto b = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    const auto g = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    const auto r = static_cast<std::uint8_t>(value & 0xFF);
    return Color::from_rgba(r, g, b);
}

// Last resort: DwmGetColorizationColor is the auto-computed glass tint, not the chosen accent,
// but a plausible blue when even the registry key is missing.
[[nodiscard]] Color accent_color_fallback()
{
    if (const std::optional<Color> reg = accent_color_from_registry())
    {
        return *reg;
    }

    DWORD color  = 0;
    BOOL  opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)))
    {
        return Color::from_argb(0xFF000000u | (color & 0x00FFFFFFu));
    }
    return Color::from_argb(0xFF0078D4);
}

[[nodiscard]] Color sys_color(int index)
{
    const DWORD c = GetSysColor(index);
    return Color::from_rgba(GetRValue(c), GetGValue(c), GetBValue(c));
}

[[nodiscard]] Color to_color(const winrt::Windows::UI::Color& c)
{
    return Color::from_rgba(c.R, c.G, c.B, c.A);
}

struct WinrtAccent
{
    Color      accent;
    AccentRamp ramp;
};

// UIColorType::Accent is exactly the color Settings shows (Raymond Chen, "The Old New Thing",
// 2017-04-05).
[[nodiscard]] std::optional<WinrtAccent> accent_from_uisettings()
{
    try
    {
        keep_mta_alive();

        using winrt::Windows::UI::ViewManagement::UIColorType;
        using winrt::Windows::UI::ViewManagement::UISettings;

        const UISettings settings;
        WinrtAccent      result;
        result.accent      = to_color(settings.GetColorValue(UIColorType::Accent));
        result.ramp.light1 = to_color(settings.GetColorValue(UIColorType::AccentLight1));
        result.ramp.light2 = to_color(settings.GetColorValue(UIColorType::AccentLight2));
        result.ramp.light3 = to_color(settings.GetColorValue(UIColorType::AccentLight3));
        result.ramp.dark1  = to_color(settings.GetColorValue(UIColorType::AccentDark1));
        result.ramp.dark2  = to_color(settings.GetColorValue(UIColorType::AccentDark2));
        result.ramp.dark3  = to_color(settings.GetColorValue(UIColorType::AccentDark3));
        return result;
    } catch (const winrt::hresult_error&)
    {
        return std::nullopt;
    }
}

// The inputs the platform's own material policy reads: UISettings.AdvancedEffectsEnabled (the
// switch, which the registry value alone gets right only while Energy Saver is off),
// PowerManager.EnergySaverStatus, and a remote session.
[[nodiscard]] bool transparency_enabled()
{
    if (GetSystemMetrics(SM_REMOTESESSION) != 0)
    {
        return false;
    }
    try
    {
        keep_mta_alive();
        using winrt::Windows::System::Power::EnergySaverStatus;
        using winrt::Windows::System::Power::PowerManager;
        using winrt::Windows::UI::ViewManagement::UISettings;
        return UISettings{}.AdvancedEffectsEnabled() && PowerManager::EnergySaverStatus() != EnergySaverStatus::On;
    } catch (const winrt::hresult_error&)
    {
        return personalize_switch(L"EnableTransparency");
    }
}

// Without UISettings, approximate the ramp from the accent rather than repeating it.
[[nodiscard]] AccentRamp accent_ramp_fallback(const Color& accent)
{
    AccentRamp ramp;
    ramp.light1 = lerp(accent, colors::White, 0.15f);
    ramp.light2 = lerp(accent, colors::White, 0.30f);
    ramp.light3 = lerp(accent, colors::White, 0.45f);
    ramp.dark1  = lerp(accent, colors::Black, 0.15f);
    ramp.dark2  = lerp(accent, colors::Black, 0.30f);
    ramp.dark3  = lerp(accent, colors::Black, 0.45f);
    return ramp;
}

[[nodiscard]] HighContrastPalette high_contrast_palette()
{
    HighContrastPalette hc;
    hc.window_background  = sys_color(COLOR_WINDOW);
    hc.window_text        = sys_color(COLOR_WINDOWTEXT);
    hc.control_background = sys_color(COLOR_BTNFACE);
    hc.control_text       = sys_color(COLOR_BTNTEXT);
    hc.control_border     = sys_color(COLOR_WINDOWFRAME);
    hc.accent             = sys_color(COLOR_HIGHLIGHT);
    hc.accent_text        = sys_color(COLOR_HIGHLIGHTTEXT);
    hc.disabled_text      = sys_color(COLOR_GRAYTEXT);
    hc.hyperlink          = sys_color(COLOR_HOTLIGHT);
    return hc;
}

} // namespace

OsThemeSnapshot Win32ThemeSource::snapshot() const
{
    OsThemeSnapshot s;

    HIGHCONTRASTW hc{};
    hc.cbSize        = sizeof(hc);
    const bool hc_ok = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != 0;
    s.high_contrast  = hc_ok && (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    s.hc             = high_contrast_palette();

    // The same value UISettings exposes.
    s.dark = !personalize_switch(L"AppsUseLightTheme");

    // The popup animation switches (Theme::popup_motion), under their master switch.
    auto spi_flag = [](UINT action)
    {
        BOOL on = FALSE;
        return SystemParametersInfoW(action, 0, &on, 0) != 0 && on != FALSE;
    };
    const bool effects    = spi_flag(SPI_GETUIEFFECTS);
    s.menu_animation      = effects && spi_flag(SPI_GETMENUANIMATION);
    s.menu_fade           = spi_flag(SPI_GETMENUFADE);
    s.combo_box_animation = effects && spi_flag(SPI_GETCOMBOBOXANIMATION);
    // "Animation effects" in Settings; a failed query leaves the default, animations on.
    BOOL client_area = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &client_area, 0) != 0)
    {
        s.animations_enabled = client_area != FALSE;
    }

    s.transparency_enabled = transparency_enabled();
    // Windows 11 is build 22000; anything before it draws Windows 10's Fluent.
    s.system_variant = os_build() >= 22000 ? ThemeVariant::Fluent2 : ThemeVariant::Fluent;

    if (const std::optional<WinrtAccent> live = accent_from_uisettings())
    {
        s.accent      = live->accent;
        s.accent_ramp = live->ramp;
    }
    else
    {
        s.accent      = accent_color_fallback();
        s.accent_ramp = accent_ramp_fallback(s.accent);
    }
    return s;
}

} // namespace ui::win32

namespace ui
{

OsThemeSnapshot os_theme()
{
    return win32::Win32ThemeSource{}.snapshot();
}

} // namespace ui

#endif // _WIN32
