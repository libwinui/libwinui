// SPDX-License-Identifier: MIT
#pragma once

// Structure that follows state. Nothing here diffs a tree: each tool either holds the change it
// was told about, or (a keyed ForEach over a whole vector) compares keys.
//
//   Situation                          Tool                                  Keeps state
//   Decided once, at build time        ui::when() / a plain `if`             n/a
//   Show or hide, cheap subtree        `.visible = ui::bind(...)`            yes
//   Show or hide, expensive subtree    ui::Lazy                              yes
//   One of N, state should reset       ui::Match                             no
//   One of N, state should persist     ui::Match{.keep = true}               yes
//   Dynamic list, you make the edits   ui::ForEach + ui::ObservableVector    yes, per item
//   Dynamic list, you assign a vector  ui::ForEach{.items, .key}             yes, per key
//   Long list                          ui::ForEach{.virtualize}, the views   per row in view
//   State local to a component         ui::State<S>                          for the node's life
//
// ui::ForEach sits beside its control in ui/collections/ForEach.hpp.

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/panels/LayoutPanel.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

namespace detail
{
// The node Match and Lazy build. They swap the child when their property changes, never while
// measuring, so measuring is the child's alone and may run on a layout worker.
class DynamicHost final : public ContentHost
{
public:
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }
};
} // namespace detail

/// Structure driven by a value: one node showing the subtree that belongs to the current value.
/// Builds a ui::ContentHost.
///
/// `.value` is what to look at, usually `ui::bind(prop)`, and `.content` turns it into the
/// subtree to show. The subtree is built once at construction and again on every change of the
/// value. The Property's equality gate drops writes that change nothing, so a redundant assignment
/// costs nothing. Only this node's subtree is rebuilt, so the cost grows with the subtree, not
/// with the screen.
///
/// By default a change destroys the old subtree, so what the user typed or scrolled in it is
/// gone and handles published from inside it through `.control` dangle afterwards. That is right
/// for pages whose state should reset. `.keep = true` keeps every subtree it has built and only
/// hides the ones not current, so tab pages and master/detail panes come back as they were left.
///
/// Name the value's type, `ui::Match<Page>{.value = page, ...}`: C++ deduces a spec's type only
/// from the field's own type, which a Property is not.
///
/// LIFETIME: `.content` is stored on the built node and invoked for the life of the tree, so it
/// must not capture anything shorter-lived than the window. Capturing a reference to model
/// state the caller owns is the intended shape. A bound Property that dies first stops the
/// rebuilds.
///
/// Example:
/// @code
/// enum class Page { Home, Settings };
/// ui::Property<Page> page{Page::Home};
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::HorizontalStack{.spacing = 8, .content = {
///         ui::Button{.text = "Home",     .on_click = [&] { page = Page::Home; }},
///         ui::Button{.text = "Settings", .on_click = [&] { page = Page::Settings; }},
///     }},
///     ui::Match<Page>{
///         .value   = page,
///         .keep    = true, // the check box is still ticked after a visit to Home
///         .content = [](const Page& p) -> ui::Spec
///         {
///             switch (p)
///             {
///             case Page::Home:     return ui::Text{.text = "Welcome home"};
///             case Page::Settings: return ui::CheckBox{.text = "Dark mode"};
///             }
///             return {};
///         },
///     },
/// }});
/// @endcode
template <typename T>
struct Match
{
    /// The control this spec builds; what `ui::Control<Match>` points at.
    using control_type = ContentHost;

    // ---- properties ----
    /// The value to match on: a plain value (built once) or `ui::bind(prop)`, which shows the
    /// content for the new value whenever the property changes.
    Binding<T> value{};


    /// false: a change destroys the old subtree and builds the new one. true: each value's
    /// subtree is built the first time the value is seen and kept until the Match goes, hidden
    /// while another one shows, so it keeps its state. One subtree is kept per distinct value,
    /// which suits a handful of pages; for a value with no bound on how many there are, use a
    /// ui::ForEach or a view instead.
    bool keep = false;

    // ---- appearance / layout ----
    /// Transitions for a change of content: with a ui::ContentTransition the new
    /// subtree slides and fades in while the old one fades out. Not used with `keep`.
    Binding<TransitionList>           content_transitions{};
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    /// Out: the handle that receives the built control. Named through std::type_identity_t so that
    /// passing a handle does not take part in deducing the type argument.
    ControlOut<std::type_identity_t<Match>> control{};
    /// Produces the subtree for a given value. Returning an empty Spec is legal and means
    /// "show nothing". Its parameter names T through std::type_identity_t so that `.value`
    /// alone decides T when the type argument is left out.
    Callback<Spec(const std::type_identity_t<T>&)> content{};

    /// Consumes the spec and returns the built ui::ContentHost, the subtree for the current value
    /// already built; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&
    {
        auto         node = std::make_unique<detail::DynamicHost>();
        ContentHost* raw  = node.get();
        if (content_transitions.reactive() || !content_transitions.initial().empty())
        {
            ui::apply(content_transitions, *node, [raw](const TransitionList& v) { raw->set_children_transitions(v); });
        }

        // Either sink owns the factory and moves into the subscription, which the node owns, so
        // the factory lives exactly as long as the node it builds for.
        if (content && keep)
        {
            // A panel with no layout: its children overlap, each given the whole box, and it is as
            // large as the largest one showing. All but the current page are collapsed. Each page
            // sits in a host of its own, so its visibility here never meets a `.visible` binding
            // the page's own root may have.
            LayoutPanelControl* pages = raw->set_child(std::make_unique<LayoutPanelControl>());

            struct Page
            {
                T            value;
                ContentHost* host;
            };
            apply(value,
                  *node,
                  [pages, seen = std::vector<Page>{}, factory = std::move(content)](const T& current) mutable
                  {
                      ContentHost* wanted = nullptr;
                      for (const Page& page : seen)
                      {
                          if (page.value == current)
                          {
                              wanted = page.host;
                          }
                      }
                      if (wanted == nullptr)
                      {
                          Spec built = factory(current);
                          wanted     = pages->add(std::make_unique<detail::DynamicHost>());
                          wanted->set_child(std::move(built).build());
                          seen.push_back(Page{current, wanted});
                      }
                      for (const Page& page : seen)
                      {
                          page.host->set_visibility(page.host == wanted ? Visibility::Visible : Visibility::Collapsed);
                      }
                  });
        }
        else if (content)
        {
            apply(value,
                  *node,
                  [raw, factory = std::move(content)](const T& current) mutable
                  {
                      Spec built = factory(current);
                      raw->set_child(std::move(built).build());
                  });
        }

        apply_common(*this, *node);
        publish_control(control, raw);
        return node;
    }
};

/// A subtree that is built the first time it is shown. Builds a ui::ContentHost.
///
/// `.visible` on any spec hides a subtree that already exists. Lazy is the same thing for a
/// subtree that is expensive to build and that most users never open: `.content` is a function,
/// and it runs the first time `.visible` is true. After that the subtree is hidden and shown
/// again without being rebuilt, so it keeps its state. While hidden the Lazy takes no room.
///
/// `.unload = true` destroys the subtree whenever `.visible` turns false and builds it again on
/// the next true: the memory is given back, and the state is lost.
///
/// LIFETIME: `.content` is stored on the built node, so it must not capture anything
/// shorter-lived than the window.
///
/// Example:
/// @code
/// ui::Spec log_view()
/// {
///     return ui::Text{.text = "Started."};
/// }
///
/// ui::Property<bool> show_log{false};
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::ToggleSwitch{.header = "Show the log", .checked = show_log},
///     ui::Lazy{
///         .visible = show_log,
///         .content = []() -> ui::Spec { return log_view(); }, // not built until switched on
///     },
/// }});
/// @endcode
struct Lazy
{
    /// The control this spec builds; what `ui::Control<ui::Lazy>` points at.
    using control_type = ContentHost;

    // ---- properties ----
    /// Whether the subtree shows. The first true builds it. This is the `.visible` every spec
    /// has, moved to the front because here it also decides when `.content` runs.
    Binding<bool> visible{true};

    /// true: destroy the subtree when `visible` turns false, instead of hiding it.
    bool unload = false;

    // ---- appearance / layout ----
    /// Transitions for the content: with a ui::ContentTransition it slides and fades
    /// in when it is built and, with `unload`, fades out when it goes.
    Binding<TransitionList>           content_transitions{};
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    /// Out: the handle that receives the built control.
    ControlOut<Lazy> control{};
    /// Builds the subtree: once, the first time `visible` is true (with `unload`, each time it
    /// turns true). Never called while `visible` stays false.
    Callback<Spec()> content{};

    /// Consumes the spec and returns the built ui::ContentHost, empty until `visible` is true;
    /// the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

namespace detail
{
// The node a ui::State builds: a ContentHost that owns the state. The state is a base listed
// first, so it is destroyed after the subtree whose handlers refer to it.
template <typename S>
struct StateStorage
{
    S state;
};

template <typename S>
class StateHost final : private StateStorage<S>, public ContentHost
{
public:
    explicit StateHost(Callback<S()>& initial) : StateStorage<S>{make(initial)}
    {
    }

    [[nodiscard]] S& state()
    {
        return StateStorage<S>::state;
    }

    // Measuring reads only the child; the state is the handlers' business.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

private:
    // A function, so that a state that cannot be moved is constructed in place.
    static S make(Callback<S()>& initial)
    {
        if constexpr (std::default_initializable<S>)
        {
            if (!initial)
            {
                return S{};
            }
        }
        assert(initial && "State: a state with no default constructor needs .initial");
        return initial();
    }
};
} // namespace detail

/// State that belongs to a piece of UI instead of to the app. Builds a ui::ContentHost that owns
/// an `S` and shows what `.content` makes of it.
///
/// A component that needs a private flag, a text being edited or a small struct of Properties has
/// nowhere to keep it: a local variable is gone when the function returns, and the handlers that
/// captured it dangle. State gives it a home in the tree. The node constructs one `S`, passes it
/// to `.content` by reference, once, and destroys it after the subtree. So a handler or a binding
/// inside the subtree may capture that reference: it lives exactly as long as they do.
///
/// `.content` runs once. It is not run again when the state changes; what changes on screen is
/// what was bound to the state. `S` is usually a ui::Property or a struct of them. State shared
/// between components still lives in the caller and is passed in as `Property<T>&`.
///
/// `.initial` returns the starting state. It is a function because a Property cannot be moved;
/// left out, the state is `S{}`.
///
/// Example:
/// @code
/// // A disclosure row whose "open" flag belongs to the row, not to the app.
/// ui::Spec disclosure(std::string title, ui::Callback<ui::Spec()> body)
/// {
///     return ui::State<ui::Property<bool>>{
///         .content = [title = std::move(title), body = std::move(body)](ui::Property<bool>& open) mutable -> ui::Spec
///         {
///             return ui::VerticalStack{.content = {
///                 ui::Button{.text = std::move(title), .on_click = [&open] { open.toggle(); }},
///                 ui::Lazy{.visible = open, .content = std::move(body)},
///             }};
///         },
///     };
/// }
/// @endcode
template <typename S>
struct State
{
    /// The control this spec builds; what `ui::Control<State>` points at.
    using control_type = ContentHost;

    // ---- properties ----
    /// Makes the starting state, constructed in place in the node; `S{}` when left out.
    Callback<S()> initial{};


    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    /// Out: the handle that receives the built control.
    ControlOut<State> control{};
    /// Builds the subtree from the state, once. The reference stays valid for as long as the
    /// subtree lives, so handlers and bindings inside may capture it.
    Callback<Spec(S&)> content{};

    /// Consumes the spec and returns the built node, a ui::ContentHost owning the state and the
    /// subtree made from it; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&
    {
        auto node = std::make_unique<detail::StateHost<S>>(initial);
        if (content)
        {
            Spec built = content(node->state());
            node->set_child(std::move(built).build());
        }

        apply_common(*this, *node);
        publish_control(control, static_cast<ContentHost*>(node.get()));
        return node;
    }
};

} // namespace ui
