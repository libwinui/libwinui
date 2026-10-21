// SPDX-License-Identifier: MIT
#include "winui/ui/DragDrop.hpp"

#include "DragSession.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/RenderRecording.hpp"

#include <utility>

namespace ui
{

namespace
{
// One drag at a time: the mouse drags one thing. UI-thread state, like the tree itself.
thread_local bool g_running          = false;
thread_local bool g_cancel_requested = false;
} // namespace

DragDrop::SessionPtr& DragDrop::pending_session()
{
    thread_local SessionPtr pending;
    return pending;
}

void DragDrop::SessionDeleter::operator()(Session* session) const
{
    delete session;
}

void DragDrop::start(Element& source, DataPackage data, RenderRecording visual, Rect visual_bounds, Point pointer)
{
    if (g_running)
    {
        return; // one at a time
    }
    SessionPtr session{new Session};
    data.source            = &source;
    session->data          = std::move(data);
    session->visual        = std::move(visual);
    session->visual_bounds = visual_bounds;
    session->grab          = Point{pointer.x - visual_bounds.x, pointer.y - visual_bounds.y};
    session->position      = pointer;
    session->source.reset(&source);
    pending_session()  = std::move(session);
    g_cancel_requested = false;
}

bool DragDrop::active()
{
    return g_running || pending_session() != nullptr;
}

bool DragDrop::pending()
{
    return pending_session() != nullptr;
}

void DragDrop::cancel()
{
    if (SessionPtr& pending = pending_session())
    {
        pending.reset();
        return;
    }
    if (g_running)
    {
        g_cancel_requested = true;
    }
}

DragDrop::SessionPtr DragDrop::take_pending()
{
    return std::move(pending_session());
}

namespace drag_detail
{

void set_running(bool running)
{
    g_running = running;
    if (!running)
    {
        g_cancel_requested = false;
    }
}

bool take_cancel_request()
{
    return std::exchange(g_cancel_requested, false);
}

} // namespace drag_detail

} // namespace ui
