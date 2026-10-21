// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/ui/Element.hpp"

#include <memory>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

template <typename S>
class Control;
template <typename S>
class ControlLink;

/// The type of every spec's `.control` field: where build() publishes the control it made.
///
/// It converts from a `ui::Control<S>` handle, so a spec is written `.control = save`, and it only
/// remembers where that handle is; the handle itself must outlive the build. App code never names
/// this type: a component function that passes a handle on to the spec it returns takes a
/// `ui::Control<S>&`, or a `ui::Control<S>*` defaulted to `nullptr` when the handle is optional,
/// and both convert. A custom spec declares its `.control` field as a `ControlOut` of itself.
///
/// Example:
/// @code
/// ui::Spec save_button(ui::Property<bool>& dirty, ui::Control<ui::Button>* control = nullptr)
/// {
///     return ui::Button{.text = "Save", .enabled = dirty, .control = control};
/// }
///
/// ui::Property<bool>      dirty{false};
/// ui::Control<ui::Button> save;
/// window.set_content(save_button(dirty, &save)); // `save` reaches the built button
/// @endcode
template <typename S>
class ControlOut
{
public:
    /// No handle: build() publishes nothing.
    ControlOut() = default;

    /// Publish into `handle`.
    ControlOut(Control<S>& handle) : handle_(&handle) // NOLINT(google-explicit-constructor)
    {
    }

    /// Publish into `*handle`, or nowhere when it is null: for a handle that is only wanted
    /// sometimes (`.control = first ? &handle : nullptr`).
    ControlOut(Control<S>* handle) : handle_(handle) // NOLINT(google-explicit-constructor)
    {
    }

    /// Stores `node` in the handle, if there is one. Called last in a spec's build(), through
    /// ui::publish_control().
    template <typename C>
    void publish(C* node) const
    {
        if (handle_ != nullptr)
        {
            handle_->watch_.reset(node);
            handle_->filled();
        }
    }

private:
    Control<S>* handle_ = nullptr;
};

/// A typed, non-owning handle to the control a spec built.
///
/// `S` is the spec type; the control's type comes from the spec's `using control_type = ...;` line,
/// so `ui::Control<ui::Button>` points at a `ui::ButtonControl`. Declare one, hand it to the
/// spec's `.control` field, and after the tree is built it reaches the live control. It changes
/// no ownership: the tree owns the control, and the handle is empty until build() has run, empty
/// again once the control is destroyed (it watches the control). That includes a control built
/// inside a subtree that was then thrown away, such as the children of a spec passed to a false
/// ui::when(). Most controls need none; name only what you address later.
///
/// It is typed, so handing a `ui::Control<ui::TextBox>` to a `ui::Button` is a compile error on
/// that line. Where an API wants the control's pointer (`Element*`, `ButtonControl*`), the handle
/// converts to it.
///
/// Example:
/// @code
/// ui::Control<ui::Button> save;
/// ui::Control<ui::TextBox> name;
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::TextBox{.placeholder_text = "Name", .control = name},
///     ui::Button{.text = "Save", .on_click = [&] { save->set_text("Saved " + name->text()); }, .control = save},
/// }});
///
/// save->set_enabled(false); // the tree is built: the handles are live
/// @endcode
template <typename S>
class Control
{
public:
    /// The control class `S` builds.
    using Type = typename S::control_type;

    /// An empty handle; a spec's build() fills it.
    Control() = default;

    /// Another handle to the same control.
    Control(const Control& other) : watch_(other.watch_.get())
    {
    }
    /// Points at the same control as `other`.
    Control& operator=(const Control& other)
    {
        watch_.reset(other.watch_.get());
        return *this;
    }

    /// The control, or nullptr before the spec was built or after the control was destroyed.
    [[nodiscard]] Type* get() const
    {
        return static_cast<Type*>(watch_.get());
    }
    /// Member access on the control. The handle must not be empty.
    [[nodiscard]] Type* operator->() const
    {
        return get();
    }
    /// The control itself. The handle must not be empty.
    [[nodiscard]] Type& operator*() const
    {
        return *get();
    }
    /// True while the built control is alive.
    [[nodiscard]] explicit operator bool() const
    {
        return watch_.get() != nullptr;
    }
    /// The control's pointer, for an API that takes one.
    operator Type*() const // NOLINT(google-explicit-constructor)
    {
        return get();
    }

    /// Forgets the control.
    Control& operator=(std::nullptr_t)
    {
        watch_.reset(nullptr);
        return *this;
    }

private:
    friend class ControlOut<S>;
    friend class ControlLink<S>;

    // Runs every link waiting on this handle (ControlLink::resolve()); a link whose owner is gone
    // is dropped.
    void filled()
    {
        std::erase_if(links_, [this](Callback<bool(Type*)>& link) { return !link(get()); });
    }

    ElementWatch                       watch_;
    std::vector<Callback<bool(Type*)>> links_; // not copied: a link belongs to the handle it was made on
};

/// The type of a spec field that names another control by its handle: InkToolbar's `.target`,
/// TeachingTip's `.target`, RichText's `.overflow`. Written `.target = canvas` with a
/// `ui::Control<ui::InkCanvas>`, it links the two controls in either build order: at once when
/// the target is already built, or as soon as the target's spec fills the handle. When the target
/// is destroyed the control lets go of it, and a later build that fills the handle again links the
/// new one. The handle must outlive the build, as for `.control`.
///
/// Example:
/// @code
/// ui::Control<ui::InkCanvas> canvas;
/// window.set_content(ui::VerticalStack{.content = {
///     ui::InkToolbar{.target = canvas}, // built first, linked once the canvas is built
///     ui::InkCanvas{.height = 300, .control = canvas},
/// }});
/// @endcode
template <typename S>
class ControlLink
{
public:
    /// No link.
    ControlLink() = default;
    /// A link to the control `handle` holds or will hold.
    ControlLink(Control<S>& handle) : handle_(&handle) // NOLINT(google-explicit-constructor)
    {
    }

    /// True when a handle was given.
    [[nodiscard]] explicit operator bool() const
    {
        return handle_ != nullptr;
    }

    /// For a spec's build(): calls `link(target)` now if the handle already holds a control, and
    /// again each time a build fills the handle, as long as `owner` lives. `owner` is the control
    /// being built, which keeps the target as an ElementWatch (or unlinks itself in its
    /// destructor) so a destroyed target never leaves it holding a dangling pointer.
    template <typename Fn>
    void resolve(Element& owner, Fn link) const
    {
        using Type = typename S::control_type; // here, not in the class: a spec may name a link to its own kind
        if (handle_ == nullptr)
        {
            return;
        }
        if (Type* target = handle_->get())
        {
            link(target);
        }
        handle_->links_.emplace_back(
            [watch = std::make_unique<ElementWatch>(&owner), link = std::move(link)](Type* target) mutable
            {
                if (watch->get() == nullptr)
                {
                    return false;
                }
                link(target);
                return true;
            });
    }

private:
    Control<S>* handle_ = nullptr;
};

/// ControlLink for a field that takes any control: TeachingTip's `.target` points at a button, a
/// text box or anything else that has a handle. Written `.target = save` with any
/// `ui::Control<S>`; it links in either build order and lets go of a destroyed target, as
/// ControlLink does.
class ElementLink
{
public:
    /// No link.
    ElementLink() = default;
    /// A link to the control `handle` holds or will hold.
    template <typename S>
    ElementLink(Control<S>& handle) : // NOLINT(google-explicit-constructor)
        resolve_(
            [&handle](Element& owner, Callback<void(Element*)> link)
            {
                ControlLink<S>{handle}.resolve(owner, [link = std::move(link)](Element* target) mutable { link(target); });
            })
    {
    }

    /// True when a handle was given.
    [[nodiscard]] explicit operator bool() const
    {
        return static_cast<bool>(resolve_);
    }

    /// For a spec's build(): see ControlLink::resolve(). Consumes the link.
    void resolve(Element& owner, Callback<void(Element*)> link)
    {
        if (resolve_)
        {
            resolve_(owner, std::move(link));
        }
    }

private:
    Callback<void(Element&, Callback<void(Element*)>)> resolve_;
};

/// Publish the built node through a spec's `.control` field, if a handle was given. Call it last
/// in build(), after handlers are installed; see apply_common() for an example.
template <typename S, typename C>
inline void publish_control(const ControlOut<S>& out, C* node)
{
    out.publish(node);
}

} // namespace ui
