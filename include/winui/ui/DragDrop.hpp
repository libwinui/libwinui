// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/ui/Clipboard.hpp" // DataPackageOperation

#include <any>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ui
{

class Element;

/// What a drag carries: text, files and an application-defined payload, such as a ListView's
/// dragged item indices or a TreeView's node path.
///
/// The source fills it in and hands it to DragDrop::start(). A drop target reads it through
/// DragEventArgs::data. Nothing is serialized, so `data` can hold any copyable C++ value. Files
/// and text dragged in from the file manager or another app arrive through the same drop events
/// while they move over the window. Their package has only `files` and `text` set, and no
/// `source`. The caption a target answers with shows under the file manager's drag image.
///
/// Example:
/// @code
/// void start_row_drag(ui::Element& row, ui::Point pointer)
/// {
///     ui::DataPackage package;
///     package.text                = "Ada Lovelace";
///     package.data                = std::size_t{3}; // the dragged row, read back with std::any_cast
///     package.requested_operation = ui::DataPackageOperation::Copy;
///     ui::RenderRecording visual; // what follows the pointer
///     row.render(visual);
///     ui::DragDrop::start(row, std::move(package), std::move(visual), row.bounds(), pointer);
/// }
/// @endcode
struct DataPackage
{
    /// Plain text for targets that only understand text.
    std::string text;
    /// Paths of the files being dragged: what a drop from the file manager carries.
    std::vector<std::filesystem::path> files;
    /// The application's own payload; the target recovers it with `std::any_cast`.
    std::any data;
    /// What the source would like done with it; a target may accept something else.
    DataPackageOperation requested_operation = DataPackageOperation::Move;
    /// The element the drag started from. DragDrop::start() fills it in; a target compares it to
    /// tell a reorder within itself from a drop from elsewhere.
    const Element* source = nullptr;
};

/// What a drop target is told as a drag moves over it and ends on it.
///
/// Passed to Element::drag_enter(), drag_over(), drag_leave() and drop(), and to the handlers set
/// with Element::on_drag_over() and the others. drag_over() answers with a DragResponse.
///
/// Example:
/// @code
/// std::vector<std::size_t> favorites;
/// auto                     target = std::make_unique<ui::BorderControl>();
/// target->set_allow_drop(true);
/// target->on_drag_over([](const ui::DragEventArgs& e)
/// {
///     if (!std::any_cast<std::size_t>(&e.data.data))
///     {
///         return ui::DragResponse{}; // not a row: refused
///     }
///     return ui::DragResponse{.operation = ui::DataPackageOperation::Copy, .caption = "Add to favorites"};
/// });
/// target->on_drop([&](const ui::DragEventArgs& e)
/// {
///     favorites.push_back(std::any_cast<std::size_t>(e.data.data));
/// });
/// @endcode
struct DragEventArgs
{
    /// The package being dragged; owned by the drag, valid for this call only.
    const DataPackage& data;
    /// The pointer, in window coordinates.
    Point position{};
};

/// A drop target's answer to a drag over it: what a drop there would do, and what the pointer
/// shows meanwhile.
///
/// Returned by Element::drag_over() and the handler set with Element::on_drag_over(), which are
/// asked when the drag comes over the element and at every move after. A refusal (`operation`
/// None, the default) passes the question on to the next drop target up the tree. The first one
/// that accepts gets the drop.
///
/// Example:
/// @code
/// auto zone = std::make_unique<ui::BorderControl>();
/// zone->set_allow_drop(true);
/// zone->on_drag_over([](const ui::DragEventArgs& e)
/// {
///     if (e.data.files.empty())
///     {
///         return ui::DragResponse{};
///     }
///     return ui::DragResponse{.operation = ui::DataPackageOperation::Copy, .caption = "Attach"};
/// });
/// @endcode
struct DragResponse
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// What a drop here would do; None refuses it.
    DataPackageOperation operation = DataPackageOperation::None;
    /// Text shown in a small box beside the pointer; empty shows none.
    std::string caption{};
    /// Whether the operation glyph is shown beside the pointer.
    bool glyph_visible = true;
};

/// The drag in progress, one per process: how a control starts an in-process drag and drop.
///
/// A source calls start() from its own pointer handling, usually once a pressed pointer has moved
/// a few DIPs. The Window the event was dispatched in then takes the drag over:
/// - pointer input drives the drag instead of reaching the tree
/// - the recorded visual follows the pointer at 0.8 opacity
/// - drop targets (Element::allow_drop()) get drag_enter(), drag_over(), drag_leave() and drop()
/// - Escape cancels the drag
///
/// The source learns the outcome through Element::drag_completed(). Only one drag runs at a time,
/// and start() while one is running is ignored. ListView, TileView and TreeView use this for
/// their item dragging, and an app's own control can too.
///
/// Example:
/// @code
/// class Chip final : public ui::Element
/// {
/// public:
///     explicit Chip(std::string label) : label_(std::move(label)) {}
///
///     void drag_completed(ui::DataPackageOperation result) override
///     {
///         dropped_ = result != ui::DataPackageOperation::None;
///     }
///
/// protected:
///     ui::Size measure_override(ui::Size, const ui::MeasureContext& context) const override
///     {
///         return context.measurer.measure(label_, context.theme.text_style);
///     }
///     void render_override(ui::Renderer& r, const ui::RenderContext& context) const override
///     {
///         r.draw_text(label_, bounds(), context.theme.colors().text, context.theme.text_style);
///     }
///     bool pointer_override(const ui::PointerEvent& e) override
///     {
///         if (e.action != ui::PointerAction::Down || ui::DragDrop::active())
///         {
///             return false;
///         }
///         ui::DataPackage package;
///         package.text = label_;
///         ui::RenderRecording visual; // what follows the pointer
///         render(visual);
///         ui::DragDrop::start(*this, std::move(package), std::move(visual), bounds(), e.position);
///         return true;
///     }
///
/// private:
///     std::string label_;
///     bool        dropped_ = false;
/// };
/// @endcode
class DragDrop
{
public:
    /// Start carrying `data` from `source`. `visual` is drawn under the pointer. It is recorded in
    /// the same coordinates as `visual_bounds`, the dragged thing's rect, and `pointer` is where the
    /// pointer is now, so the visual keeps that offset to the pointer as it moves. Sets
    /// `data.source` to `&source`. Ignored while another drag is running.
    static void start(Element& source, DataPackage data, RenderRecording visual, Rect visual_bounds, Point pointer);
    /// Whether a drag is in progress (or about to be taken over by its window).
    [[nodiscard]] static bool active();
    /// Whether a drag has been started but no window has taken it over yet.
    [[nodiscard]] static bool pending();
    /// End the drag in progress without a drop. The source's drag_completed() gets None. A
    /// pending drag is discarded.
    static void cancel();

private:
    // ---- the window's side ----
    friend class Window;

    // The state of one drag (package, visual, pointer offset, current target), defined in
    // src/ui/DragSession.hpp; opaque outside the Window that runs it.
    struct Session;
    struct SessionDeleter
    {
        void operator()(Session* session) const;
    };
    using SessionPtr = std::unique_ptr<Session, SessionDeleter>;
    // The started drag, handed to the window it began in; null when none is pending. Called by
    // Window after dispatching a pointer event.
    [[nodiscard]] static SessionPtr take_pending();
    // The drag started but not taken over yet, one per UI thread.
    [[nodiscard]] static SessionPtr& pending_session();
};

} // namespace ui
