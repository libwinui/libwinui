# A History of Windows UI Frameworks

How Windows UI got from a window procedure in 1985 to WinUI 3, told by lineage, and why a C++ library still has a place in it.

Windows has never had one UI framework. What it has is a stack of them, each built on top of or
next to the previous one, and almost none of them ever went away. A Windows 11 machine still runs
programs written against the 1985 window-procedure model, right next to WinForms, WPF, UWP and
WinUI 3 apps and a dozen browser engines. The stack makes a lot more sense once you walk it from
the bottom up, because each layer was an answer to something the layer below couldn't do. The
design eras, documented in detail under [Theme Eras](themes/index.md), give the timeline.

## USER, GDI and the Common Controls

Everything starts with Windows 1.01, which shipped in November 1985. Its UI model is still the
one every later Windows framework sits on, so it pays to get it straight first. A program
registers a *window class* together with a *window procedure*, creates windows from that class,
and runs a message loop that pulls input from a queue and dispatches it to the procedure.[^petzold]
The work behind that model was split three ways:

- `USER` owned windows, menus, dialogs and input.
- `GDI` owned drawing.
- The display driver owned the pixels of the chrome itself.

Dialogs were *resources*, templates compiled into the executable and laid out in *dialog units*,
a quarter of the average character width by an eighth of its height. As such the same dialog
stayed usable at any font size. A surprising amount of what people credit to Windows 3.0 dates
back to this first release:[^styleguide]

- the dialog unit
- `MessageBox`
- the `IDOK`/`IDCANCEL` identifiers
- the `COLOR_*` system color slots

Windows 1.x tiled its windows, and overlapping ones only arrived with Windows 2.03 in December
1987. Apart from that, nothing visual changed across the 1.x and 2.x releases. There was no bevel,
no gray face and no API that could have drawn one. Every piece of system chrome was a 1-bit
bitmap, and the only state change a surface ever showed was inversion. The
[early Windows](themes/winold.md) page has the measured details.

That look held until Windows 3.0 (May 1990) repainted the interface in the gray, beveled style
most people still picture when they think of "old" Windows. The bevel itself still lived in bitmap
artwork inside the display driver though, so an application had no way to ask the system for
one. Windows 3.1 (April 1992) left the look alone and added infrastructure: the common dialogs
(`COMMDLG`), TrueType and `SystemParametersInfo`. Applications that wanted three-dimensional
dialog controls had to reach for `CTL3D.DLL`, which subclassed the stock controls and painted a
bevel on them.[^win31] See [Windows 3.x](themes/win3x.md).

The bevel finally moved into the system with Win32. Windows NT 3.1 (1993) introduced the 32-bit
API, and Windows 95 (August 1995) made it the mainstream. With `DrawEdge`, `DrawFrameControl` and
a five-slot system color ramp from `COLOR_3DHILIGHT` down to `COLOR_3DDKSHADOW`, any control could
draw raised and sunken edges on its own. At the same time `comctl32.dll` added the *common
controls*: list view, tree view, toolbar, status bar, progress bar, trackbar, tab control, up-down
and more.[^comctl] Since `comctl32` was versioned separately from the OS and got updated together
with Internet Explorer, new controls could show up between Windows releases, which is how the date
picker and month calendar (version 4.70) and the rebar arrived. The [Windows 9x](themes/win9x.md)
page covers this look, which Windows XP later named "Windows Classic".

That layer is still there, under everything else on this page. Every Win32 control is a child
`HWND` with its own window procedure, talking to its parent through `WM_COMMAND` and `WM_NOTIFY`.
It's fast, small and accessible out of the box. It's also the reason a Win32 dialog looks the way
it does, because the controls draw themselves, and an application that wants a different look
has to subclass, owner-draw or custom-draw each one of them. Keep that in mind, as it's the limit
most of the frameworks that follow keep running into.

## Class Libraries Over Win32

Before anyone tried to change the look, developers wanted to get rid of the boilerplate. Writing a
window procedure by hand is verbose, so C++ compiler vendors shipped class libraries that wrapped
it. Borland's ObjectWindows Library (OWL) came first, in the early 1990s. Microsoft's Foundation
Classes (MFC) shipped with Microsoft C/C++ 7.0 in 1992 and became the standard way to write a
Windows program in C++ for the rest of the decade.[^mfc] MFC maps messages to member functions
through message maps, wraps each control in a class (`CButton`, `CListCtrl`), and adds the
document/view architecture, serialization and, later, docking panes and the Office-style ribbon.
Under all of that it stayed a thin layer over `HWND`s, so an MFC button is still a `BUTTON`-class
window.

The Active Template Library (ATL) took a different route. It was built for small COM components
and used templates and the curiously recurring template pattern in place of virtual functions,
which produced binaries with no runtime DLL. The Windows Template Library (WTL) applied the same
idea to application UI: MFC-like wrappers for windows, dialogs and common controls, header only,
on top of ATL. Microsoft released it as an unsupported download and open-sourced it in 2004, and
it's still maintained today.[^wtl]

Borland pushed further. Delphi (1995) and C++Builder (1997) came with the Visual Component Library
(VCL), a component model with properties, events and a form designer that wrote the UI into a
resource, which the runtime streamed back in. A lot of line-of-business software in the late
1990s was built with VCL, and it showed quite clearly, that a visual designer plus a component
model is what most application developers actually want from a framework.

For all their differences, these libraries share one limit. They wrap the controls Windows ships
and as such inherit their look. When Windows changed its appearance, an MFC or WTL program changed
with it, once it opted in, and when a design called for something the stock controls couldn't
draw, the library offered owner-draw and nothing else. If the look was going to change, the change
had to come from Windows itself.

## The Managed Turn

Windows XP (October 2001) kept every control class as it was and changed who paints them. The
theme engine in `uxtheme.dll` draws each control part from pre-authored bitmaps and metrics in a
`.msstyles` file, and version 6 of `comctl32` asks that engine to paint, where earlier versions
drew their own bevels.[^visualstyles] An application opted in with a manifest dependency on
`comctl32` v6. An earlier plan with new themed control classes would have broken every program
that located controls by class name, so the opt-in was the price of compatibility. The engine
also exposes the theme to custom controls through `OpenThemeData` and `DrawThemeBackground`, which
lets an owner-drawn control paint the same parts. Note that the shipping style's user-facing name
was "Windows XP style", "Luna" was only its codename. See [Luna](themes/luna.md).

Only a few months later, in February 2002, the .NET Framework 1.0 shipped with Windows
Forms.[^winforms] WinForms is a managed wrapper over Win32 controls in the tradition of VCL, with a
`Form` designer, properties and events, and every `Control` still backed by an `HWND`. Custom
painting goes through GDI+, a separate 2D API with antialiasing, gradients and alpha blending,
which the stock controls never used. `Application.EnableVisualStyles()` (.NET 1.1) switches on the
XP theme engine for the wrapped controls. WinForms was quick to learn and quick to build with, and
it's still shipped and maintained as part of .NET. Then again, it inherited every limit of the
controls underneath:

- fixed pixel layout
- an `HWND` per control
- a look defined by whatever Windows the program happened to run on

If you want to know exactly what each of these stacks (GDI, uxtheme, GDI+ and WinForms) can and
can't express, the [Win32 Theme Stacks](studies/win32-theme-stacks.md) study measures it.

## WPF and Retained Composition

Two decades in, every framework on this page still had the same core, a window behind every
control and each of those windows drawing itself. The Windows Presentation Foundation (codename
Avalon) was the first to throw that out. It shipped with .NET Framework 3.0 in November 2006,
alongside Windows Vista, and broke with everything before it on three counts:[^wpf]

- A window is one `HWND`. Controls have no window of their own. They are elements in a
  retained visual tree, drawn by WPF itself through Direct3D.
- Layout is a two-pass measure and arrange over panels (`StackPanel`, `Grid`, `DockPanel`) in
  device-independent units, where the earlier frameworks used absolute pixel positions.
- A control's look is data. Each control is *lookless*, and its template of primitive elements
  comes from a style, so a theme or an application can replace the whole visual without
  subclassing.

WPF also brought XAML, an XML language for describing object trees, plus data binding through
dependency properties and storyboard animation. XAML made the UI declarative and toolable, to the
point that Expression Blend was a whole designer tool built on it, and data binding made the
model-view-view model pattern the default architecture for .NET desktop apps.

The other half of the shift happened in the OS. Vista introduced the Desktop Window Manager
(DWM), which composes every top-level window off-screen and draws the result with the GPU.[^dwm]
That's what made the Aero glass frame, live thumbnails and Flip 3D possible, and from Windows 8
onward composition is always on. Aero's controls came twice, once as `aero.msstyles` for Win32 and
once as WPF's own vector re-creation in `PresentationFramework.Aero`. See [Aero](themes/aero.md).

Still, WPF never replaced Win32 inside Windows itself. Microsoft's own shell and most of its
desktop applications stayed native, WPF's startup cost and memory footprint were an issue on the
hardware of the time, and it was .NET only. What it did leave behind was an idea: a retained tree,
measure and arrange, lookless controls with templated visuals, and a composition engine
underneath. Every Microsoft UI framework since has been built on that idea, starting with a
trimmed-down WPF that went looking for a new home.

## Silverlight, Metro, WinRT XAML and UWP

That trimmed-down WPF was Silverlight (2007). It ran as a browser plug-in and later became the app
platform of Windows Phone 7 (2010). Windows Phone 7 also introduced the design language that came
to be called Metro: flat surfaces, no chrome, large type, a strict grid and content-first layouts.
When Windows 8 (October 2012) brought Metro to the desktop, it came together with a new app model,
the Windows Runtime (WinRT).[^winrt]

WinRT's UI framework, `Windows.UI.Xaml`, was XAML reimplemented in native C++ as part of the OS.
It kept WPF's model, i.e. the retained tree, measure/arrange, templates, dependency properties and
bindings, and dropped a good part of its surface. It was callable from C++ (first through the
C++/CX language extensions, later through standard C++ with C++/WinRT), C# and JavaScript. The
catch was, that it only ran inside sandboxed, full-screen Store apps with their own lifecycle.
Metro's grid, its type ramp and its dark and light themes, which are two separate constructions
and a lot more than one palette with the colors swapped, are covered in the
[Metro study](studies/metro.md) and on the [MDL](themes/mdl.md) page.

Windows 10 (July 2015) folded those apps back into ordinary windows and renamed the model the
Universal Windows Platform (UWP), one app package for desktop, phone, Xbox and HoloLens. Its look,
the flat and square design often called MDL2, is on the [MDL2](themes/mdl2.md) page. The Fluent
Design System then added materials (Acrylic), light effects (Reveal), depth and motion on top of
the same UWP XAML. It was announced at Build in May 2017 and shipped in the 1709 update that
October.[^fluent] See [Fluent](themes/fluent.md).

Where UWP struggled was the app model around the UI framework:

- Store distribution
- the sandbox
- a lifecycle that could suspend the app
- no access to much of Win32
- a framework that only updated with Windows itself

Developers with existing Win32 programs couldn't move them without a rewrite, and most of them
didn't. If the developers weren't coming to the framework, the framework had to come to them.

## WinUI 2, WinUI 3 and the Windows App SDK

Microsoft did that in two steps. WinUI 2 (2018) moved the newer controls and the latest styles out
of the OS and into a NuGet package for UWP apps, so an app no longer had to wait for a Windows
release to get a new control.[^winui] Its source is public in the `microsoft/microsoft-ui-xaml`
repository, and WinUI 2.6 (June 2021) is where the Windows 11 look first shipped. The Windows 10
styles stayed in the package, renamed with a `_v1` suffix.

WinUI 3 went all the way and took the whole XAML framework out of Windows. `Microsoft.UI.Xaml` is
the same framework, lifted from the OS into the Windows App SDK (announced as Project Reunion, 1.0
in November 2021), and it runs in ordinary Win32 desktop processes, with no sandbox and no Store
requirement.[^wasdk] Rendering goes through the Windows composition layer, text through
DirectWrite, and a desktop `HWND` hosts the XAML content. The design language that shipped with it
is the one Microsoft renamed Fluent 2 in 2023, when the same tokens were unified across web, iOS
and Android. See [Fluent 2](themes/fluent2.md). How WinUI 3 is put together on the inside is the
subject of the [WinUI Internals](studies/winui-internals.md) study.

The decoupling solved the release problem, but it moved a cost onto every app. A WinUI 3 program
depends on the Windows App SDK runtime, which is either installed framework-dependent or bundled
self-contained, and it brings the WinRT activation model, the XAML compiler and C++/WinRT's
projection along. For a C++ developer the build is a Visual Studio project with MSBuild and NuGet
integration, which isn't something a CMake project "just" picks up.

## What Left the Platform

While Microsoft moved from framework to framework, a large part of the Windows desktop stopped
waiting and moved to toolkits that weren't Microsoft's at all.

C++ developers had cross-platform options early on, with Qt (Trolltech, 1995, now The Qt Company)
and wxWidgets (started in 1992 as wxWindows).[^qt][^wx] The two took opposite approaches.
wxWidgets wraps the native controls of each platform, so on Windows it's a Win32 program with
Win32 controls. Qt draws its widgets itself, imitates the platform's look through styles, and
later added Qt Quick, a declarative, GPU-rendered scene graph with its own language (QML). Both are
large, both come with a build and deployment story of their own, and both are the reason many C++
desktop applications never needed a Microsoft framework in the first place.

Electron (first released as Atom Shell in 2013) went further still. It put a Chromium browser and
Node.js into a desktop window and turned the web stack into a desktop UI toolkit.[^electron]
Visual Studio Code, Slack, Discord and, for a long time, Microsoft Teams are built on it.
Microsoft's own answer for embedding the web is WebView2 (generally available in 2020), an
Edge-based control a native app can host.[^webview2] Today, many "native" Windows applications are
a thin native shell around a web view.

The managed world kept moving too. WPF and WinForms were open-sourced in 2018 and live on in .NET,
.NET MAUI (2022) replaced Xamarin.Forms as Microsoft's cross-platform option, and third-party
frameworks such as Avalonia took WPF's model to other platforms. None of that helps a C++ program
though, which brings us back to where this page started.

## Where That Leaves a C++ Developer in 2026

A C++ developer who wants a Windows UI that looks current has three first-party choices today, and
each of them comes with a catch:

- Raw Win32, MFC or WTL are small and fast, but they draw the controls Windows shipped in the
  2000s.
- WinUI 3 has the current look and the full control set, but it brings the Windows App SDK
  runtime, the WinRT projection and an MSBuild-centered toolchain.
- Anything else means leaving the platform's look behind, or shipping a browser.

libwinui sits in that gap and borrows something from each lineage:

- From USER and Win32: one real `HWND` per top-level window, the ordinary message loop, and
  the Win32 controls as an escape hatch for when a stock control is what you want
  (`NativeControl` hosts any window class, and the example code's `native_controls::EditBox` and
  `native_controls::RichEditBox` wrap `EDIT` and RichEdit with it).
- From MFC and WTL: a thin C++ layer with no runtime to install, built by CMake and linked
  like any other library.
- From WPF and WinUI: a retained tree of windowless elements, two-pass measure and arrange
  layout, and the Fluent 2 design with its tokens, drawn with Direct2D and DirectWrite
  and animated through DirectComposition. WinUI is the reference design, measured value by value.
- From declarative frameworks: a description layer of plain aggregates (`ui::Button`,
  `ui::VerticalStack`) built once into the retained tree, with `Property<T>` for state. The
  [Declarative UI in C++](studies/declarative-ui-in-cpp.md) study explains why it's shaped like
  that, and why it doesn't diff trees.
- From the design eras: every look on this page, from the 1-bit chrome of Windows 1.x to
  Fluent 2, as a selectable `ThemeVariant`, with its values measured from the era's own SDKs and
  shipped media.

On top of that, it adds something none of them had. Its controls render into a
platform-independent command stream, so a whole UI can be laid out and checked in a unit test
without a window, a GPU or a Windows session.

[^petzold]: Charles Petzold, *Programming Windows*, 5th ed., Microsoft Press, 1998. Chapters 1 to 3
    cover window classes, the window procedure and the message loop.
[^styleguide]: Microsoft, *Windows Application Style Guide*, Version 1.03, October 1986; the
    Windows 1.03 SDK header `WINDOWS.H`.
[^win31]: Microsoft, *The Windows Interface: An Application Design Guide*, Microsoft Press, 1992;
    Windows 3.1 SDK headers `WINDOWS.H` and `COMMDLG.H`.
[^comctl]: Microsoft Learn, "About Common Controls",
    <https://learn.microsoft.com/windows/win32/controls/common-controls-intro>; "Common Control
    Versions", <https://learn.microsoft.com/windows/win32/controls/common-control-versions>.
[^visualstyles]: Microsoft Learn, "Enabling Visual Styles",
    <https://learn.microsoft.com/windows/win32/controls/cookbook-overview>; "Visual Styles
    Overview", <https://learn.microsoft.com/windows/win32/controls/themes-overview>.
[^mfc]: Microsoft Learn, "MFC Desktop Applications",
    <https://learn.microsoft.com/cpp/mfc/mfc-desktop-applications>.
[^wtl]: Windows Template Library, <https://sourceforge.net/projects/wtl/> and
    <https://github.com/Win32-WTL/WTL>.
[^winforms]: Microsoft Learn, "Desktop Guide (Windows Forms .NET)",
    <https://learn.microsoft.com/dotnet/desktop/winforms/overview/>.
[^wpf]: Microsoft Learn, "What is Windows Presentation Foundation",
    <https://learn.microsoft.com/dotnet/desktop/wpf/overview/>; Adam Nathan, *WPF 4 Unleashed*,
    Sams, 2010.
[^dwm]: Microsoft Learn, "Desktop Window Manager",
    <https://learn.microsoft.com/windows/win32/dwm/dwm-overview>.
[^winrt]: Microsoft Learn, "Windows Runtime (WinRT) APIs",
    <https://learn.microsoft.com/uwp/api/>; "C++/WinRT",
    <https://learn.microsoft.com/windows/uwp/cpp-and-winrt-apis/>.
[^fluent]: Microsoft Learn, "Design and code Windows apps",
    <https://learn.microsoft.com/windows/apps/design/>.
[^winui]: Microsoft Learn, "WinUI 2",
    <https://learn.microsoft.com/windows/apps/winui/winui2/>; source at
    <https://github.com/microsoft/microsoft-ui-xaml>.
[^wasdk]: Microsoft Learn, "Windows App SDK",
    <https://learn.microsoft.com/windows/apps/windows-app-sdk/>; "WinUI 3",
    <https://learn.microsoft.com/windows/apps/winui/winui3/>.
[^qt]: The Qt Company, "Qt history", <https://wiki.qt.io/Qt_History>.
[^wx]: wxWidgets, "About", <https://www.wxwidgets.org/about/>.
[^electron]: Electron, <https://www.electronjs.org/>.
[^webview2]: Microsoft Learn, "Introduction to Microsoft Edge WebView2",
    <https://learn.microsoft.com/microsoft-edge/webview2/>.
