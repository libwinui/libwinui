# WinUI Animations

The five animation layers in WinUI, the small motion vocabulary they share, and how libwinui maps onto them.

This study read the animation design note in `microsoft-ui-xaml`[^design-note], the animation-related XAML under `controls/dev/CommonStyles/`, the `AnimatedIcon` control and the engine code under `dxaml/xcp/{core,components}/animation/`. The short version: WinUI's motion is several systems stacked on each other over fifteen years, but the vocabulary they use is tiny, four durations and one curve, and most of what makes a control feel alive is two cheap tricks, a short linear cross-fade on state changes and a decelerating move for anything that travels.

## Five Layers

The design note lists the layers in roughly the order they arrived. None of them replaced the ones before it; all five are still live.

### 1. Storyboards

The original, WPF-derived timeline system in `dxaml/xcp/core/animation/`: `DoubleAnimation`, `ColorAnimation`, `ObjectAnimationUsingKeyFrames`, driven by a time manager and addressable both from markup and from `VisualStateManager::GoToState`[^storyboards].

A large share of the storyboards in the templates were never real animations. They were a single `DiscreteObjectKeyFrame` at time zero, a property assignment wearing an animation's clothes. The `*_perf2026.xaml` rewrite replaces exactly those with plain `VisualState.Setters`, and leaves alone the storyboards that interpolate something. See the [WinUI internals study](winui-internals.md#the-visualstatemanager) for that rewrite.

### 2. Theme Transitions

`EntranceThemeTransition`, `ContentThemeTransition`, `RepositionThemeTransition`, `AddDeleteThemeTransition`, `PopupThemeTransition`, `NavigationThemeTransition` and friends cover the recurring cases of something entering, leaving or moving[^transitions]. They are built on storyboards. Each one gets a `CLayoutTransitionElement` (`CTransition::SetupTransition`), a redirected-rendering proxy that lets the animated element escape its normal clip and z-order for the duration: a list item can slide *over* its neighbors while the list reflows, and a flyout can draw outside its logical parent. `RewriteTransitionTargets` retargets the storyboard from the real element onto the proxy.

The timings are not in the XAML. WinUI reads them at run time from the OS animation theme data (`OpenThemeData(nullptr, "Animations")`), the same data the shell uses.

### 3. Theme Animations

`FadeInThemeAnimation`, `FadeOutThemeAnimation`, `RepositionThemeAnimation`, `DragItemThemeAnimation`, `DragOverThemeAnimation` and `DropTargetItemThemeAnimation` are prepackaged storyboards for one visual effect each, invoked from a visual state's storyboard instead of written as raw key frames. They are what is left after `perf2026` removed the disguised setters, because they are genuine interpolated motion.

### 4. Composition Animations

`Microsoft.UI.Composition` offers three separate APIs, all of which run in the compositor without the UI thread ("independent animation"):

- **Implicit property transitions**: `BrushTransition`, `ScalarTransition` and `Vector3Transition`, declared once on an element and fired on every later change of that property, with no storyboard at all[^property-animations]. This is what drives the 83 ms hover and press cross-fade in every modern control template, through `CBorder::SetValue` and `WUCBrushManager::SetUpBrushTransitionIfAllowed`.
- **Escape hatches**: `ElementCompositionPreview.GetElementVisual` hands out the real composition visual behind an element, and `UIElement.StartAnimation` lets a composition key-frame animation drive a XAML property. Controls use them too; `ScrollPresenter.cpp` is one example.
- **`ImplicitAnimations`** on any composition object: the general form of the first bullet, for any composition property. XAML explicitly disables this collection during its own render walk (`HWCompNodeWinRT.cpp`) so it cannot fight XAML-driven updates.

### 5. Connected Animations

`ConnectedAnimationService` animates one specific element across a page transition: the "hero" animation where a list thumbnail grows into the detail page's header image[^connected]. It is keyed by a string handle (`PrepareToAnimate(key, element)` on the page being left, `GetAnimation(key).TryStart(destination)` on the page arriving) rather than by anything in a template, and it is orthogonal to the other four layers.

### And AnimatedIcon

`AnimatedIcon` is a sixth thing on the side: a Lottie-driven vector animation exposed as an icon element[^animated-icon]. Templates set its `State` attached property from ordinary visual-state setters, and the animation segments (normal to hover, hover to pressed, and so on) live inside the Lottie file as named markers. The control only ever says which named state it wants. This is how the modern CheckBox's check mark draws itself on instead of appearing.

## Motion Vocabulary

`Common_themeresources_any.xaml` defines the entire timing vocabulary in five resources, reused verbatim by every control:

| Token | Value | Used for |
| --- | --- | --- |
| `ControlFasterAnimationDuration` | 83 ms | brush and opacity cross-fades (hover, press) |
| `ControlFastAnimationDuration` | 167 ms | slightly larger moves |
| `ControlFastAnimationAfterDuration` | 168 ms | the second half of a two-phase transition, so the total lines up on whole frames |
| `ControlNormalAnimationDuration` | 250 ms | bigger, structural transitions (CalendarView's month flip, Slider's thumb grow) |
| `ControlFastOutSlowInKeySpline` | cubic Bézier `0,0,0,1` | the one curve used wherever a `Spline*KeyFrame` appears |

They are stored as strings, not typed `Duration` or `KeySpline` resources, and parsed at the point of use. That detail is not worth copying. The point is how few values there are: four durations and one curve, shared across the platform, tokenized exactly like the color and metric tokens[^timing].

The curve's name is misleading. `0,0,0,1` has both interior control points pulled to the start of the time axis, so it is a pure **decelerate**: fast start, slow finish. It is not the symmetric ease-in-out shape "fast out, slow in" suggests.

Two other curves appear outside the control templates. Implicit `ScalarTransition` and `Vector3Transition` use a cubic Bézier of `(0.8,0)-(0.2,1)` (`SharedTransitionAnimations.cpp`), while `BrushTransition` has no easing parameter and is always linear.

## Case Studies

Each mechanism above shows up in a recognizable shape in the templates.

**Button, AppBarButton, AppBarToggleButton hover and press.** A `BrushTransition` of 83 ms on `Background` and `BorderBrush`. No key frames, no easing: brush transitions are linear.

**ToggleSwitch knob.** Two mechanisms at once (`ToggleSwitch_themeresources.xaml`). The on and off tracks, and the on and off knobs, are each two stacked, pre-positioned visuals cross-faded by opacity. On top of that, a `RepositionThemeAnimation` slides the knob's transform from its old offset to the new one. So the knob's *appearance* changes by cross-fade and its *travel* is a real position animation. On `PointerOver` the knob grows from 12 to 14 px, and on `Pressed` the template swaps `HorizontalAlignment` and `Margin` so the knob stretches to 17 × 14 toward the far edge of its slot while staying anchored at the near one. Both use `ControlFasterAnimationDuration`.

**Slider thumb.** The inner circle of the thumb grows on hover and shrinks on press. The durations are keyed by the state being *entered*: `ControlNormalAnimationDuration` (250 ms) going into PointerOver or Pressed, `ControlFastAnimationDuration` (167 ms) going back to Normal or Disabled.

**CalendarView month and year navigation.** The most elaborate storyboard left (`CalendarView_themeresources.xaml`): `SplineDoubleKeyFrame` sequences with explicit key times (0.150 s, then 0.500 s) driving scale and opacity on the outgoing and incoming header content, all on `ControlFastOutSlowInKeySpline`. Genuine multi-key-frame motion, which is why `perf2026` left it alone.

**ListViewItem drag, reorder and multi-select.** `DragOverThemeAnimation`, `DragItemThemeAnimation` and `DropTargetItemThemeAnimation`, plus paired fade-in and fade-out animations on an overlay text element that the source comments mark as required. They are not decoration: the fade covers a discrete content swap in the middle of the animation, and removing it breaks the interaction.

**ToolTip open and close.** Bare `FadeInThemeAnimation` and `FadeOutThemeAnimation`, the simplest use of the theme-animation layer.

**NavigationView content change.** `NavigationView.cpp` picks a `SlideNavigationTransitionInfo` (from the left, from the right, or from the overflow, depending on where the newly selected item sits relative to the old one) or falls back to `EntranceNavigationTransitionInfo`. The animation belongs to the `Frame`, not to NavigationView itself.

## Libwinui's Animation Surface

libwinui has no storyboards addressed by property name, because it has no dependency-property system. It carries WinUI's vocabulary in two places, split by who runs the animation.

### The Compositor Seam: Control State Changes Off the UI Thread

`Compositor` (`src/render/Compositor.hpp`, private to libwinui) is the seam between a control's visual state and the system compositor. A control calls it on state *transitions*, not every frame, from `Element::sync_composition_override()`. The Win32 host implements it with DirectComposition (`src/platform/windows/DCompCompositor.cpp`); tests use `TestCompositor`, which records the calls the same way `TestRenderer` records render commands; a headless host has none.

Overlays take three calls. `sync_visual()` places a filled rounded-rect overlay, `remove_visual()` drops it, and `animate_opacity()` fades it, always linearly, like `BrushTransition`.

Layers are the second half of the interface. A layer is part of an element's own drawing that the compositor shows and animates in place of the window's frame: the window places, paints and removes it (`sync_layer()`, `paint_layer()`, `remove_layer()`), and the element starts key-frame animations of its offset, scale, rotation, opacity or clip on it (`animate_layer()`). The indeterminate `ProgressBar` and `ProgressRing` run this way, so they keep moving with no UI-thread work. A compositor without layers says so (`supports_layers()`), and the window then draws everything into its frame.

The parts that move or resize are drawn by the control itself, eased on the UI thread by a `detail::RectTween` that `sync_composition_override()` aims and `tick_override()` advances. Drawing them in the frame keeps them under popups and clips, which an overlay on top of the window would cover.

The controls, with durations from `Theme`:

| Control | What changes | Where | Duration | Easing |
| --- | --- | --- | --- | --- |
| `Button` | hover/press wash, opacity 0 to 0.16 | compositor | `anim_faster` (83 ms) | linear |
| `CheckBox` | the same wash on the box | compositor | `anim_faster` | linear |
| `RadioButton` | the wash on the ring | compositor | `anim_faster` | linear |
| `RadioButton` | the checked dot resizing between `ControlChrome::radio_dot` sizes (12, 14, 10) | frame | `anim_fast` (167 ms) | `anim_fast_out_slow_in` |
| `ToggleSwitch` | the wash on the track | compositor | `anim_faster` | linear |
| `ToggleSwitch` | the knob sliding by `travel` 20, growing 12 to 14 on hover, stretching to 17 × 14 on press | frame | `anim_fast` for a slide, `anim_faster` for a resize alone | `anim_fast_out_slow_in` |
| `Slider` | the thumb's inner circle growing and shrinking | frame | `anim_normal` (250 ms) into hover or pressed, `anim_fast` back | `anim_fast_out_slow_in` |

The opacity fade being linear is deliberate, because WinUI's own `BrushTransition` is linear. The frame-drawn motion follows `ControlFastOutSlowInKeySpline`.

`IDCompositionAnimation` segments are time polynomials, not Bézier key splines, which is one more reason the eased parts are drawn in the frame: the CPU evaluates a curve exactly, `easing::evaluate()` inverting the Bézier's x(t) by 24 bisection steps. The opacity fade is a straight ramp, one linear segment.

### Element Animations: Everything Else, on the UI Thread

For everything that is not a control's own state wash, `include/winui/ui/Animation.hpp` carries the rest of WinUI's vocabulary, and `Element` exposes it (`include/winui/ui/Element.hpp`):

- **Visual properties.** Every element has `opacity`, `translation`, `rotation`, `scale` and `center_point`, WinUI's `UIElement` properties that do not affect layout.
- **Implicit transitions.** `set_opacity_transition()`, `set_rotation_transition()`, `set_translation_transition()` and `set_scale_transition()` take a `ScalarTransition` or `Vector3Transition`, which animate every later change along `(0.8,0)-(0.2,1)`. `BrushTransition` and its `ColorAnimator` blend a background color linearly, from the color on screen at the moment it changes.
- **Explicit animations.** `start_animation(AnimatedProperty, Animation)` runs a `DoubleAnimation` (from, to, by, duration, easing, auto-reverse, repeat count, fill behavior), a `KeyFrameAnimation` (linear, discrete, spline and easing key frames), a `SpringAnimation` (damping ratio and period) or an `ExpressionAnimation` (a C++ callable evaluated every frame instead of an expression string). The eleven easing functions of WinUI's `EasingFunctionBase` are ported as `BackEase`, `BounceEase`, `CircleEase` and so on.
- **Theme transitions.** `set_transitions()` and `set_children_transitions()` take `EntranceTransition`, `RepositionTransition`, `ContentTransition`, `AddRemoveTransition`, `PopupTransition` and `NavigationTransition`, libwinui's names for WinUI's theme transitions. Their timings are copied from the OS animation theme data as Windows 11 ships it, for example 667 ms along `(0.1,0.9)-(0.2,1)` for an entrance, 35 ms stagger between elements entering together, and an 83 ms fade when a popup closes.
- **Navigation transitions.** `NavigationFrame` takes a `PageTransition`: `RisePageTransition`, `ZoomPageTransition`, `SlidePageTransition`, `FadeThroughPageTransition` and `NoPageTransition` stand for WinUI's Entrance, DrillIn, Slide, Continuum and Suppress transition infos.
- **Connected animations.** `ConnectedAnimationService` (`include/winui/ui/ConnectedAnimation.hpp`) follows WinUI's flow: `prepare_to_animate(key, source)` snapshots the source, `animation(key)->try_start(destination)` flies a cross-fade of the two snapshots from one rect to the other. WinUI's Gravity (the default, dipping 80 DIPs over 300 ms), Direct and Basic configurations are there as the flight paths `ArcFlightPath`, `ShortFlightPath` and `StraightFlightPath`, and an animation prepared but never started is dropped after two seconds. Since libwinui draws through render commands rather than composition surfaces, a snapshot is a `RenderRecording` of the element's command stream.
- **Animated icons.** `AnimatedIcon` and `AnimatedVisualPlayer` play between named markers the way WinUI's do, with a set of stock visuals (back, chevrons, find, settings, the navigation button) in `include/winui/ui/media/AnimatedVisuals.hpp`.

These run on the UI thread. The host drives `Window::tick()` from a timer, each element advances its own animations in `Element::tick()`, and the result reaches the screen as an opacity layer and a transform in the element's render stream (`PushOpacityLayer`, `PushTransform`). Tests call `Window::tick()` with a synthetic delta, so no real clock is involved.

This split answers a problem the compositor seam could not. A compositor overlay is a solid rounded rect drawn *on top of* already rendered content, so it can never fade the content itself. A popup fading in, or a list item entering, needs its whole rendered output to move and fade as a unit, and an opacity layer in the render stream does exactly that. The cost is that these animations are not independent of the UI thread the way WinUI's composition animations are.

Without a dependency-property system an animation never names its target. Storyboard-style and Composition-style animations run on an element's own visual properties (`AnimatedProperty`, `Element::start_animation()`), and anything else animates through a callback. An expression animation is a C++ callable evaluated every frame: a lambda reading `card->current_scale()` takes the place of the string `"Vector3(1 / card.Scale.X, ...)"`. The easing functions are a whole port of `dxaml/xcp/components/animation/EasingFunctions.cpp`, the theme transitions follow `LayoutTransition_partial.cpp` and the implicit transitions follow `SharedTransitionAnimations.cpp`.

### Theme and Navigation Transitions in Detail

`NavigationFrame` picks its transition info in the same order as WinUI's `NavigationThemeTransition::CreateStoryboard`: the transition handed to `navigate()`, else the `NavigationTransition`'s default, else `RisePageTransition`. The info builds one storyboard for the page leaving and one for the page arriving, and both play at once. Going back plays the same info backwards (WinUI's Back triggers), taken from the entry being left unless `go_back()` is handed another one. Without a `NavigationTransition` in the frame's content transitions, navigation is immediate.

| Page transition | What plays |
| --- | --- |
| `RisePageTransition` | the old page fades out over 150 ms while the new one waits invisible, then the new page comes in from 140 DIPs down |
| `ZoomPageTransition` | the new page grows in from 94 % |
| `NoPageTransition` | nothing |

`AddRemoveTransition` on a list follows the list contexts of `LayoutTransition_partial.cpp`. An inserted row waits for the rows it pushes down: they start one row up, where they were, and move over 400 ms (the `ADDTOGRID` timing) before the new row grows in after 300 ms. A removed row shrinks to 90 % and fades out over 100 ms where it was, and the rows closing its gap wait 220 ms and then take 333 ms (`DELETEFROMGRID`). When an add and a delete happen together, the add waits 600 ms.

### Connected Animations

The flow matches WinUI's. `prepare_to_animate()` shows the source's snapshot where the source was, so the element seems to stay put while the page under it changes. After `try_start()`, the next layout snapshots and hides the destination, and for the duration a cross-fade of both snapshots moves and scales from the source rect to the destination rect. Then the destination shows again and Completed fires.

An `ArcFlightPath`, the counterpart of WinUI's Gravity configuration, also scales the element 10 % up at the bottom of its dip when its size does not otherwise change, and unless its `.shadow` is false draws a drop shadow under it at elevation 32, like a flyout. Coordinated elements, passed as `try_start()`'s second argument, fly in alongside the destination from where they would sit relative to the source, fading in. The service belongs to a `Window` (`Window::connected_animation_service()`, WinUI's `GetForCurrentView()`) and draws above the tree and below the popups.

### Animated Icons

`AnimatedIcon` ports the state machine of WinUI's `AnimatedIcon.cpp`. Going from state A to state B, it looks for markers in this order (`TransitionStates`):

1. `AToB_Start` and `AToB_End`: play that segment.
2. Either of them alone.
3. An `AToB` marker, then a `B` marker: cut to it.
4. Any marker containing `ToB_End` (the first in key order): cut to it.
5. A numeric state name: `"0.5"` plays to that progress.

With none of them it cuts to 0. The state machine starts with no state, so the first `set_state()` finds no `ToB_Start` and cuts to the end of whichever transition ends in B, like WinUI's first visual state change.

A state set while a segment plays is queued. The default `QueueOne` keeps up to four queued states, and a fifth cuts the running animation over to the oldest one (`OnLayoutUpdatedAfterStateChanged`). `SpeedUpQueueOne` plays the running and queued transitions seven times faster (`m_speedUpMultiplier`). Like `TransitionAndUpdateStates`, the port moves the state on before the segment can complete. A segment under 20 ms completes inside the very call that starts it, and completing starts the next queued state, so a queue that still held the current state started it again until the stack overflowed. An earlier port did exactly that with DropDownButton's chevron (pressed, released, and the pointer left while the release still played; PointerOver to Normal is a zero-length segment).

The icon's foreground (the theme text color unless set) is handed to the source for every frame, like WinUI's `SetColorProperty("Foreground")`. `MirroredWhenRightToLeft` is not ported, since there is no right-to-left layout.

`AnimatedVisualPlayer` follows `AnimatedVisualPlayer.cpp`:

- A play from a higher to a lower progress runs from `from` to the end and on from the start to `to`. PlayAsync's adjustments are kept: `to == 0` with `from > 0` plays to 1, and `from == 1` with `to > 0` plays from 0. A play shorter than 20 ms jumps straight to `from`.
- A negative playback rate starts the play at its end. A looped play wraps either way, a non-looped one completes at whichever end it hits.
- `pause()` and `resume()` keep `is_playing()` true. `stop()` and `set_progress()` complete the play, and `stop()` rewinds to 0 (WinUI's `m_currentPlayFromProgress` never moves off 0 either).
- A play requested with no source loaded never starts, and setting a source stops whatever was playing.
- Stretch `None` draws top-left, unscaled and cut off at the edge; `Fill` fills; `Uniform` fits and centers; `UniformToFill` covers, centers and clips. `Fill` and `UniformToFill` fall back to `Uniform` along an unbounded axis.
- Not ported: `Diagnostics`, `ProgressObject` (`progress()` reads the same value), `AnimationOptimization`, and PlayAsync's awaitable (poll `is_playing()` instead). WinUI's `FallbackContent` is a `DataTemplate`; here it is an element.

The stock visuals in `include/winui/ui/media/AnimatedVisuals.hpp` are WinUI's LottieGen-generated Composition code (`controls/dev/AnimatedIcon/AnimatedVisuals/*.cpp`) re-read as data: the same path coordinates, key frames, easing curves and markers, evaluated at a progress and drawn through `Renderer::draw_geometry()`. Every source is 48 × 48 and stateless. `AnimatedAcceptVisualSource` is left out, because its strokes animate `TrimStart`/`TrimEnd` and the renderer has no path trimming.

They run on a small private Composition runtime (`src/ui/media/AnimatedVisualsDetail.hpp`):

- A value between two keys is the earlier key's value eased toward the later one, under the later key's easing. Before the first key the initial value eases toward it, after the last it holds.
- The step easings are LottieGen's `HoldThenStep` (`IsFinalStepSingleFrame`) and `StepThenHold` (`IsInitialStepSingleFrame`). A path key is LottieGen's cubic Bézier expression key frame over one of the root's `tN` scalars.
- A shape transform composes in Composition's order: translate by `-CenterPoint`, scale, rotate, translate by `CenterPoint`, `TransformMatrix`, translate by `Offset`. A child's matrix is followed by its container's, and the stroke thickness scales with the transform, as in Composition.

## Gaps and Non-Gaps

### Still Open

- **No generic compositor-side property transition.** WinUI's `BrushTransition` works for any brush property on any element and runs in the compositor. libwinui's compositor path is one bespoke overlay per control, and a new animated property there (a border-color fade, a text-color fade) needs its own overlay and `sync_composition()` logic. The UI-thread `BrushTransition` covers the general case, just not off-thread.
- **Element animations are not independent.** A busy UI thread stalls them, where WinUI's composition animations keep running.
- **`TestCompositor` is a call recorder.** It does not model two animations composing on one layer. That composition is checked by the DirectComposition backend compiling against the real API and by tests asserting on the calls a control makes, not by an assertion that DirectComposition draws both.
- **Approximated easing on the compositor.** A curved key frame on a layer becomes 16 straight pieces there, because DirectComposition segments are polynomials of time.

### Not Gaps

- **State to resolved visual instead of a storyboard.** WinUI's `perf2026` rewrite converges on the model libwinui uses: storyboards survive in the modern templates only where they are real interpolated motion, never as disguised setters.
- **A linear hover fade.** That is what `BrushTransition` does.
- **A headless test double in front of the platform compositor.** `TestCompositor` mirrors `DCompCompositor` the way `TestRenderer` mirrors the Direct2D renderer, so animation logic is testable without a window.
- **Skipping Lottie for the check mark.** A static glyph loses nothing important, and `AnimatedIcon` exists for the places (navigation items, chevrons) where the animated version is the recognizable one.

## Sources

[^design-note]: `microsoft/microsoft-ui-xaml`, `docs/design-notes/animation.md`. <https://github.com/microsoft/microsoft-ui-xaml>
[^storyboards]: Microsoft Learn, "Storyboarded animations". <https://learn.microsoft.com/windows/apps/design/motion/storyboarded-animations>
[^transitions]: `microsoft-ui-xaml`, `dxaml/xcp/dxaml/lib/LayoutTransition_partial.cpp`; Microsoft Learn, "Animations in XAML" (theme transitions and theme animations). <https://learn.microsoft.com/windows/apps/design/motion/xaml-animation>
[^property-animations]: `microsoft-ui-xaml`, `dxaml/xcp/components/comptree/SharedTransitionAnimations.cpp`; Microsoft Learn, "Animating XAML elements with composition animations" / "XAML property animations". <https://learn.microsoft.com/windows/apps/design/motion/xaml-property-animations>
[^connected]: `microsoft-ui-xaml`, `dxaml/xcp/components/animation/ConnectedAnimation.cpp` and `ConnectedAnimationService.cpp`; Microsoft Learn, "Connected animation". <https://learn.microsoft.com/windows/apps/design/motion/connected-animation>
[^animated-icon]: `microsoft-ui-xaml`, `controls/dev/AnimatedIcon/AnimatedIcon.cpp` and `controls/dev/AnimatedIcon/Docs/AnimatedIconDevDesign.md`; Microsoft Learn, "Animated icon". <https://learn.microsoft.com/windows/apps/design/controls/animated-icon>
[^timing]: `microsoft-ui-xaml`, `controls/dev/CommonStyles/Common_themeresources_any.xaml`; Microsoft Learn, "Timing and easing". <https://learn.microsoft.com/windows/apps/design/motion/timing-and-easing>
