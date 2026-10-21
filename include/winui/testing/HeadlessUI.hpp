// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/PerfCounters.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestJumpListHost.hpp"
#include "winui/testing/TestLauncher.hpp"
#include "winui/testing/TestMessageBoxHost.hpp"
#include "winui/testing/TestNotifier.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui::detail
{
class HeadlessHostAdapter; // the WindowHost HeadlessUI installs, in HeadlessUI.cpp
} // namespace ui::detail

namespace ui::testing
{

/// Deterministic, platform-independent text metrics. They don't match the real text renderer's
/// metrics, but they make layout reproducible in tests and on any OS.
///
/// Every code point advances by `font_size * AdvanceRatio` (nudged by weight, plus character
/// spacing); a line is `font_size * LineRatio` tall unless the style sets a line height. The
/// semantic goldens are laid out with it. HeadlessUI owns one; construct your own to measure or
/// lay out a bare element in a test.
///
/// Example:
/// @code
/// const ui::testing::HeadlessTextMeasurer measurer;
/// ui::TextStyle style;
/// style.font_size = 20.0f;
/// const ui::Size one = measurer.measure("abcd", style);
/// REQUIRE(one.width == 4 * 20.0f * ui::testing::HeadlessTextMeasurer::AdvanceRatio);
///
/// ui::ButtonControl button{"OK"};
/// const ui::Size desired = button.measure(ui::Size{200, ui::Unbounded}, measurer, ui::Theme::default_light());
/// @endcode
class HeadlessTextMeasurer final : public TextMeasurer
{
public:
    /// The advance width per em, a rough monospace stand-in for a proportional UI font.
    static constexpr float AdvanceRatio = 0.55f; // advance width per em
    /// Line height per em, used when TextStyle::line_height is unset.
    static constexpr float LineRatio = 1.35f; // line height per em, when style.line_height is unset
    /// The baseline's depth into a line, as a share of the line height.
    static constexpr float BaselineRatio = 0.8f;

    /// A weight-dependent nudge on top of AdvanceRatio. Lighter weights sit slightly narrower and
    /// heavier ones slightly wider, so a semantic golden orders weights correctly.
    [[nodiscard]] static constexpr float weight_advance_factor(FontWeight w)
    {
        switch (w)
        {
            case FontWeight::Light:
                return 0.94f;
            case FontWeight::SemiLight:
                return 0.97f;
            case FontWeight::Regular:
                return 1.00f;
            case FontWeight::SemiBold:
                return 1.06f;
            case FontWeight::Bold:
                return 1.10f;
        }
        return 1.00f;
    }

    /// One line of `text`: code points times the advance, by one line height. `\n` is not
    /// treated as a break here; measure_wrapped() handles it.
    [[nodiscard]] TextMetrics measure(std::string_view text, const TextStyle& style) const override
    {
        detail::count(detail::PerfCounter::TextMeasurements);
        return measure_line(text, style);
    }

    [[nodiscard]] TextMetrics measure_wrapped(std::string_view text, const TextStyle& style, float max_width) const override
    {
        detail::count(detail::PerfCounter::TextMeasurements);
        return wrap(text, style, max_width);
    }

private:
    [[nodiscard]] static TextMetrics measure_line(std::string_view text, const TextStyle& style)
    {
        // One advance per code point, so "ä" is as wide as "a" rather than twice it.
        const float w = static_cast<float>(code_point_count(text)) *
                        (style.font_size * AdvanceRatio * weight_advance_factor(style.weight) +
                         style.character_spacing_dips());
        const float h = style.line_height > 0.0f ? style.line_height : style.font_size * LineRatio;
        return {.width = w, .height = h, .baseline = h * BaselineRatio, .line_count = 1};
    }

    /// Greedy wrapping over the same fixed advance: words separated by spaces fill a line while
    /// they fit, `\n` always starts one, and a word longer than a whole line is split at the line
    /// width under Wrap and left to overflow under WrapWholeWords. Text that fits on one line
    /// measures exactly as measure() does.
    [[nodiscard]] static TextMetrics wrap(std::string_view text, const TextStyle& style, float max_width)
    {
        const TextMetrics single  = measure_line(text, style);
        const float       advance = style.font_size * AdvanceRatio * weight_advance_factor(style.weight) +
                                    style.character_spacing_dips();
        if (style.wrapping == TextWrapping::NoWrap || !(max_width > 0.0f) || !std::isfinite(max_width) ||
            advance <= 0.0f || (single.width <= max_width && text.find('\n') == std::string_view::npos))
        {
            return single;
        }

        const std::size_t fit    = (std::max<std::size_t>)(1, static_cast<std::size_t>(max_width / advance));
        std::size_t       lines  = 0;
        std::size_t       widest = 0;
        std::size_t       begin  = 0;
        while (begin <= text.size())
        {
            const std::size_t      newline   = text.find('\n', begin);
            const std::size_t      end       = newline == std::string_view::npos ? text.size() : newline;
            const std::string_view paragraph = text.substr(begin, end - begin);

            ++lines;
            std::size_t line = 0; // characters on the current line, trailing spaces excluded
            std::size_t pos  = 0;
            while (pos < paragraph.size())
            {
                const std::size_t start = paragraph.find_first_not_of(' ', pos);
                if (start == std::string_view::npos)
                {
                    break;
                }
                std::size_t stop       = paragraph.find(' ', start);
                stop                   = stop == std::string_view::npos ? paragraph.size() : stop;
                const std::size_t word = code_point_count(paragraph.substr(start, stop - start));
                const std::size_t gap  = line > 0 ? start - pos : 0;

                if (line + gap + word <= fit)
                {
                    line += gap + word;
                }
                else
                {
                    if (line > 0)
                    {
                        widest = (std::max)(widest, line);
                        ++lines;
                    }
                    if (word <= fit || style.wrapping == TextWrapping::WrapWholeWords)
                    {
                        line = word;
                    }
                    else
                    {
                        const std::size_t full = (word - 1) / fit;
                        lines += full;
                        widest = (std::max)(widest, fit);
                        line   = word - full * fit;
                    }
                }
                pos = stop;
            }
            widest = (std::max)(widest, line);

            if (newline == std::string_view::npos)
            {
                break;
            }
            begin = newline + 1;
        }

        const float line_height = style.line_height > 0.0f ? style.line_height : style.font_size * LineRatio;
        return {.width      = static_cast<float>(widest) * advance,
                .height     = static_cast<float>(lines) * line_height,
                .baseline   = line_height * BaselineRatio,
                .line_count = static_cast<int>(lines)};
    }
};

/// The in-memory window host HeadlessUI installs. It follows the same window-state rules as the
/// real host without a native window, so window-state and title-bar tests can drive it.
///
///   * Requests apply synchronously by default. Set `defer_requests` to queue them instead, so a
///     test can observe the gap between a request and the system's answer, then call
///     apply_pending_requests() to let them land.
///   * simulate_state() is the other direction: the user clicking the caption's maximize
///     button, or Win+Down. It reports the new state without anyone having asked.
///   * Everything the window sends the host is recorded, and click_system_menu_item() and
///     click_thumbnail_button() stand in for the user picking an entry.
///
/// Like the real host, it remembers the normal bounds across minimize and maximize, and whether
/// full screen was entered from maximized, so restore() goes back to the right place.
///
/// Example:
/// @code
/// ui::testing::HeadlessUI ui;
/// ui::Window& window = ui.window();
///
/// ui.host().defer_requests = true;
/// window.request_state(ui::WindowState::Maximized);
/// REQUIRE(window.state().get() == ui::WindowState::Normal); // asked, not yet answered
/// ui.host().apply_pending_requests();
/// REQUIRE(window.state().get() == ui::WindowState::Maximized);
///
/// window.set_system_menu({{.text = "Always on top", .on_click = [&] { /* ... */ }}});
/// REQUIRE(ui.host().click_system_menu_item(0));
///
/// window.close();
/// REQUIRE(ui.host().close_requests == 1);
/// @endcode
class HeadlessWindowHost final
{
public:
    /// A host for `window`, which must outlive it. Does not install itself; HeadlessUI does.
    explicit HeadlessWindowHost(Window& window);
    ~HeadlessWindowHost();
    HeadlessWindowHost(const HeadlessWindowHost&)            = delete;
    HeadlessWindowHost& operator=(const HeadlessWindowHost&) = delete;

    // ---- what the window asks of its host ----
    /// Applies `state` at once and reports it back, or queues it while `defer_requests` is set.
    void request_state(WindowState state);
    /// Leaves Minimized/Maximized/Fullscreen for the state it came from; queued like
    /// request_state() while `defer_requests` is set.
    void restore();
    /// Only counts the request in `close_requests`; the window is not destroyed.
    void close()
    {
        ++close_requests;
    }
    /// Only counts the request in `wake_requests`; safe from any thread.
    void wake()
    {
        ++wake_requests; // from any thread
    }
    /// Records the title for title().
    void set_title(std::string_view title)
    {
        title_ = std::string{title};
    }
    /// Records the style for style() and counts it in `style_applications`.
    void set_style(const WindowStyle& style)
    {
        style_ = style;
        ++style_applications;
    }
    /// Records the chrome overrides for chrome().
    void set_chrome(const ChromeOverrides& chrome)
    {
        chrome_ = chrome;
    }
    /// `normal_bounds`, the current state, and whether a restore would go back to Maximized.
    [[nodiscard]] std::optional<WindowPlacement> placement() const;
    /// Records only whether there is an icon (has_icon()).
    void set_icon(const WindowIcon* icon)
    {
        has_icon_ = icon != nullptr;
    }
    /// Records the entries' texts, checks and enabled states for click_system_menu_item().
    void set_system_menu(const std::vector<MenuItem>& items);
    /// Records the progress for taskbar_progress().
    void set_taskbar_progress(const TaskbarProgress& progress)
    {
        taskbar_progress_ = progress;
    }
    /// Records whether there is an overlay and its description.
    void set_taskbar_overlay(const WindowIcon* icon, std::string_view description)
    {
        has_overlay_         = icon != nullptr;
        overlay_description_ = std::string{description};
    }
    /// Records the flash request for flashing().
    void request_attention(bool flash)
    {
        flashing_ = flash;
    }
    /// Fixes the button count on the first call (at most 7), as the taskbar does. Later calls
    /// only update which of those buttons are clickable.
    void set_thumbnail_buttons(const std::vector<ThumbnailButton>& buttons);

    // ---- position, size and visibility ----
    // The window's outer rect is `normal_bounds`; its client area that less `frame`.
    /// Moves `normal_bounds` to `position`.
    void move(Point position)
    {
        normal_bounds.x = position.x;
        normal_bounds.y = position.y;
    }
    /// Resizes `normal_bounds` so its client part, less `frame`, is `size`, clamped to the size
    /// limits.
    void resize(Size size)
    {
        resize_outer(Size{size.width + frame.horizontal(), size.height + frame.vertical()});
    }
    /// Always `normal_bounds`.
    [[nodiscard]] std::optional<Rect> bounds() const
    {
        return normal_bounds;
    }
    /// Always `screen_work_area`.
    [[nodiscard]] std::optional<Rect> work_area() const
    {
        return screen_work_area;
    }
    /// Reports visibility to the window, and activates it when asked to.
    void set_visible(bool visible, bool activate)
    {
        ui::testing::seams(window_).report_visible(visible);
        if (visible && activate)
        {
            this->activate();
        }
    }
    /// Counts the activation in `activations` and reports the window active.
    void activate()
    {
        ++activations;
        ui::testing::seams(window_).report_active(true);
    }
    /// Records the client size limits and re-clamps the current size, as a real host does.
    void set_size_limits(Size min, Size max)
    {
        min_size_ = min;
        max_size_ = max;
        resize_outer(Size{normal_bounds.width, normal_bounds.height}); // re-clamped, as a real host does
    }

    // ---- test controls ----
    /// When true, requests are queued instead of applied (see the class comment).
    bool defer_requests = false;
    /// Applies every queued request in order, reporting each resulting state.
    void apply_pending_requests();
    /// How many requests are queued while `defer_requests` is set.
    [[nodiscard]] std::size_t pending_request_count() const
    {
        return pending_.size();
    }

    /// A change the user makes from outside the app. Reports `state` and, while not
    /// minimized, remembers it for restore().
    void simulate_state(WindowState state);
    /// The user activating another window, or coming back.
    void simulate_active(bool active)
    {
        ui::testing::seams(window_).report_active(active);
    }
    /// Reports the caption-button insets of an extended title bar, as the system would.
    void simulate_caption_insets(const CaptionInsets& insets)
    {
        ui::testing::seams(window_).report_caption_insets(insets);
    }
    /// Picks the app's system-menu entry `index`, as the user would. False if the host was never
    /// given that many entries.
    bool click_system_menu_item(std::size_t index);
    /// Clicks thumbnail button `index`. False if it doesn't exist, or is disabled or hidden.
    bool click_thumbnail_button(std::size_t index);

    /// The bounds a restore to Normal lands on, and what bounds() reports.
    Rect normal_bounds{0.0f, 0.0f, 800.0f, 600.0f};
    /// What the frame takes around the client area, for resize() and the size limits.
    Thickness frame{};
    /// What work_area() reports.
    Rect screen_work_area{0.0f, 0.0f, 1920.0f, 1040.0f};

    // ---- what the host was told ----
    /// The last title set_title() was given.
    [[nodiscard]] const std::string& title() const
    {
        return title_;
    }
    /// The last style set_style() was given.
    [[nodiscard]] const WindowStyle& style() const
    {
        return style_;
    }
    /// The last chrome overrides set_chrome() was given.
    [[nodiscard]] const ChromeOverrides& chrome() const
    {
        return chrome_;
    }
    /// Whether the last set_icon() passed an icon.
    [[nodiscard]] bool has_icon() const
    {
        return has_icon_;
    }
    /// The system menu entries' texts, in order; empty text is a separator.
    [[nodiscard]] const std::vector<std::string>& system_menu_texts() const
    {
        return system_menu_texts_;
    }
    /// The system menu entries' check marks, parallel to system_menu_texts().
    [[nodiscard]] const std::vector<bool>& system_menu_checks() const
    {
        return system_menu_checks_;
    }
    /// The last taskbar progress set.
    [[nodiscard]] const TaskbarProgress& taskbar_progress() const
    {
        return taskbar_progress_;
    }
    /// Whether a taskbar overlay icon is set.
    [[nodiscard]] bool has_overlay() const
    {
        return has_overlay_;
    }
    /// The overlay's accessible description.
    [[nodiscard]] const std::string& overlay_description() const
    {
        return overlay_description_;
    }
    /// Whether the last request_attention() asked to flash.
    [[nodiscard]] bool flashing() const
    {
        return flashing_;
    }
    /// How many buttons the taskbar holds, fixed by the first set_thumbnail_buttons() call.
    [[nodiscard]] std::size_t thumbnail_button_count() const
    {
        return thumbnail_buttons_added_;
    }
    /// The minimum client size last set; zero on an axis is none.
    [[nodiscard]] const Size& min_size() const
    {
        return min_size_;
    }
    /// The maximum client size last set; zero on an axis is none.
    [[nodiscard]] const Size& max_size() const
    {
        return max_size_;
    }
    /// How many times close() was called.
    int close_requests = 0;
    /// How many times set_style() was called.
    int style_applications = 0;
    /// How many times activate() ran.
    int activations = 0;
    /// How many times wake() was called; atomic since wake() may come from any thread.
    std::atomic<int> wake_requests{0};

private:
    struct Request
    {
        bool        restore;
        WindowState state;
    };
    void apply(const Request& request);
    // Resizes `normal_bounds` to an outer size, clamping its client part to the size limits.
    void resize_outer(Size size);
    void apply_state(WindowState state);
    void apply_restore();
    // Where a restore from `state` goes, and the report that carries it to the Window.
    [[nodiscard]] WindowState restore_target(WindowState state) const;
    void                      report(WindowState state);

    Window& window_;

    std::vector<Request> pending_;
    // What a restore from Minimized goes back to, and whether leaving Fullscreen lands on
    // Maximized.
    WindowState before_minimize_           = WindowState::Normal;
    bool        fullscreen_from_maximized_ = false;

    std::string              title_;
    WindowStyle              style_{};
    ChromeOverrides          chrome_{};
    bool                     has_icon_ = false;
    std::vector<std::string> system_menu_texts_;
    std::vector<bool>        system_menu_checks_;
    std::vector<bool>        system_menu_enabled_;
    TaskbarProgress          taskbar_progress_{};
    bool                     has_overlay_ = false;
    std::string              overlay_description_;
    bool                     flashing_                = false;
    std::size_t              thumbnail_buttons_added_ = 0;
    std::vector<bool>        thumbnail_clickable_;
    Size                     min_size_{};
    Size                     max_size_{};

    // What the window holds: a WindowHost that hands every request to this object.
    std::unique_ptr<detail::HeadlessHostAdapter> adapter_;
    friend class HeadlessUI;
};

/// The UI host for tests, with no native window. It holds one Window with a HeadlessWindowHost
/// installed, laid out with a HeadlessTextMeasurer, so layout, focus, input and window-state
/// behavior run the same on any OS with deterministic numbers.
///
/// Drive it like a real host: set content, layout(), then feed input to window() and advance time
/// with tick(). Nothing is rendered; call Element::render() with a TestRenderer to inspect output.
///
/// Unless a test installed its own, the window comes with a TestClipboard, a TestLauncher, a
/// TestMessageBoxHost, a TestNotifier and a TestJumpListHost installed (set_clipboard(),
/// set_launcher(), set_message_box_host(), set_notifier(), set_jump_list_host()) for as long as
/// this HeadlessUI lives, so a copy, a link, a message box, a notification or a jump list never
/// reaches the OS. clipboard(), launcher(), message_boxes(), notifier() and jump_list() return
/// them.
///
/// Example:
/// @code
/// ui::testing::HeadlessUI ui = ui::testing::create_headless();
/// ui::Property<int> clicks{0};
/// ui::Control<ui::Button> save;
/// ui.window().set_content(ui::VerticalStack{.content = {
///     ui::Text{.text = ui::bind(clicks, [](int n) { return std::to_string(n); })},
///     ui::Button{.text = "Save", .on_click = [&] { ++clicks; }, .control = save},
/// }});
/// ui.layout({400, 300});
///
/// const ui::Point center = save->bounds().center();
/// ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Down});
/// ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Up});
/// REQUIRE(clicks.get() == 1);
/// @endcode
class HeadlessUI
{
public:
    /// The single logical window (created lazily), with a HeadlessWindowHost already installed.
    /// The host owns it; the reference is valid for the lifetime of this HeadlessUI.
    [[nodiscard]] Window& window();

    /// The fake window host (see HeadlessWindowHost).
    [[nodiscard]] HeadlessWindowHost& host();

    /// The TestClipboard this HeadlessUI installed with window(), or nullptr when a test had
    /// installed its own clipboard first.
    [[nodiscard]] TestClipboard* clipboard() const
    {
        return clipboard_.get();
    }
    /// The TestLauncher this HeadlessUI installed with window(), or nullptr when a test had
    /// installed its own launcher first.
    [[nodiscard]] TestLauncher* launcher() const
    {
        return launcher_.get();
    }
    /// The TestMessageBoxHost this HeadlessUI installed with window(), or nullptr when a test had
    /// installed its own host first.
    [[nodiscard]] TestMessageBoxHost* message_boxes() const
    {
        return message_boxes_.get();
    }
    /// The TestNotifier this HeadlessUI installed with window(), or nullptr when a test had
    /// installed its own notifier first.
    [[nodiscard]] TestNotifier* notifier() const
    {
        return notifier_.get();
    }
    /// The TestJumpListHost this HeadlessUI installed with window(), or nullptr when a test had
    /// installed its own host first.
    [[nodiscard]] TestJumpListHost* jump_list() const
    {
        return jump_list_.get();
    }

    /// Shows the window in the `initial` state, as ui::WindowOptions::initial_state does, and lays
    /// it out at `client` unless that state is Minimized.
    void show(Size client, WindowState initial = WindowState::Normal);

    /// The user changing the state from outside the app (see HeadlessWindowHost::
    /// simulate_state()), followed by the layout a real host runs at the window's new size.
    void simulate_state(WindowState state, Size client);

    /// Convenience factories. New tests should declare their tree instead, so they build through
    /// the API real callers use. `.control` gives them the same typed observers these factories
    /// return (see the class example).
    ///
    /// Each returns a freshly owned element. Ownership transfers to whatever the caller hands
    /// it to (StackControl::add / Window::set_content).
    [[nodiscard]] std::unique_ptr<ButtonControl> button(std::string text);
    /// A new TextControl showing `text`; see button().
    [[nodiscard]] std::unique_ptr<TextControl> text(std::string text);
    /// A new CheckBox; `checked` nullopt is the indeterminate state. See button().
    [[nodiscard]] std::unique_ptr<CheckBoxControl> check_box(std::string text, std::optional<bool> checked = false);
    /// A new RadioButton; see button().
    [[nodiscard]] std::unique_ptr<RadioButtonControl> radio_button(std::string text, bool checked = false);
    /// A new ToggleSwitch; see button().
    [[nodiscard]] std::unique_ptr<ToggleSwitchControl> toggle_switch(bool on = false);

    /// Builds a vertical stack, seeded with children in order:
    /// ui.vertical_stack(ui.text("Hello"), ui.button("Save")). Children are moved in.
    template <typename... Children>
    [[nodiscard]] std::unique_ptr<StackControl> vertical_stack(Children... children)
    {
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        (stack->add(std::move(children)), ...);
        return stack;
    }

    /// Lays out the window at a client size (DIPs) using the headless measurer, as a real host
    /// does when the window is resized.
    void layout(Size client);

    /// Advances elapsed time by `delta` through Window::tick(). Time moves deterministically,
    /// with no real clock, for things like a ToolTip's hover delay. Returns whether the tree
    /// became dirty and needs a repaint, as Window::tick() does.
    bool tick(Duration delta)
    {
        return window().tick(delta);
    }

    /// The HeadlessTextMeasurer layout() uses, for measuring or laying out elements outside the
    /// window the same way.
    [[nodiscard]] const TextMeasurer& measurer() const
    {
        return measurer_;
    }

private:
    std::unique_ptr<TestClipboard>      clipboard_; // uninstall themselves when destroyed
    std::unique_ptr<TestLauncher>       launcher_;
    std::unique_ptr<TestMessageBoxHost> message_boxes_;
    std::unique_ptr<TestNotifier>       notifier_;
    std::unique_ptr<TestJumpListHost>   jump_list_;
    std::unique_ptr<Window>             window_;
    std::unique_ptr<HeadlessWindowHost> host_; // destroyed before window_, which it refers to
    HeadlessTextMeasurer                measurer_;
};

/// A fresh HeadlessUI, the same as `HeadlessUI{}`. It reads better at the top of a test.
[[nodiscard]] HeadlessUI create_headless();

} // namespace ui::testing
