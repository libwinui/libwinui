# Performance

How long a libwinui app takes to start, how much memory it uses, and what a frame costs.

Every number on this page was measured on one machine, and the section at the end shows how to
measure your own. The numbers are updated when they move by more than 10%.

## The Machine

| | |
|---|---|
| CPU | AMD Ryzen 5 9600X, 6 cores |
| GPU | NVIDIA GeForce RTX 3060 Ti, driver 32.0.16.1714 |
| Memory | 61.6 GB |
| OS | Windows 11 Pro, build 26300 |
| Compiler | MSVC 19.51, `RelWithDebInfo` |
| libwinui | `dd3cc9c6`, 2026-10-21 |

## Startup

`winui_startup` opens a window, waits until its message loop has nothing left to do, then prints
how long each step took, counted from the moment Windows created the process, and closes again.
It has two scenarios:

- **minimal** is the window from [Your First Window](../tutorials/basics/01-first-window.md): a
  label and two buttons in a stack.
- **gallery** is the gallery's start page in a 1180 × 820 window with Mica and the themed frame.

Median of 10 runs, with the binaries and system DLLs already in the file cache:

| Scenario | First window | First frame | First idle | Private memory |
|---|---|---|---|---|
| minimal | 27 ms | 174 ms | 182 ms | 69.0 MB |
| gallery | 28 ms | 177 ms | 186 ms | 72.4 MB |

The first frame landed between 167 and 197 ms across the runs. The first run after a rebuild, with
the executable not yet cached, reached its first frame after 207 to 235 ms. Memory is private bytes
(memory committed for the process alone) at the first idle, in MB of 1024 × 1024 bytes as Task
Manager counts them.

The gallery page costs about the same as the minimal window. Building and laying out the page's
controls is a few milliseconds of the 177.

### Where the Time Goes

To see how much of this is libwinui, the same measurement was taken for three windows written
directly against Win32 (median of 5 runs):

| Window | First window | First frame | Private memory |
|---|---|---|---|
| Win32 window, painted with GDI | 13.5 ms | 35 ms | 1.7 MB |
| The same with a Direct2D `ID2D1HwndRenderTarget`, one cleared frame | 12.9 ms | 162 ms | 52.9 MB |
| The same plus one DirectWrite text layout | 13 ms | 158 ms | 54.1 MB |

Most of the startup time is creating the Direct2D render target and presenting its first frame,
about 150 ms on this machine whatever is drawn. Most of the memory is what Direct2D and the GPU
driver commit for that target, about 51 MB. Both depend on the GPU and its driver, so expect
different numbers on other machines.

libwinui adds two things on top:

- About 14 ms before the window exists: loading its DLLs, creating the Direct2D and DirectWrite
  factories, reading the system theme (3.7 ms) and loading the ICU text analyzer (5.7 ms, once per
  process).
- About 16 MB of memory. That includes the DirectComposition device for animations and in-app
  acrylic, which is created after the first frame, once the message loop is idle, so the first
  frame does not wait for it.

The window is shown in the 8 ms between the first frame and the first idle. Startup draws one frame
and lays out once to get there.

## Frames

`winui_bench` times the common kinds of work. Each scenario runs twice: **headless**, with the
deterministic text measurer and a renderer that only records commands, and **d2d**, with the
DirectWrite measurer and the real Direct2D renderer drawing into an 800 × 600 software bitmap,
read back after every frame. The d2d column is the full CPU cost of a frame, without a GPU. Median
of 5:

| Scenario | Headless | d2d |
|---|---|---|
| 1000 buttons: create, lay out and draw | 1.7 ms | 3.1 ms |
| 1000 buttons: 10 frames where nothing changed | 3.0 ms | 24 ms |
| 1000 buttons: hover 100 of them, one frame each | 38 ms | 237 ms |
| 10'000 bound labels: change every value, one frame | 11.1 ms | 11.7 ms |
| A flyout: open and close it 50 times | 32 ms | 201 ms |
| A TextBox holding 10'000 characters: type 100 more | 13 ms | 286 ms |
| A 100'000-item ListView: scroll 100 times, one frame each | 14 ms | 133 ms |
| The gallery's TileView page: resize through 20 widths | 6.5 ms | 178 ms |
| The gallery's TableView page: resize through 20 widths | 5.8 ms | 111 ms |
| The gallery's ListView page: resize through 20 widths | 4.1 ms | 99 ms |
| Every gallery page: build and lay out | 23 ms | |

Per frame, that is about 2.4 ms for a hover in a window of 1000 buttons and 2.4 ms for a frame
where nothing changed, both with the 800 × 600 readback included.

What keeps those costs low:

- A setter marks only its own control dirty. Layout measures and arranges the dirty controls and
  their ancestors, and a hover that changes only a color lays out nothing.
- A frame draws only the area that looks different from the previous frame.
- Text layouts, brushes and geometries are kept from one frame to the next, so a frame where
  nothing changed creates none.
- An idle window does not wake up. Nothing ticks unless an animation or a control asks for it.

## Idle Pages

`winui_idle` shows every gallery page in a 1180 × 820 window with Mica and the themed frame,
leaves it alone for 4 seconds and then measures it for 3. No page draws a frame while it sits
there. Three pages keep something moving: the ProgressBar page's indeterminate
bars, the ProgressRing page's indeterminate rings, and the ring the ToggleSwitch page's switch
turns on. Those run on compositor layers, so the compositor animates them and the window's UI
thread does nothing. Measured for 10 seconds each:

| Page | What animates | Ticks | Frames |
|---|---|---|---|
| ProgressBar | indeterminate bars | 2 | 0 |
| ToggleSwitch | a ProgressRing the switch turns on | 2 | 0 |
| ProgressRing | indeterminate rings | 2 | 0 |

The two ticks run the probe's own posted work. Before the layers, each of these pages ticked
and drew a frame at the display's rate for as long as it was shown.

With the display on, every page, these three included, uses 2 to 4 million CPU cycles a second
over the whole process, about 0.1% of one core. That is the probe's own threads. The compositor
that runs the layer animations is a separate process and is not counted.

What a window costs while it does draw, measured with `winui_timers` for 10 seconds on a 144 Hz
display, in million cycles a second over the whole process:

| Scenario | Frames | Million cycles/s |
|---|---|---|
| A looped AnimatedVisualPlayer on screen | 1440 | 173 |
| A label that changes every 20 ms | 485 | 134 |
| An on-screen GIF, 10 frames of 100 ms | 100 | 32 |
| A focused TextBox | 18 | 8 |

An animation drawn on the UI thread runs at the display's rate, one frame per refresh, and costs
about 1.2 million cycles a frame, a few percent of one core at 144 Hz.

A progress control that cannot use its layers draws its animation itself: under a popup or
dialog, with animations off, or in a window without a compositor. An indeterminate control
scrolled out of view then costs a tick at the display's rate and draws nothing. With its layers
it costs nothing until it is scrolled back. Aero's glossy ProgressBar still draws its glint
itself.

The Image page's animated GIF, 30 frames of 100 ms, sits below the fold. It asks for each of its
frames with a wake-up at that frame's delay instead of keeping the window ticking, and it stands
still while it is off screen. Measured for 3 seconds:

| Image page | Timer wake-ups | Ticks | Frames |
|---|---|---|---|
| Before | 27 | 114 | 0 |
| After | 0 | 2 | 0 |

Before, the GIF kept advancing out of view, so the window ticked every 100 ms and every paint
found nothing to draw. On screen it now wakes the window exactly when its next frame is due, so
a GIF with 30 ms frames plays at its own pace too.

## Waits in the Controls

A control that waits for a known time asks for one wake-up at its end instead of keeping the
window ticking: a caret blink, a tip's delay, a submenu's hover delay, the spell check after
typing, an overlay scroll bar's two seconds before it fades and Carousel's three seconds before its
buttons hide. What a control changes at that moment, such as the caret or a GIF's next frame, is
drawn without starting the three seconds of 100 ms ticks that follow any other change. An image
still decoding costs no wake-ups until the decode is done. `winui_timers idle 10 <tree>` counts what
each of these costs on an otherwise idle window in 10 seconds:

| Scenario | Before: wake-ups | Ticks | Frames | After: wake-ups | Ticks | Frames |
|---|---|---|---|---|---|---|
| A focused TextBox | 91 | 220 | 18 | 21 | 81 | 18 |
| An on-screen GIF, 10 frames of 100 ms | 109 | 421 | 100 | 106 | 414 | 100 |
| A looped AnimatedVisualPlayer out of view | 0 | 78 | 0 | 0 | 2 | 0 |
| A 4 s play of an AnimatedVisualPlayer out of view | 28 | 60 | 1 | 28 | 60 | 1 |
| The pointer moved onto a text with a tool tip | 28 | 60 | 1 | 28 | 60 | 1 |
| The pointer moved onto a Carousel | 56 | 118 | 2 | 56 | 118 | 2 |

The caret wakes the window twice per 1.06 s blink cycle, where it kept it ticking every 100 ms
before. The GIF wakes it once per frame; its 100 ms frames match the old tick, so the count barely
moves, but a GIF with 300 ms frames now costs a third as much. The player out of view keeps time
without ticking, and the play that ends there completes on one wake-up. In the last three rows the
wait itself costs one wake-up, and what remains is the three seconds of ticks after the change that
starts it (the hover, the `play()`) and after the one that ends it (the tip opening, the buttons
hiding).

## Timers

`window.after()` and `window.every()` wake the process only when a timer is due, so a pending
`after(1h)` costs nothing while it waits. `winui_timers` runs each scenario for 10 seconds on an
otherwise idle window:

| Scenario | Wake-ups | Callbacks | Interval or lateness | CPU |
|---|---|---|---|---|
| No timer | 0 | 0 | | 0 ms |
| `after(1h)` pending | 0 | 0 | | 0 ms |
| `every(1s)` | 11 | 9 | 999 ms (985 to 1010) | 0 ms |
| `every(100ms)` | 100 | 99 | 100 ms (86 to 117) | 16 ms |
| `every(20ms)` | 506 | 495 | 20 ms (13 to 46) | 16 ms |
| `every(20ms)`, 1000 buttons in the window | 506 | 487 | 20 ms (8 to 47) | 47 ms |
| `after(250ms)`, re-armed each time | 63 | 38 | 11 ms late (at most 30) | 0 ms |

Windows rounds a timer to its own resolution of about 15.6 ms, so a short interval keeps its
average but each callback can be that much early or late. Nothing runs more often than every
10 ms. A wake-up walks the window's tree once, which for 1000 buttons is still under 0.1 ms. A
callback that changes what the window shows costs a frame, and frames are paced by the display,
so `every(20ms)` writing a label draws about 28 frames a second.

## Lists

Lists only realize the rows on screen, so the item count barely matters. `winui_bench --filter lists/`
lays out each list and scrolls it 400 times, headless:

| List | 100 items | 100'000 items | 1'000'000 items |
|---|---|---|---|
| ListView | 34 ms | 38 ms | 40 ms |
| TileView | 13 ms | 73 ms | 77 ms |
| ForEach, virtualized | 13 ms | 16 ms | 15 ms |
| VirtualList | 2.0 ms | 2.2 ms | 2.1 ms |

## Memory per Control

The size of a control plus everything it allocates when it is created, without any text or
children:

| Control | Bytes | Allocations |
|---|---|---|
| TextControl | 1512 | 1 |
| ButtonControl | 752 | 1 |
| CheckBoxControl | 464 | 1 |
| TextBoxControl | 2440 | 1 |
| SliderControl | 744 | 1 |
| StackControl | 728 | 1 |

Settings that most controls never use, such as keyboard accelerators, a tool tip, a cursor or
accessibility properties, live in a block that is only allocated for a control that sets one.

## Measuring It Yourself

The tools are in `benchmarks/` and build with the rest of the tree. ctest does not run them.

```sh
cmake --build build --config RelWithDebInfo --target winui_startup winui_bench winui_timers winui_idle -- -j 2
build/bin/RelWithDebInfo/winui_startup.exe minimal
build/bin/RelWithDebInfo/winui_startup.exe gallery
build/bin/RelWithDebInfo/winui_bench.exe --out bench.json
build/bin/RelWithDebInfo/winui_timers.exe every_20ms 10 buttons
build/bin/RelWithDebInfo/winui_idle.exe 4 3
```

`winui_startup` prints one line of JSON per run, with the milestones in milliseconds, the private
bytes, and the frames and layout passes it took to get there. Run it several times and take the
median, because the first run after a build also pays for reading the executable from disk.

`winui_timers <scenario> [seconds] [tree]` prints one line of JSON: wake-ups, ticks, frames, CPU
time, and how often and how punctually the callback ran. Its scenarios are `idle`, `pending`,
`every_1s`, `every_100ms`, `every_20ms`, `label_20ms` and `after_250ms`. The tree is `minimal`,
`buttons` (1000 of them), or one control's waits: `textbox`, `tooltip`, `flipview`, `gif`, `player`,
`player_once` (both scrolled out of view) and `player_visible`, best measured with `idle`. The CPU
time comes twice: `cpu_ms` from `GetProcessTimes()`, which moves in 15.6 ms steps, and
`mcycles_per_s` from `QueryProcessCycleTime()`, which is exact.

`winui_idle [settle seconds] [measure seconds] [page filter]` prints one line of JSON per gallery
page: timer wake-ups, ticks, frames and CPU time, the latter both as `cpu_ms` and as
`mcycles_per_s`. The first line is the window without a page.

`winui_bench` prints a table and, with `--out`, writes every sample as JSON. `--filter <text>`
runs only the scenarios whose name contains the text, and `--repeats <n>` sets how often each one
runs. Its table also counts measures, text layouts created and allocations per scenario.

The counts behind the claims above (a hover lays out nothing, an unchanged frame creates no text
layout, an idle window wakes up zero times) are checked by the `[perf-count]` tests in
`winui_tests`, so a change that breaks one fails the normal test run.
