// SPDX-License-Identifier: MIT
#pragma once

// What DatePicker and TimePicker share: their field split into parts, and the three ways an era
// lets the user change them (ControlChrome::DateTimePickerChrome). Fluent's flyout of looping
// columns, Windows 8.1's drop-down lists, and comctl32's field edited in place. The controls
// describe their parts through a PickerModel; the engine draws them and takes the input. Private.

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/LayoutContext.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <cstddef>

namespace ui
{

class CalendarViewControl;
class ComboBoxControl;

namespace detail
{

// One part of a date or a time: what the field shows and the choices the lists offer.
struct PickerPart
{
    std::string              name;        // what automation calls it: "day", "month", "hour"
    std::string              placeholder; // what the field shows while there is no value
    std::vector<std::string> items;       // the choices, in order
    std::size_t              selected = 0;
    bool                     loops    = true;  // the flyout's column wraps around
    float                    weight   = 1.0f;  // the column's share of the width until measure() sizes it
    bool                     leading  = false; // left-aligned 9 DIPs in (the month), else centered
};

// One run of comctl32's field: text the pattern writes as it stands, or the value of a part.
struct PickerSegment
{
    std::string text;
    int         part = -1; // the part the run shows, or -1 for literal text
};

// What a DatePicker or TimePicker tells the engine.
class PickerModel
{
public:
    // The parts for `proposal` (an index per part), or, when it is empty, for the value: the
    // value, or now while there is none.
    [[nodiscard]] virtual std::vector<PickerPart> parts(std::span<const std::size_t> proposal) const = 0;
    [[nodiscard]] virtual bool                    has_value() const                                  = 0;
    // Sets the value from an index per part.
    virtual void commit(std::span<const std::size_t> selection) = 0;
    // comctl32's field: the value (or now) in the locale's short pattern, split at its parts.
    [[nodiscard]] virtual std::vector<PickerSegment> segments() const = 0;
    // The index a typed number picks in `part` (3 in the month is March), if any.
    [[nodiscard]] virtual std::optional<std::size_t> index_for_number(std::size_t part, int number) const = 0;
    // A DatePicker's drop-down opens a calendar on this date, and a pick there comes back here.
    [[nodiscard]] virtual bool                                       calendar_button() const = 0;
    [[nodiscard]] virtual std::optional<std::chrono::year_month_day> calendar_date() const   = 0;
    virtual void commit_calendar_date(std::chrono::year_month_day date)                      = 0;
    // The strings the Accept and Dismiss buttons are named by.
    [[nodiscard]] virtual std::string accept_name() const  = 0;
    [[nodiscard]] virtual std::string dismiss_name() const = 0;
    // Makes `part` (a drop-down list) a part of the owner (Element::set_parent_of()).
    virtual void adopt(Element& part) = 0;
    // The owner's protected Element calls, for the engine.
    virtual void invalidate(DirtyFlags flags)     = 0;
    virtual void open_popup(PopupRequest request) = 0;
    virtual void close_popup()                    = 0;
    virtual void request_tick()                   = 0;

protected:
    ~PickerModel() = default;
};

class PickerFlyout;

// The engine an owner forwards its Element work to. The owner keeps its header and description
// and has already taken them off its bounds when it calls arrange() and render().
class PickerEngine
{
public:
    PickerEngine(Element& owner, PickerModel& model);
    ~PickerEngine();
    PickerEngine(const PickerEngine&)            = delete;
    PickerEngine& operator=(const PickerEngine&) = delete;

    [[nodiscard]] Size measure(Size available, const MeasureContext& context) const;
    void               arrange(const ArrangeContext& context);
    void               render(Renderer& renderer, const Theme& theme) const;
    bool               pointer(const PointerEvent& e);
    bool               key(const KeyEvent& e);
    bool               text(const TextInputEvent& e);
    void               tick(Duration delta);
    void               enabled_changed();
    void               focus_changed();
    // The value changed: the drop-down lists and an open flyout follow.
    void value_changed();

    void for_each_child(ChildVisitor visit) const;
    void collect_focusables(std::vector<Element*>& out);
    void automation_children(std::vector<Element*>& out) const;

    // Whether the flyout (or a comctl32 calendar) is open, and opening or closing it.
    [[nodiscard]] bool is_open() const
    {
        return open_;
    }
    void set_open(bool open);
    // Called after it opened (true) or closed (false).
    Callback<void(bool)> on_open_changed;

    // Called by the flyout.
    void                       accept(std::span<const std::size_t> selection);
    void                       dismiss();
    [[nodiscard]] PickerModel& model()
    {
        return model_;
    }
    // Gives each part the width measure() found for its column, as its weight.
    void apply_column_widths(std::vector<PickerPart>& parts) const;

private:
    enum class Kind
    {
        Flyout,
        ComboBoxes,
        Field,
    };
    [[nodiscard]] Kind                    kind_for(const Theme& theme) const;
    void                                  sync_combos();
    void                                  step_part(int delta);
    void                                  set_part_index(std::size_t index);
    void                                  sync_popup();
    [[nodiscard]] std::vector<PickerPart> current_parts() const;
    [[nodiscard]] Rect                    button_rect() const;
    [[nodiscard]] Rect                    field_rect() const;

    Element&     owner_;
    PickerModel& model_;
    mutable Kind kind_           = Kind::Flyout;
    bool         open_           = false;
    bool         hovered_        = false;
    bool         pressed_        = false;
    bool         button_pressed_ = false;
    int          spin_pressed_   = 0; // +1 up, -1 down
    double       spin_ms_        = 0.0;
    std::size_t  active_part_    = 0;
    std::string  typed_;
    double       typed_ms_ = 0.0;
    // Measured for the theme measure() last saw.
    mutable float              field_height_ = 32.0f;
    mutable float              button_width_ = 17.0f;
    mutable std::vector<float> segment_widths_;
    mutable std::vector<float> column_widths_; // the flyout construction's columns, widest item each

    std::unique_ptr<PickerFlyout>                 flyout_;
    std::unique_ptr<CalendarViewControl>          calendar_;
    std::vector<std::unique_ptr<ComboBoxControl>> combos_;
    bool                                          syncing_ = false;
};

} // namespace detail
} // namespace ui
