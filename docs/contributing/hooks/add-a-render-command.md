# Add a Render Command

A new primitive through every backend.

Controls draw through the `Renderer` interface, and each of its calls has a data form, a render command. A new primitive therefore touches every `Renderer` in the library and the code that prints commands. The running example is `StrokeWavyLine`, the spelling squiggle `TextBox` draws under a misspelled word.

| Step | File |
| --- | --- |
| 1. The command and the virtual | `include/winui/render/Renderer.hpp` |
| 2. Record it | `include/winui/testing/TestRenderer.hpp` (the declaration), `src/testing/TestRenderer.cpp` (the body) |
| 3. Keep it for replay | `include/winui/render/RenderRecording.hpp`, `src/render/RenderRecording.cpp` |
| 4. Print it | `src/render/Describe.cpp` |
| 5. Draw it | `src/platform/windows/D2DRenderer.hpp` |
| 6. Test it | `tests/test_render.cpp` (or the feature's test file), `tests/test_golden.cpp` |

## First, Check You Need One

The interface is small on purpose, and it grows only when a control needs something the existing calls cannot express. Before adding a command, check whether a helper built from existing primitives does the job. The classic 3D bevel is exactly that: `draw_bevel_border()` in `include/winui/render/Bevel.hpp` is a handful of `stroke_line()` and `fill_rounded_rect()` calls, and every backend draws it without knowing it exists. Helpers like that live in `include/winui/render/` with their `.cpp` in `src/render/`.

A new command is right when composing would lose something that matters:

- the semantic dump would turn one meaningful thing into dozens of meaningless lines (a squiggle as 40 line segments),
- the backend can do it far better natively (a real Bézier path, an image blit, an SVG),
- or the result depends on device pixels in a way only the backend can know.

## Renderer Interface

In `include/winui/render/Renderer.hpp`, add the command struct next to its relatives. It holds exactly the arguments of the call, in DIPs and window coordinates, with a defaulted `operator==` so tests can compare whole commands:

```cpp
/// A wavy ("squiggle") line from `from` to `to`: the misspelling underline. The wave oscillates
/// `amplitude` DIPs to either side of the axis, one full period every `wavelength` DIPs.
struct StrokeWavyLine
{
    Point from;                ///< Start of the axis, in DIPs.
    Point to;                  ///< End of the axis, in DIPs.
    Color color;               ///< The stroke color.
    float stroke_width = 1.0f; ///< Pen width in DIPs.
    float amplitude    = 1.0f; ///< Peak distance from the axis, in DIPs.
    float wavelength   = 4.0f; ///< Length of one full period along the axis, in DIPs.

    friend bool operator==(const StrokeWavyLine&, const StrokeWavyLine&) = default;
};
```

Append it to the end of the `RenderCommand` variant, so existing alternatives keep their index. `StrokeWavyLine` went last when it was added, and later commands followed it:

```cpp
using RenderCommand = std::variant<
    FillRoundedRect,
    // ...
    PopOpacityLayer,
    StrokeWavyLine, // the last entry when it was added
    FillPolygon,
    // ...
    StrokePolyline,
    PushClipRoundedRect>; // new commands go last
```

Then add the pure virtual to `Renderer`:

```cpp
/// The misspelling squiggle; see StrokeWavyLine.
virtual void stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude,
                              float wavelength) = 0;
```

Keep the stream minimal. A command should carry what it draws and nothing about why: no control names, no state names, no theme references. A command that takes a large payload (an image, a path) takes it by `const&`, and a `push_*` call must always come with a matching `pop_*`.

Making the virtual pure is deliberate. Every renderer, including one an application wrote, then fails to compile until it handles the new primitive, instead of silently drawing nothing.

## TestRenderer

`TestRenderer` records every call as its command struct. Declare the override in the header and push the command in `TestRenderer.cpp`:

```cpp
void TestRenderer::stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude,
                                    float wavelength)
{
    commands.push_back(StrokeWavyLine{from, to, color, stroke_width, amplitude, wavelength});
}
```

The command is data for tests to read. A command whose argument is large (a bitmap's pixels, SVG markup) records a summary instead, such as the size, so tests stay cheap to write and compare.

`RenderRecording` is the other recorder. It keeps the calls themselves, with everything they need, so a snapshot of an element can be replayed into another renderer later (a connected animation shows its source that way). Declare the override in `RenderRecording.hpp` and capture the arguments by value in `src/render/RenderRecording.cpp`:

```cpp
void RenderRecording::stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width,
                                       float amplitude, float wavelength)
{
    calls_.emplace_back([=](Renderer& r) { r.stroke_wavy_line(from, to, color, stroke_width, amplitude, wavelength); });
}
```

## describe()

`describe()` in `src/render/Describe.cpp` prints one line per command. The semantic goldens are these lines, so a command it does not print is invisible to every golden. Add a branch to the `if constexpr` chain inside the `std::visit`:

```cpp
else if constexpr (std::is_same_v<T, StrokeWavyLine>)
{
    out += "StrokeWavyLine from=(" + fnum(c.from.x) + "," + fnum(c.from.y) + ") to=(" + fnum(c.to.x) + "," +
           fnum(c.to.y) + ") color=" + color_str(c.color) + " width=" + fnum(c.stroke_width) +
           " amplitude=" + fnum(c.amplitude) + " wavelength=" + fnum(c.wavelength) + "\n";
}
```

Note that the chain has no final `else`, so a missing branch compiles and prints nothing. Follow the existing format: the struct's name, then `key=value` pairs, numbers through `fnum()` (two decimals), colors through `color_str()`, rects through `rect_str()`. The output is compared byte for byte, so keep it deterministic: no pointers, no unordered containers, no locale-dependent formatting.

## D2DRenderer

`src/platform/windows/D2DRenderer.hpp` is the Windows backend, used both by the on-screen window and by `OffscreenCapture` for the pixel goldens. It is a private header; the whole class lives in it. Add the override and draw with Direct2D:

```cpp
void stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude,
                      float wavelength) override
{
    const D2D1_POINT_2F a{snap_x(from.x), snap_y(from.y)};
    const D2D1_POINT_2F b{snap_x(to.x), snap_y(to.y)};
    key_begin('w'); // the geometry cache's key: everything the path depends on
    key_add(a);
    key_add(b);
    key_add(amplitude);
    key_add(wavelength);
    ID2D1Geometry* path = cached_geometry([&](ComPtr<ID2D1PathGeometry>&, ID2D1GeometrySink* sink) {
        // ... describe the quadratic Béziers, one per half period, into the sink ...
    });
    if (path)
    {
        target_->DrawGeometry(path, brush(color), stroke_width);
    }
}
```

Things the existing overrides already handle, which a new one should reuse:

- `brush(color)` returns a cached solid brush, `linear_gradient()` and `radial_gradient()` cached
  gradient brushes, `cached_geometry()` a path geometry cached under the key built with
  `key_begin()` and `key_add()`. The renderer lives as long as the window's render target, so a
  resource created per call is created every frame; the `[perf-count]` tests catch that.
- `snap()`, `snap_x()` and `snap_y()` align geometry to device pixels under the current rasterization policy, so thin lines stay crisp at every DPI.
- Failure to create a Direct2D resource returns quietly. A render call never throws.

A backend that cannot draw the primitive faithfully should still draw a reasonable approximation. Drawing nothing is a bug the semantic goldens cannot see.

## Tests

Test the command at three levels:

1. **The recorder and the dump.** One call on a `TestRenderer`, then check the recorded struct and that `describe()` prints it (`tests/test_spell_check.cpp`):

    ```cpp
    TEST_CASE("StrokeWavyLine is recorded verbatim and described", "[render][spellcheck]")
    {
        const Color  red = Color::from_argb(0xFFC42B1C);
        TestRenderer r;
        r.stroke_wavy_line({1.0f, 2.0f}, {30.0f, 2.0f}, red, 1.0f, 1.5f, 4.0f);

        REQUIRE(std::get<StrokeWavyLine>(r.commands[0]) ==
                StrokeWavyLine{{1.0f, 2.0f}, {30.0f, 2.0f}, red, 1.0f, 1.5f, 4.0f});
        REQUIRE(describe(r.commands).starts_with("StrokeWavyLine from=("));
    }
    ```

2. **The control that uses it.** The control's own tests check that it emits the command where expected, through `TestRenderer`.
3. **The Direct2D path.** Inside `#ifdef _WIN32` in `tests/test_golden.cpp`, capture a small scene with `OffscreenCapture` and sample a few pixels to prove the backend drew something in the right place. The `Icon`, `ImageControl` and shapes tests there do exactly this for the image, SVG and geometry commands.

## Goldens

Adding a command changes no golden by itself. A golden changes only when a control in one of its scenes starts emitting the new command. When that happens:

- the semantic golden shows the new line (and the lines it replaced) in the diff, which is the review;
- the pixel golden for the same scene changes as well, if the backend draws differently from what it replaced.

Regenerate with `LIBWINUI_UPDATE_GOLDEN=1`, check that only the scenes containing the changed control moved, and commit the new goldens with the change. See [Write Tests](../development/write-tests.md#updating-goldens).
