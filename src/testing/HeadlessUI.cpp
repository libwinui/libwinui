// SPDX-License-Identifier: MIT
#include "winui/testing/HeadlessUI.hpp"

#include "../ui/WindowHost.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/text/Text.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui::detail
{
// The WindowHost the window holds: every request goes to the HeadlessWindowHost a test reads.
class HeadlessHostAdapter final : public WindowHost
{
public:
    explicit HeadlessHostAdapter(testing::HeadlessWindowHost& host) : host_(host)
    {
    }

    void request_state(WindowState state) override
    {
        host_.request_state(state);
    }
    void restore() override
    {
        host_.restore();
    }
    void close() override
    {
        host_.close();
    }
    void wake() override
    {
        host_.wake();
    }
    void set_title(std::string_view title) override
    {
        host_.set_title(title);
    }
    void set_style(const WindowStyle& style) override
    {
        host_.set_style(style);
    }
    void set_chrome(const ChromeOverrides& chrome) override
    {
        host_.set_chrome(chrome);
    }
    [[nodiscard]] std::optional<WindowPlacement> placement() const override
    {
        return host_.placement();
    }
    void set_icon(const WindowIcon* icon) override
    {
        host_.set_icon(icon);
    }
    void set_system_menu(const std::vector<MenuItem>& items) override
    {
        host_.set_system_menu(items);
    }
    void set_taskbar_progress(const TaskbarProgress& progress) override
    {
        host_.set_taskbar_progress(progress);
    }
    void set_taskbar_overlay(const WindowIcon* icon, std::string_view description) override
    {
        host_.set_taskbar_overlay(icon, description);
    }
    void request_attention(bool flash) override
    {
        host_.request_attention(flash);
    }
    void set_thumbnail_buttons(const std::vector<ThumbnailButton>& buttons) override
    {
        host_.set_thumbnail_buttons(buttons);
    }
    void move(Point position) override
    {
        host_.move(position);
    }
    void resize(Size size) override
    {
        host_.resize(size);
    }
    [[nodiscard]] std::optional<Rect> bounds() const override
    {
        return host_.bounds();
    }
    [[nodiscard]] std::optional<Rect> work_area() const override
    {
        return host_.work_area();
    }
    void set_visible(bool visible, bool activate) override
    {
        host_.set_visible(visible, activate);
    }
    void activate() override
    {
        host_.activate();
    }
    void set_size_limits(Size min, Size max) override
    {
        host_.set_size_limits(min, max);
    }

private:
    testing::HeadlessWindowHost& host_;
};
} // namespace ui::detail

namespace ui::testing
{

// ---- HeadlessWindowHost ----


HeadlessWindowHost::HeadlessWindowHost(Window& window) :
    window_(window),
    adapter_(std::make_unique<detail::HeadlessHostAdapter>(*this))
{
}

HeadlessWindowHost::~HeadlessWindowHost() = default;


void HeadlessWindowHost::request_state(WindowState state)
{
    const Request request{false, state};
    if (defer_requests)
    {
        pending_.push_back(request);
        return;
    }
    apply(request);
}

void HeadlessWindowHost::restore()
{
    const Request request{true, WindowState::Normal};
    if (defer_requests)
    {
        pending_.push_back(request);
        return;
    }
    apply(request);
}

void HeadlessWindowHost::apply_pending_requests()
{
    // Swapped out first: a state observer is allowed to make another request while this runs.
    std::vector<Request> pending;
    pending.swap(pending_);
    for (const Request& request : pending)
    {
        apply(request);
    }
}

void HeadlessWindowHost::simulate_state(WindowState state)
{
    apply_state(state);
}

void HeadlessWindowHost::apply(const Request& request)
{
    if (request.restore)
    {
        apply_restore();
    }
    else
    {
        apply_state(request.state);
    }
}

void HeadlessWindowHost::apply_state(WindowState state)
{
    const WindowState current = window_.state().get();
    if (state == current)
    {
        return;
    }
    switch (state)
    {
        case WindowState::Minimized:
            // Minimizing from full screen keeps full screen as the way back, as a real host's
            // WS_POPUP style staying in place while iconic does.
            before_minimize_ = current;
            break;
        case WindowState::Fullscreen:
            fullscreen_from_maximized_ = current == WindowState::Minimized ? before_minimize_ == WindowState::Maximized
                                                                           : current == WindowState::Maximized;
            break;
        case WindowState::Normal:
        case WindowState::Maximized:
            break;
    }
    report(state);
}

void HeadlessWindowHost::apply_restore()
{
    if (window_.state().get() != WindowState::Normal)
    {
        report(restore_target(window_.state().get()));
    }
}

// The real host gets what before_minimize_ and fullscreen_from_maximized_ hold from WINDOWPLACEMENT
// and its own fullscreen flag, and keeps normal_bounds as WINDOWPLACEMENT's rcNormalPosition.
WindowState HeadlessWindowHost::restore_target(WindowState state) const
{
    switch (state)
    {
        case WindowState::Minimized:
            return before_minimize_;
        case WindowState::Fullscreen:
            return fullscreen_from_maximized_ ? WindowState::Maximized : WindowState::Normal;
        case WindowState::Normal:
        case WindowState::Maximized:
            break;
    }
    return WindowState::Normal;
}

void HeadlessWindowHost::report(WindowState state)
{
    ui::testing::seams(window_).report_state(state, restore_target(state));
}

std::optional<WindowPlacement> HeadlessWindowHost::placement() const
{
    const WindowState state = window_.state().get();
    WindowPlacement   placement{};
    placement.normal_bounds        = normal_bounds;
    placement.state                = state;
    placement.restore_to_maximized = restore_target(state) == WindowState::Maximized;
    return placement;
}

void HeadlessWindowHost::set_system_menu(const std::vector<MenuItem>& items)
{
    system_menu_texts_.clear();
    system_menu_checks_.clear();
    system_menu_enabled_.clear();
    for (const MenuItem& item : items)
    {
        system_menu_texts_.push_back(item.kind == MenuItemKind::Separator ? std::string{} : item.display_text());
        system_menu_checks_.push_back(item.is_checked());
        system_menu_enabled_.push_back(item.is_enabled());
    }
}

bool HeadlessWindowHost::click_system_menu_item(std::size_t index)
{
    // A separator (empty text) and a disabled entry can't be picked from a real menu either.
    if (index >= system_menu_texts_.size() || system_menu_texts_[index].empty() || !system_menu_enabled_[index])
    {
        return false;
    }
    ui::testing::seams(window_).handle_system_menu_item(index);
    return true;
}

void HeadlessWindowHost::resize_outer(Size size)
{
    // The limits are client sizes (WM_GETMINMAXINFO's reading of them adds the frame back on).
    const auto clamp = [](float client, float min, float max)
    {
        if (max > 0.0f)
        {
            client = std::min(client, max);
        }
        return min > 0.0f ? std::max(client, min) : client;
    };
    const float client_w = clamp(size.width - frame.horizontal(), min_size_.width, max_size_.width);
    const float client_h = clamp(size.height - frame.vertical(), min_size_.height, max_size_.height);
    normal_bounds.width  = std::max(0.0f, client_w) + frame.horizontal();
    normal_bounds.height = std::max(0.0f, client_h) + frame.vertical();
}

void HeadlessWindowHost::set_thumbnail_buttons(const std::vector<ThumbnailButton>& buttons)
{
    // ThumbBarAddButtons fixes the set once; later calls only update the buttons it added.
    if (thumbnail_buttons_added_ == 0)
    {
        thumbnail_buttons_added_ = std::min<std::size_t>(buttons.size(), 7);
    }
    thumbnail_clickable_.assign(thumbnail_buttons_added_, false);
    for (std::size_t i = 0; i < thumbnail_buttons_added_ && i < buttons.size(); ++i)
    {
        thumbnail_clickable_[i] = buttons[i].is_enabled() && buttons[i].visible.initial();
    }
}

bool HeadlessWindowHost::click_thumbnail_button(std::size_t index)
{
    if (index >= thumbnail_clickable_.size() || !thumbnail_clickable_[index])
    {
        return false;
    }
    ui::testing::seams(window_).handle_thumbnail_button(index);
    return true;
}

// ---- HeadlessUI ----

Window& HeadlessUI::window()
{
    if (!window_)
    {
        if (ui::detail::installed_clipboard() == nullptr)
        {
            clipboard_ = std::make_unique<TestClipboard>();
            set_clipboard(clipboard_.get());
        }
        if (ui::detail::installed_launcher() == nullptr)
        {
            launcher_ = std::make_unique<TestLauncher>();
            set_launcher(launcher_.get());
        }
        if (ui::detail::installed_message_box_host() == nullptr)
        {
            message_boxes_ = std::make_unique<TestMessageBoxHost>();
            set_message_box_host(message_boxes_.get());
        }
        if (ui::detail::installed_notifier() == nullptr)
        {
            notifier_ = std::make_unique<TestNotifier>();
            set_notifier(notifier_.get());
        }
        if (ui::detail::installed_jump_list_host() == nullptr)
        {
            jump_list_ = std::make_unique<TestJumpListHost>();
            set_jump_list_host(jump_list_.get());
        }
        window_ = std::make_unique<Window>();
        host_   = std::make_unique<HeadlessWindowHost>(*window_);
        testing::seams(*window_).set_window_host(host_->adapter_.get());
        testing::seams(*window_).handle_opened();
        // Stands for a window on screen and in the foreground, as a real host reports one it showed.
        testing::seams(*window_).report_visible(true);
        testing::seams(*window_).report_active(true);
    }
    return *window_;
}

HeadlessWindowHost& HeadlessUI::host()
{
    (void)window();
    return *host_;
}

void HeadlessUI::show(Size client, WindowState initial)
{
    host().simulate_state(initial);
    layout(client);
}

void HeadlessUI::simulate_state(WindowState state, Size client)
{
    host().simulate_state(state);
    layout(client);
}

std::unique_ptr<ButtonControl> HeadlessUI::button(std::string text)
{
    return std::make_unique<ButtonControl>(std::move(text));
}

std::unique_ptr<TextControl> HeadlessUI::text(std::string text)
{
    return std::make_unique<TextControl>(std::move(text));
}

std::unique_ptr<CheckBoxControl> HeadlessUI::check_box(std::string text, std::optional<bool> checked)
{
    return std::make_unique<CheckBoxControl>(std::move(text), checked);
}

std::unique_ptr<RadioButtonControl> HeadlessUI::radio_button(std::string text, bool checked)
{
    return std::make_unique<RadioButtonControl>(std::move(text), checked);
}

std::unique_ptr<ToggleSwitchControl> HeadlessUI::toggle_switch(bool on)
{
    return std::make_unique<ToggleSwitchControl>(on);
}

void HeadlessUI::layout(Size client)
{
    window().layout(client, measurer_);
}

HeadlessUI create_headless()
{
    return HeadlessUI{};
}

} // namespace ui::testing
