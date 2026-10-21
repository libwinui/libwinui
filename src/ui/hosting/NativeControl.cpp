// SPDX-License-Identifier: MIT
#include "winui/ui/hosting/NativeControl.hpp"

#include "../../platform/Backend.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace ui
{

void NativeHost::notify_host_lost(NativeControl& control) noexcept
{
    control.host_lost();
}

NativeControl::NativeControl(NativeControlDesc desc, Size size) : desc_(std::move(desc)), size_(size)
{
    detail::link_spell_checker(); // a native edit control spell-checks through the same checker
}

NativeControl::~NativeControl()
{
    if (host_ && created_)
    {
        host_->forget(visual_id());
        host_->destroy(visual_id());
    }
}

void NativeControl::set_native_size(Size size)
{
    if (size == size_)
    {
        return;
    }
    size_ = size;
    mark_dirty(DirtyFlags::Layout);
}

Rect NativeControl::visible_rect() const
{
    for (const Element* e = this; e != nullptr; e = e->parent())
    {
        if (e->visibility() == Visibility::Collapsed)
        {
            return Rect{};
        }
    }
    const Rect own = bounds();
    if (own.width <= 0.0f || own.height <= 0.0f)
    {
        return Rect{};
    }
    const Rect visible = intersection(own, effective_viewport());
    if (visible.width <= 0.0f || visible.height <= 0.0f)
    {
        return Rect{};
    }
    return visible;
}

Size NativeControl::measure_override(Size /*available*/, const MeasureContext&) const
{
    return size_;
}

void NativeControl::render_override(Renderer& /*renderer*/, const RenderContext&) const
{
    // Empty on purpose: the native window paints itself through its own WM_PAINT, on top of
    // whatever the Renderer drew.
}

void NativeControl::sync_native(NativeHost& host)
{
    host_ = &host;
    if (!created_)
    {
        host.ensure_created(visual_id(), desc_);
        // Lets the host route the child window's WM_CONTEXTMENU and EN_CHANGE back to us.
        host.set_owner(visual_id(), this);
        created_            = true;
        last_synced_bounds_ = bounds();
        host.set_bounds(visual_id(), bounds());
        // Last, so configuration messages reach a registered window at its real size. Some
        // control classes size their internals off the client rect on the first such message.
        if (const NativeHandle handle = host.native_handle(visual_id()))
        {
            on_native_created(handle);
        }
    }
    // Compared with slack: intersecting with a larger rect can come back a rounding error
    // smaller, which must not count as clipped.
    const Rect          visible = visible_rect();
    const bool          shown   = visible.width > 0.0f && visible.height > 0.0f;
    std::optional<Rect> clip;
    if (shown)
    {
        const Rect  own   = bounds();
        const float slack = 0.01f;
        if (visible.x > own.x + slack || visible.y > own.y + slack ||
            visible.x + visible.width < own.x + own.width - slack || visible.y + visible.height < own.y + own.height - slack)
        {
            clip = visible;
        }
    }
    if (!(bounds() == last_synced_bounds_))
    {
        last_synced_bounds_ = bounds();
        last_clip_          = clip;
        host.place(visual_id(), bounds(), clip);
    }
    else if (clip != last_clip_)
    {
        last_clip_ = clip;
        host.set_clip(visual_id(), clip);
    }
    host.present(visual_id(), shown);

    // Every pass: the only route a spell checker has in, since real focus belongs to the child
    // HWND, not the framework's ring. A control early-outs when nothing moved.
    if (SpellChecker* checker = host.spell_checker())
    {
        sync_spell_checker(*checker);
    }
}

std::unique_ptr<ui::Element> Native::build() &&
{
    auto node = std::make_unique<ui::NativeControl>(std::move(desc), native_size.initial());
    apply(native_size, *node, [raw = node.get()](const Size& v) { raw->set_native_size(v); });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
