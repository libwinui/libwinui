// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"

#include <memory>
#include <vector>

namespace ui
{

struct DragDrop::Session
{
    ElementWatch    source;
    DataPackage     data;
    RenderRecording visual;
    Rect            visual_bounds{};
    Point           grab{}; // the pointer's offset into visual_bounds when the drag started
    Point           position{};
    // The drop targets asked at the last move, innermost first: each heard drag_enter() and hears
    // drag_leave() once it is no longer asked. The last of them is the one that accepted, if any.
    std::vector<std::unique_ptr<ElementWatch>> asked;
    ElementWatch                               responder;
    DataPackageOperation                       accepted = DataPackageOperation::None;
    std::string                                caption;
    bool                                       glyph_visible = true;
    // Dragged in from another app: the backend shows the caption beside its own drag image.
    bool external = false;
};

namespace drag_detail
{
/// The window running the drag says it has one (or no longer has); DragDrop::active() reads it.
void set_running(bool running);
/// Whether DragDrop::cancel() asked for the running drag to end.
[[nodiscard]] bool take_cancel_request();
} // namespace drag_detail

} // namespace ui
