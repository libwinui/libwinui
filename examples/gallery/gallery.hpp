// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/scrolling/PullToRefresh.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <winui/winui.hpp>

#include <concepts>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cstddef>

namespace gallery
{
// State that outlives every page. Page factories are plain function pointers, so their
// handlers capture references into this object, which lives in main().
struct Context
{
    // For pages whose cards have no output box of their own.
    ui::Property<std::string>& status;

    ui::Window& window;

    // The theme the header last asked for. New sample windows open in it.
    const ui::ThemeRequest& theme_request;

    // Go to another page by its navigation name, for "see the X sample" links.
    std::function<void(std::string_view)> navigate;
};

using PageFactory = ui::Spec (*)(Context&);

// What the gallery does when a later start hands over its command line: shows the arguments in
// the status strip and brings the window forward. Both outlive the gallery's pages.
ui::Callback<void(ui::InstanceActivation)> report_handover(ui::Property<std::string>& status, ui::Window& window);

struct Page
{
    const char* name;
    const char* description;
    PageFactory make;
};

// A navigation group and its pages, libwinui-only controls at the end.
struct Group
{
    const char*           title;
    std::span<const Page> pages;
};

std::span<const Group> groups();

// The icon of a page or a group, by its name; empty for a name without one.
ui::IconSource page_icon(std::string_view page);
ui::IconSource group_icon(std::string_view group);

// The paragraph under a page's description (page_about.cpp): what else is worth knowing about
// the control, and what to try. Null for a page without one.
const char* page_about(std::string_view page);

// A page the search found, and the group it sits in.
struct SearchHit
{
    const Page*  page;
    const Group* group;
};

// The title bar's search, in navigation order: every space-separated word of `query` must occur,
// ignoring case, in the page's name, its description or its group's title. A blank query finds
// nothing.
std::vector<SearchHit> search_pages(std::string_view query);

// The same hits ordered for the search box's suggestion list: names that start with the whole
// query first, then alphabetical.
std::vector<SearchHit> search_suggestions(std::string_view query);

// What a submitted search shows: the hits for `query`, filtered by group, each a card that goes
// to its page through Context::navigate.
ui::Spec search_results_page(std::string query, Context& ctx);

// One example card.
struct Example
{
    std::string title;

    // The code that makes this card's control, shown under it in a "Source code" expander. A raw
    // string literal, indented like the code around it: the common indentation and the blank
    // lines at both ends are taken off. Trimmed to what the card is about: the spec and the
    // state it binds to, not the page's plumbing.
    std::string code{};

    bool stacked = false;

    // The output's horizontal alignment: false keeps a single control at its own
    // width (the Left default), true stretches it across the card.
    bool stretch = false;

    ui::Spec output{};

    ui::Spec options{};

    ui::SpecList controls{};
};

ui::Spec example(Example card);

// The older card shape without output or options.
ui::Spec example(std::string caption, ui::SpecList controls, bool stacked = false, std::string code = {});

ui::Spec examples(ui::SpecList cards);

// Whether `t` carries the Fluent token table (Theme::palette): Fluent 1 and 2 outside high
// contrast. Every other theme leaves it default, each color opaque black, so a page reading a
// Fluent token falls back to one every theme sets.
inline bool has_fluent_palette(const ui::Theme& t)
{
    return (t.variant == ui::ThemeVariant::Fluent2 || t.variant == ui::ThemeVariant::Fluent) && !t.high_contrast;
}

// Holds a page's state exactly as long as the page is on screen. The state is destroyed
// after the subtree, so no control built from it ever holds a dangling reference.
template <typename State>
class StateHost;

template <typename State, typename Body>
    requires std::default_initializable<State> && std::invocable<Body&, State&>
ui::Spec with_state(Body body);

// A filled rectangle for cards whose output is a color. Transparent until bound.
struct Swatch
{
    ui::Binding<ui::Color> fill{ui::colors::Transparent};
    ui::Binding<ui::Color> border{ui::colors::Transparent};
    float                  border_thickness = 0.0f;
    float                  width            = 0.0f; // 0 stretches
    float                  height           = 32.0f;

    [[nodiscard]] std::unique_ptr<ui::Element> build() &&;
};

// The content pane's background: one elevation step above the nav pane, top-left corner
// rounded.
std::unique_ptr<ui::Element> content_surface(ui::Spec content);

// A Border filled with theme resources, re-read every frame so a theme switch repaints it.
// ui::BorderControl takes plain colors and cannot follow the theme.
enum class BoxStyle
{
    Card,    ///< card_background_secondary, card_stroke_default
    Surface, ///< card_background_default, card_stroke_default, square corners
    Control, ///< control_fill_default, control_stroke_default
    Accent,  ///< accent_fill_default, control_stroke_default
    Frame,   ///< no fill, card_stroke_default
    Subtle,  ///< subtle_fill_tertiary, no stroke
};
std::unique_ptr<ui::Element> themed_box(BoxStyle style, ui::Thickness padding, ui::Spec content);

// A popup-colored card for a Popup's child.
std::unique_ptr<ui::Element> overlay_card(ui::Spec content, ui::Thickness padding);

// A page timer. A zero-size element in the page, so it never fires after the
// page is gone.
class Timer final : public ui::Element
{
public:
    void start(double ms, ui::Callback<void()> action)
    {
        remaining_ = ms;
        action_    = std::move(action);
    }
    void stop()
    {
        remaining_ = -1.0;
    }
    void every_tick(ui::Callback<void()> action)
    {
        each_tick_ = std::move(action);
    }

protected:
    [[nodiscard]] ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return ui::Size{};
    }
    void render_override(ui::Renderer&, const ui::RenderContext&) const override
    {
    }
    void tick_override(ui::Duration delta) override
    {
        if (each_tick_)
        {
            each_tick_();
        }
        if (remaining_ < 0.0)
        {
            return;
        }
        remaining_ -= delta.count();
        if (remaining_ < 0.0 && action_)
        {
            remaining_ = -1.0; // one shot; the action may start it again
            action_();
        }
    }


private:
    double               remaining_ = -1.0;
    ui::Callback<void()> action_;
    ui::Callback<void()> each_tick_;
};

// Procedurally generated stand-ins, since libwinui ships only originally authored assets.
// Function-local statics, so pages may use them across rebuilds.
const ui::Bitmap&             placeholder_photo();
const ui::AnimatedIconSource& pulse_visual();

// Procedural 400x266 landscapes standing in for sample photos.
// Any index is valid, it wraps.
const ui::Bitmap& sample_photo(std::size_t index);

// The collection samples' data item, with repeatable counts.
struct CustomDataObject
{
    std::string title;
    std::size_t image = 0;
    std::string views;
    std::string likes;
    std::string description;
};
std::vector<CustomDataObject> data_objects(bool include_all_items = false);

// The six sample pages, which the Navigation and Motion frames navigate between.
// `parts` receives the elements a connected animation animates.
struct SamplePageParts
{
    ui::Element*                   accent = nullptr;
    ui::Control<ui::VerticalStack> content_panel;
};
ui::Spec navigation_sample_page(int page, SamplePageParts* parts = nullptr);

// ---- the per-group page tables, one .cpp each, in navigation order ----
std::span<const Page> menus_and_toolbars_pages();
std::span<const Page> collections_pages();
std::span<const Page> basic_input_pages();
std::span<const Page> date_and_time_pages();
std::span<const Page> status_and_info_pages();
std::span<const Page> dialogs_and_flyouts_pages();
std::span<const Page> scrolling_pages();
std::span<const Page> layout_pages();
std::span<const Page> navigation_pages();
std::span<const Page> media_pages();
std::span<const Page> styles_pages();
std::span<const Page> text_pages();
std::span<const Page> motion_pages();
std::span<const Page> windowing_pages();
std::span<const Page> native_pages();
std::span<const Page> system_pages();

// ---- with_state ----

template <typename State>
struct StateSlot
{
    std::unique_ptr<State> state = std::make_unique<State>();
};

// StateSlot is the first base so it is destroyed last, after ContentHost's subtree. A member
// would die before the bases.
template <typename State>
class StateHost final : private StateSlot<State>, public ui::ContentHost
{
public:
    State& state()
    {
        return *this->StateSlot<State>::state;
    }
};

template <typename State, typename Body>
    requires std::default_initializable<State> && std::invocable<Body&, State&>
ui::Spec with_state(Body body)
{
    auto     host = std::make_unique<StateHost<State>>();
    ui::Spec page = body(host->state());
    host->set_child(std::move(page).build());
    return host;
}

} // namespace gallery
