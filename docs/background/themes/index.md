# Theme Eras

Nine design languages, from Windows 1.x to Windows 11, written down as measured data rather than as impressions.

Every era here was researched from its own first-party material: shipped installation media, the SDK headers and theme dictionaries of the time, the design guidelines Microsoft published, and the framework sources that still ship today. Each page condenses that research into one read: what the design language set out to do, the colors, type, metrics and motion it actually used, how its controls were built, and what it takes to reproduce it on a current Windows stack.

## The Era Table

| Era | Windows | Period | Frameworks | Page |
| --- | --- | --- | --- | --- |
| Fluent 2 | Windows 11 | 2021 to present | WinUI 2.6 to 2.8, WinUI 3 | [Fluent 2](fluent2.md) |
| Fluent | Windows 10 | 2017 to 2021 | UWP, WinUI 2 | [Fluent](fluent.md) |
| MDL2 | Windows 10 | 2015 to 2017 | UWP, WinRT XAML | [MDL2](mdl2.md) |
| Metro (MDL) | Windows Phone 7 and 8, Windows 8 and 8.1 | 2010 to 2015 | WinRT XAML | [Metro (MDL)](mdl.md) |
| Aero | Windows Vista, Windows 7 | 2006 to 2012 | WPF, Win32, WinForms | [Aero](aero.md) |
| Luna | Windows XP | 2001 to 2006 | Win32, WinForms | [Luna](luna.md) |
| Windows 9x | Windows 95, 98, Me, NT 4.0, 2000 | 1995 to 2001 | Win32 | [Windows 9x](win9x.md) |
| Windows 3.x | Windows 3.0, 3.1 | 1990 to 1995 | Win16 | [Windows 3.x](win3x.md) |
| Early Windows | Windows 1.x, 2.x | 1985 to 1990 | Win16 | [Early Windows](winold.md) |

Several pages cover more than one release, because the releases share one design language: Aero covers Vista and 7, Metro covers Windows 8 and 8.1 (with Windows Phone as its origin), Windows 9x covers 95 to 2000, and Early Windows covers 1.x and 2.x. Where the releases differ, the page gives both values and says which is which.

## Appearance and Behavior

A design language is two things at once: how controls look, and how they behave, are laid out and are worded. The pages record both, but they keep them apart, because only the first half is a theme. Tiled windows in Windows 1.x, the `Ok` spelling on its buttons, the right-hand button column of a Windows 3.1 dialog or the Windows 8 edge gestures are behavior, layout and content. A re-creation that swaps colors, metrics and control art can reproduce the look of every era here; it cannot reproduce those traits without changing the application itself. Each page's *Reproducing it today* section says which traits fall on which side.

The same split applies in the other direction. Most eras lack controls a modern application expects: there is no toggle switch in Windows 95 and no slider in Windows 2.x. A re-creation that needs one has to build it from the era's own parts, and the pages point out where a control has no era original.

## Light, Dark and High Contrast

Only four of the nine eras have a genuine dark and light pair: Metro, MDL2, Fluent and Fluent 2. The five older eras are light-only, although from Windows 1.x to 9x every system color was user-settable, so a user could dial in a dark scheme by hand.

High contrast exists in every era from Windows 95 on, and each era keeps its own construction under it. Windows 95 to 2000 treat the contrast colors as one more color scheme. Luna and Aero drop to the Classic look while a contrast theme is on, exactly as Windows XP and 7 did. Metro, MDL2 and Fluent keep their shape and swap to their `HighContrast` theme dictionary, whose colors are the user's system colors.

!!! note "`GetSysColor` on Windows 11"
    On a Windows 11 machine in dark mode `GetSysColor` still returns the light values, because dark mode is not part of the classic system color table at all. `COLOR_HIGHLIGHT` is a frozen Windows 10 constant (`#0078D7`) rather than the user's accent (`#0078D4` by default, from `DwmGetColorizationColor` or `UISettings`). A re-creation of the modern eras has to read light or dark and the accent from their own sources. High contrast is the one case where `GetSysColor` is exactly right: the user picked those values on purpose.

## What Each Page Covers

Every era page follows the same outline, so two eras can be compared section by section.

- **Principles.** What the design language set out to do, in its own first-party words where there are any.
- **Foundations.** Color, type, metrics and motion, as tables of measured values. Color includes the system color slots or theme keys of the era, type the UI faces and their sizes in effective pixels, metrics the border, padding and control sizes, and motion the durations (which for the five oldest eras is zero, everywhere).
- **Controls.** Per control: geometry, states and the tokens each state reads. Controls the era did not have are listed as such.
- **Reproducing it today.** What a re-creation on a current Windows stack can reproduce exactly, what it has to approximate (fonts that no longer ship, artwork that must not be redistributed, OS features that were removed, such as glass), and what is behavior rather than appearance.
- **Sources.** The primary sources behind the values, as footnotes.

## How to Read a Page

Values on these pages come from sources of different strength, and the pages say which tier a value comes from wherever that changes how far you should trust it.

| Tier | What it is | Examples |
| --- | --- | --- |
| Tier 1 | First-party, machine-readable artifacts of the era | Shipped installation media and drivers, SDK headers (`WINDOWS.H`, `vsstyle.h`), SDK theme XAML (`generic.xaml`, `themeresources.xaml`), `AeroStyle.xml`, the INIs inside `luna.msstyles`, WPF's and WinUI's theme sources |
| Tier 2 | First-party written specification | *The Windows Interface Guidelines*, the Windows XP and Vista *User Experience Guidelines*, the Windows 8 UX guidelines, Microsoft Learn design pages |
| Tier 3 | Samples and reimplementations | SDK samples, WinUI Gallery, ReactOS and Wine's restatements of `comctl32` and `uxtheme` |
| Tier 4 | Secondary literature and community material | Contemporary books, web design kits, community recreations; used for context, never as the only source for a value |

Tier 1 wins over Tier 2 when they disagree, because the shipped artifact is what users saw. A handful of values have no Tier 1 or Tier 2 source at all (the Windows 95 trackbar's pixel geometry is one); the page flags them. "Measured" means read from a file or counted in a bitmap. Where a value is chosen rather than found, the page says that too.

None of the pages carry screenshots or bitmaps from Microsoft media. Everything here is text, tables and numbers.
