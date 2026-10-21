# Themes

The types a window's theme is made of. [Themes](../../tutorials/guides/themes.md) shows how to pick a theme, light or dark mode, an accent color and a backdrop.

- [`ThemeRequest`](theme-request.md): what an app asks for, passed as `WindowOptions::theme`.
- [`Theme`](theme.md): the resolved colors, metrics and fonts every control draws with.
- [`OsThemeSnapshot`](os-theme-snapshot.md): the system's light or dark mode, accent and contrast state.
- [`HighContrastPalette`](high-contrast-palette.md): the colors of a contrast theme.
- [`ClassicPalette`](classic-palette.md) and [`EarlyPalette`](early-palette.md): the color schemes of the Windows 9x and 3.x eras, and of Windows 1.x and 2.x.
- [`ChromeOverrides`](chrome-overrides.md): a window's own frame colors and corners.

[Theme Eras](../../background/themes/index.md) describes each era the library can draw.
