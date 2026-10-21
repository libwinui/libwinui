// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/ui/hosting/NativeControl.hpp"

#include <optional>
#include <unordered_map>

#include <cstddef>

namespace ui::testing
{

/// NativeHost test double: tracks one Entry per native child instead of creating native windows.
///
/// A NativeControl's lifecycle (create once, move on every changed arrange, hide/show, clip,
/// destroy with the control) is plain state, and this host records it so a test can check it
/// without a window. Hand it to Element::sync_native() or install it with
/// Window::set_native_host(). The host must outlive every control synced against it, since a
/// NativeControl calls destroy() from its destructor.
///
/// Example:
/// @code
/// ui::NativeControlDesc desc;
/// desc.class_name = "EDIT";
///
/// ui::testing::TestNativeHost host;
/// {
///     ui::NativeControl control{desc, ui::Size{120.0f, 24.0f}};
///     control.arrange({10, 10, 120, 24});
///     control.sync_native(host);
///
///     const ui::testing::TestNativeHost::Entry* entry = host.find(control.visual_id());
///     REQUIRE(entry != nullptr);
///     REQUIRE(entry->create_count == 1);
///     REQUIRE(entry->bounds == ui::Rect{10, 10, 120, 24});
///     REQUIRE(entry->owner == &control);
/// }
/// REQUIRE(host.count() == 0); // destroyed along with the control
/// @endcode
class TestNativeHost final : public NativeHost
{
public:
    /// Everything recorded about one native child, keyed by its VisualId.
    struct Entry
    {
        /// The description passed to the first, creating ensure_created() call.
        NativeControlDesc desc;
        /// Last bounds from set_bounds(), in window DIPs.
        Rect bounds;
        /// Last value from set_visible(); a fresh entry starts visible.
        bool visible = true;
        /// True once ensure_created() has run for this id.
        bool created = false;
        /// How many times ensure_created() created the entry; repeat calls don't count.
        int create_count = 0;
        /// Number of set_bounds() calls; a NativeControl only moves when its bounds changed.
        int move_count = 0;
        /// Control registered via set_owner(), which a real host uses to route the native child's events.
        NativeControl* owner = nullptr;
        /// set_clip()'s last visible region (nullopt: unclipped).
        std::optional<Rect> clip;
        /// Number of set_clip() calls.
        int clip_count = 0;
        /// Number of set_visible() calls.
        int visible_count = 0;
    };

    /// Creates the entry on the first call for `id` and stores `desc`; later calls are no-ops.
    void ensure_created(VisualId id, const NativeControlDesc& desc) override
    {
        Entry& e = entries_[id];
        if (!e.created)
        {
            e.desc    = desc;
            e.created = true;
            ++e.create_count;
        }
    }

    /// Records `bounds` and bumps Entry::move_count.
    void set_bounds(VisualId id, const Rect& bounds) override
    {
        Entry& e = entries_[id];
        e.bounds = bounds;
        ++e.move_count;
    }

    /// Records `visible` and bumps Entry::visible_count; ignored for an id with no entry.
    void set_visible(VisualId id, bool visible) override
    {
        auto it = entries_.find(id);
        if (it != entries_.end())
        {
            it->second.visible = visible;
            ++it->second.visible_count;
        }
    }

    /// Records the visible region (nullopt: unclipped) and bumps Entry::clip_count.
    void set_clip(VisualId id, const std::optional<Rect>& visible) override
    {
        Entry& e = entries_[id];
        e.clip   = visible;
        ++e.clip_count;
    }

    /// Erases the entry, so find() returns null and count() drops.
    void destroy(VisualId id) override
    {
        entries_.erase(id);
    }

    /// Tells every control still registered that its host is gone, as the real host does when
    /// its window is closed under a tree that lives on.
    ~TestNativeHost() override
    {
        for (const auto& [id, entry] : entries_)
        {
            if (entry.owner != nullptr)
            {
                notify_host_lost(*entry.owner);
            }
        }
    }

    /// Records `owner` on the entry for `id`.
    void set_owner(VisualId id, NativeControl* owner) override
    {
        entries_[id].owner = owner;
    }

    /// Always null: no real handle exists headlessly.
    [[nodiscard]] NativeHandle native_handle(VisualId) const override
    {
        return nullptr; // no real handle exists headlessly
    }

    /// The entry for `id`, or null if it was never touched or has been destroyed. The pointer is
    /// invalidated by the next call that adds or erases an entry.
    [[nodiscard]] const Entry* find(VisualId id) const
    {
        auto it = entries_.find(id);
        return it == entries_.end() ? nullptr : &it->second;
    }
    /// Number of live entries (native children created or touched and not yet destroyed).
    [[nodiscard]] std::size_t count() const
    {
        return entries_.size();
    }

private:
    std::unordered_map<VisualId, Entry> entries_;
};

} // namespace ui::testing
