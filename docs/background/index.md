# Background

The history and research libwinui is built on, published so others can build on it too.

## What This Section Is

libwinui reproduces the look and behavior of Windows UI, from the 1-bit chrome of Windows 1.01
to WinUI 3's Fluent 2, in plain C++. Getting there took a lot of reading: SDK headers, shipped
XAML dictionaries, era design guides, the WinUI source tree and the theme files on original
installation media. This section is that reading, written up so you do not have to repeat it.

It has four parts:

- [A History of Windows UI Frameworks](history.md) is one essay on how Windows UI got from `USER`
  and `GDI` to WinUI 3, and what a C++ developer is left with today.
- [Performance](performance.md) measures what a libwinui app costs to start, to keep in memory
  and to draw, and shows how to measure it yourself.
- [Theme Eras](themes/index.md) documents nine design languages as measured data: colors,
  metrics, type ramps, bevels, corner radii and state tables, era by era.
- [Studies](studies/index.md) are the research behind specific design decisions in the library:
  how WinUI is built, how its motion works, what declarative UI can look like in C++23, what each
  Win32 rendering stack can express, what Metro was mechanically, and how Windows spell checking
  plugs into a text box.

None of it is needed to use the library. The [tutorials](../tutorials/index.md) and the
[reference](../reference/index.md) cover that. Read this section when you want to know why a
control looks the way it does, or when you are building something similar yourself.

## How to Cite It

Pages are stable, and their headings are stable anchors, so you can link to a section directly.
Every value on a page comes with its source: a Microsoft Learn article, a WinUI source file, a
header or XAML file from a named Windows SDK, a page of a first-party design guide, or a
measurement on a stated Windows build. The sources are listed as footnotes and in a Sources
section at the end of each page. When you use a value, cite the primary source it came from,
not just this site. That way the next person can check it.

Where a source states nothing, a page says so instead of borrowing a value from a later era.

## What Is Not Here

No screenshots or bitmaps from Microsoft media, and no extracted theme artwork. The pages
describe what the artwork looks like and give its measured values (colors, pixel geometry,
state counts), but the files themselves stay Microsoft's. Text, tables and measured values only.

There are also no API docs here. Anything about how to call libwinui lives in the
[reference](../reference/index.md), and how the library works inside lives under
[Contributing](../contributing/index.md).
