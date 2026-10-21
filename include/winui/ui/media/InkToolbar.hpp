// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <cstddef>

namespace ui
{

class FlyoutControl;
class InkToolbarControl;
class SliderControl;
class StackControl;

/// A tool on a ui::InkToolbarControl.
enum class InkToolbarTool
{
    BallpointPen, ///< A round, solid pen.
    Pencil,       ///< Pencil ink.
    Highlighter,  ///< A wide, translucent chisel nib.
    Eraser,       ///< The stroke eraser, with "Erase all ink" in its flyout.
    CustomPen,    ///< A pen the app defines (InkToolbarControl::add_custom_pen()).
    CustomTool,   ///< A tool the app implements (InkToolbarControl::add_custom_tool()).
};

/// How a custom button of a ui::InkToolbarControl looks and what it does. The button is a custom
/// pen, a custom tool or a custom toggle. Each field applies to the kinds its comment names.
///
/// The callables are std::function, so they can be copied. The spec lists custom buttons in a
/// braced list, which copies its entries.
///
/// Example:
/// @code
/// // A calligraphy pen: a flat nib a third as wide as it is tall.
/// auto toolbar = std::make_unique<ui::InkToolbarControl>();
/// toolbar->add_custom_pen({.name  = "Calligraphy pen",
///                          .icon  = ui::IconGlyph{0xEDFB},
///                          .pen   = [](ui::Color color, float width)
///                          {
///                              return ui::InkDrawingAttributes{.color   = color,
///                                                              .size    = {width / 3.0f, width},
///                                                              .pen_tip = ui::InkPenTip::Rectangle};
///                          }});
/// @endcode
struct InkToolbarCustomButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Its tool tip and accessible name. Every kind.
    std::string name{};
    /// The icon it shows: a Symbol, a glyph, a path or a picture. A glyph of the default symbol
    /// font takes the theme's icon font. Every kind.
    IconSource icon{};
    /// What a click on the active custom pen or tool opens in its flyout. Without it, a custom pen
    /// opens the pens' color and size flyout, and a custom tool opens nothing.
    std::function<Spec()> configuration{};
    /// What a custom pen draws with, made from the color and width picked in its flyout. Without it,
    /// the pen draws with a round nib of that color and width.
    std::function<InkDrawingAttributes(Color color, float width)> pen{};
    /// Called when a custom toggle turns on or off, with the new state.
    std::function<void(bool checked)> on_checked_changed{};
};

/// Which stencil the stencil button shows on the canvas.
enum class InkToolbarStencilKind
{
    Ruler,      ///< The ruler (InkCanvasControl::set_ruler()).
    Protractor, ///< The protractor (InkCanvasControl::set_protractor()).
};

/// What an InkToolbar's `on_stencil_button_checked_changed` gets: whether the stencil button is
/// on, and the stencil it shows.
struct InkToolbarStencilChangedArgs
{
    bool                  checked = false;                        ///< The button is on and the stencil shows.
    InkToolbarStencilKind kind    = InkToolbarStencilKind::Ruler; ///< The stencil the button selects.
};

/// Which side of its button a tool's flyout opens on.
enum class InkToolbarButtonFlyoutPlacement
{
    Auto,   ///< Below the button, the same as Bottom.
    Top,    ///< Above the button, left edges lined up.
    Bottom, ///< Below the button, left edges lined up.
    Left,   ///< Left of the button, centered.
    Right,  ///< Right of the button, centered.
};

/// One tool button of a ui::InkToolbarControl: a pen, the eraser or another tool. The toolbar
/// makes these. Reach one through InkToolbarControl::tool_button().
///
/// The button is a 36 DIP square with the tool's glyph. A pen button also shows its selected color
/// in the glyph, and holds the palette and the stroke width its flyout edits. The eraser button
/// holds whether its flyout offers "Erase all ink". The active tool's button is drawn selected,
/// with a small chevron toward the flyout it opens on a second click.
///
/// Example:
/// @code
/// auto                             toolbar = std::make_unique<ui::InkToolbarControl>();
/// ui::InkToolbarToolButtonControl* pen     = toolbar->tool_button(ui::InkToolbarTool::BallpointPen);
/// pen->set_selected_brush_index(7);       // red, in the default palette
/// pen->set_selected_stroke_width(6.0f);   // within min_stroke_width()..max_stroke_width()
/// @endcode
class InkToolbarToolButtonControl final : public Element, private SelectionItemProvider, private ExpandCollapseProvider
{
public:
    /// The button for `tool`, with that tool's palette, width range and defaults.
    explicit InkToolbarToolButtonControl(InkToolbarTool tool);
    /// A custom pen or tool button, looking as `custom` says. `tool` is CustomPen or CustomTool.
    /// A custom pen starts with the ballpoint pen's palette and widths.
    InkToolbarToolButtonControl(InkToolbarTool tool, InkToolbarCustomButton custom);
    ~InkToolbarToolButtonControl() override;

    /// Which tool the button is.
    [[nodiscard]] InkToolbarTool tool() const
    {
        return tool_;
    }
    /// True for the ballpoint pen, the pencil, the highlighter and a custom pen.
    [[nodiscard]] bool is_pen() const
    {
        return tool_ != InkToolbarTool::Eraser && tool_ != InkToolbarTool::CustomTool;
    }
    /// What a custom button was made with, or nullptr for a built-in tool.
    [[nodiscard]] const InkToolbarCustomButton* custom() const
    {
        return custom_ ? &*custom_ : nullptr;
    }
    /// Whether this is the toolbar's active tool.
    [[nodiscard]] bool checked() const
    {
        return checked_;
    }

    // ---- pen ----
    /// The colors the flyout offers. The ballpoint pen and the pencil share a 30-color palette, and
    /// the highlighter has six colors.
    [[nodiscard]] const std::vector<Color>& palette() const
    {
        return palette_;
    }
    /// Replaces the palette. The selected index is clamped into it.
    void set_palette(std::vector<Color> palette);
    /// The selected palette index. 0 by default, which is black for the pens and yellow for the
    /// highlighter.
    [[nodiscard]] std::size_t selected_brush_index() const
    {
        return selected_index_;
    }
    /// Selects a palette color, clamping `index` into the palette. Updates the drawing attributes
    /// when this is the active pen.
    void set_selected_brush_index(std::size_t index);
    /// The palette color at the selected index, or black for an empty palette.
    [[nodiscard]] Color selected_color() const;

    /// The thinnest stroke width: 1 for the pens, 12 for the highlighter.
    [[nodiscard]] float min_stroke_width() const
    {
        return min_width_;
    }
    /// The thickest stroke width: 24 for the pens, 64 for the highlighter.
    [[nodiscard]] float max_stroke_width() const
    {
        return max_width_;
    }
    /// Sets both ends of the range, swapping them when given backwards, and clamps the selected
    /// width into it.
    void set_stroke_width_range(float min, float max);
    /// The selected stroke width: 2 for the ballpoint pen, 4 for the pencil, 16 for the
    /// highlighter.
    [[nodiscard]] float selected_stroke_width() const
    {
        return selected_width_;
    }
    /// Sets the stroke width, clamped into the range. Updates the drawing attributes when this is
    /// the active pen.
    void set_selected_stroke_width(float width);

    /// What this pen draws with. The ballpoint pen draws with a round nib of the selected width,
    /// and the pencil with pencil ink of that width. The highlighter draws as a highlighter, with a
    /// chisel a third of the width wide and the width tall. A custom pen draws with what its
    /// InkToolbarCustomButton::pen makes of the color and width.
    [[nodiscard]] InkDrawingAttributes drawing_attributes() const;

    // ---- eraser ----
    /// Whether the eraser's flyout offers "Erase all ink" under its stroke and point erasers. True
    /// by default.
    [[nodiscard]] bool clear_all_visible() const
    {
        return clear_all_visible_;
    }
    /// Shows or hides the "Erase all ink" item.
    void set_clear_all_visible(bool visible);

    /// Whether the chevron that says a second click opens a flyout shows. It shows on the active
    /// tool while it has a flyout and that flyout is closed.
    [[nodiscard]] bool extension_glyph_shown() const;
    /// Whether the pointer is over the button.
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }
    /// Shows the tool's name.
    [[nodiscard]] std::string tool_tip() const override;

    // ---- automation ----
    /// Button for the pens, Custom for the eraser and a custom tool.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return is_pen() ? AutomationRole::Button : AutomationRole::Custom;
    }

protected:
    /// The tool's name, such as "Ballpoint pen", with ", selected" while it is the active tool.
    [[nodiscard]] std::string automation_name_fallback() const override;

    // ---- Element ----
    /// 36 by 36 DIPs. The toolbar adds the 2 DIP margin around it.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// The background for the state, the color glyph of a pen, the tool's glyph and the chevron.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// A left click selects the tool. On the active tool, it opens the tool's flyout.
    bool pointer_override(const PointerEvent& e) override;


private:
    friend class InkToolbarControl;

    void attributes_changed();

    InkToolbarTool                        tool_;
    std::optional<InkToolbarCustomButton> custom_{};
    std::vector<Color>                    palette_{};
    std::size_t                           selected_index_    = 0;
    float                                 min_width_         = 1.0f;
    float                                 max_width_         = 24.0f;
    float                                 selected_width_    = 2.0f;
    bool                                  clear_all_visible_ = true;
    bool                                  checked_           = false;
    bool                                  hovered_           = false;
    bool                                  pressed_           = false;

    InkToolbarControl* owner_  = nullptr; // the toolbar that made this button
    FlyoutControl*     flyout_ = nullptr; // the flyout wrapping this button, owned by the toolbar

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.selection_item  = this;
        out.expand_collapse = this;
    }
    // The active tool.
    [[nodiscard]] bool item_selected() const override
    {
        return checked_;
    }
    // Makes this the active tool.
    bool select_item() override;
    // Whether its flyout is open.
    [[nodiscard]] bool expanded_state() const override;
    // Opens or closes its flyout. False when it has none.
    bool set_expanded_state(bool expanded) override;
};

/// A toggle button on a ui::InkToolbarControl. It turns on or off and sits after the tools. Add
/// one with InkToolbarControl::add_custom_toggle().
///
/// Example:
/// @code
/// // A toggle that lets touch draw as well as the pen and the mouse.
/// auto canvas  = std::make_unique<ui::InkCanvasControl>();
/// auto toolbar = std::make_unique<ui::InkToolbarControl>();
/// ui::InkToolbarToggleButtonControl* touch =
///     toolbar->add_custom_toggle({.name = "Touch writing", .icon = ui::IconGlyph{0xED5F}});
/// touch->on_checked_changed([ink = canvas.get()](bool on) {
///     ink->set_input_device_types(on ? ui::PointerDeviceTypes::All
///                                    : ui::PointerDeviceTypes::Pen | ui::PointerDeviceTypes::Mouse);
/// });
/// @endcode
class InkToolbarToggleButtonControl final : public Element, private ToggleProvider
{
public:
    /// A toggle looking as `look` says, off.
    explicit InkToolbarToggleButtonControl(InkToolbarCustomButton look);
    ~InkToolbarToggleButtonControl() override;

    /// What the toggle was made with.
    [[nodiscard]] const InkToolbarCustomButton& look() const
    {
        return look_;
    }
    /// Whether the toggle is on.
    [[nodiscard]] bool checked() const
    {
        return checked_;
    }
    /// Turns the toggle on or off, raising on_checked_changed() when the state changes.
    void set_checked(bool checked);
    /// Raised with the new state when the toggle turns on or off, after the look's own handler.
    void on_checked_changed(Callback<void(bool)> handler)
    {
        on_checked_changed_ = std::move(handler);
    }
    /// Whether the pointer is over the button.
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }
    /// The look's name.
    [[nodiscard]] std::string tool_tip() const override
    {
        return look_.name;
    }

    // ---- automation ----
    /// A button that toggles.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Button;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return look_.name;
    }

    // ---- Element ----
    /// 36 by 36 DIPs, like a tool button.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// The background for the state, with the selected color while on, and the glyph.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// A left click flips it.
    bool pointer_override(const PointerEvent& e) override;


private:
    friend class InkToolbarControl;

    InkToolbarCustomButton look_;
    bool                   checked_ = false;
    bool                   hovered_ = false;
    bool                   pressed_ = false;
    InkToolbarControl*     owner_   = nullptr;
    Callback<void(bool)>   on_checked_changed_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.toggle = this;
    }
    [[nodiscard]] ToggleState toggle_value() const override
    {
        return checked_ ? ToggleState::On : ToggleState::Off;
    }
    // Flips it. False while the toolbar is disabled.
    bool toggle_next() override;
};

/// The stencil button of a ui::InkToolbarControl, which shows the ruler or the protractor on the
/// target canvas. Add it with InkToolbarControl::add_stencil_button().
///
/// A click while it is off turns it on and shows the stencil picked last, the ruler at first. A
/// click while it is on opens its flyout, which picks the ruler or the protractor. Picking the one
/// showing hides it and turns the button off. With only one of the two offered, it is a plain
/// toggle with no flyout.
///
/// Example:
/// @code
/// auto                                toolbar  = std::make_unique<ui::InkToolbarControl>();
/// ui::InkToolbarStencilButtonControl* stencils = toolbar->add_stencil_button();
/// stencils->set_selected_stencil(ui::InkToolbarStencilKind::Protractor);
/// toolbar->set_stencil_button_checked(true); // the protractor shows on the canvas
/// @endcode
class InkToolbarStencilButtonControl final : public Element, private ToggleProvider, private ExpandCollapseProvider
{
public:
    InkToolbarStencilButtonControl();
    ~InkToolbarStencilButtonControl() override;

    /// The stencil the button shows while it is on. The ruler by default.
    [[nodiscard]] InkToolbarStencilKind selected_stencil() const
    {
        return selected_;
    }
    /// Picks the stencil, and shows it at once while the button is on.
    void set_selected_stencil(InkToolbarStencilKind kind);
    /// Whether the flyout offers the ruler. True by default.
    [[nodiscard]] bool ruler_item_visible() const
    {
        return ruler_item_;
    }
    void set_ruler_item_visible(bool visible); ///< See ruler_item_visible().
    /// Whether the flyout offers the protractor. True by default.
    [[nodiscard]] bool protractor_item_visible() const
    {
        return protractor_item_;
    }
    void set_protractor_item_visible(bool visible); ///< See protractor_item_visible().
    /// Whether the button is on, with a stencil showing.
    [[nodiscard]] bool checked() const
    {
        return checked_;
    }
    /// Whether the chevron that says a click opens the flyout shows. It shows while the button is
    /// on, offers both stencils and its flyout is closed.
    [[nodiscard]] bool extension_glyph_shown() const;
    /// Whether the pointer is over the button.
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }
    /// The selected stencil's name.
    [[nodiscard]] std::string tool_tip() const override;

    // ---- automation ----
    /// A button that toggles, and expands and collapses its flyout.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Button;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return tool_tip();
    }

    // ---- Element ----
    /// 36 by 36 DIPs, like a tool button.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// The background for the state, the selected stencil's glyph and the chevron.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// A left click turns it on or opens its flyout.
    bool pointer_override(const PointerEvent& e) override;


private:
    friend class InkToolbarControl;

    [[nodiscard]] bool offers_both() const
    {
        return ruler_item_ && protractor_item_;
    }

    InkToolbarStencilKind selected_         = InkToolbarStencilKind::Ruler;
    bool                  ruler_item_       = true;
    bool                  protractor_item_  = true;
    bool                  checked_          = false;
    bool                  hovered_          = false;
    bool                  pressed_          = false;
    InkToolbarControl*    owner_            = nullptr;
    FlyoutControl*        flyout_           = nullptr; // owned by the toolbar's panel
    Element*              ruler_entry_      = nullptr; // the flyout's two items
    Element*              protractor_entry_ = nullptr;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.toggle          = this;
        out.expand_collapse = this;
    }
    [[nodiscard]] ToggleState toggle_value() const override
    {
        return checked_ ? ToggleState::On : ToggleState::Off;
    }
    // Turns it on or off. False while the toolbar is disabled.
    bool               toggle_next() override;
    [[nodiscard]] bool expanded_state() const override;
    // Opens or closes its flyout. False when it has none.
    bool set_expanded_state(bool expanded) override;
};

/// A row of ink tools bound to a ui::InkCanvasControl. By default it holds a ballpoint pen, a
/// pencil, a highlighter and an eraser, in that order.
///
/// Clicking a tool makes it the active tool and sets the target canvas up for it. A pen sets its
/// drawing attributes and Inking mode, and the eraser sets Erasing mode. Clicking the active tool
/// again opens its flyout. A pen's flyout picks a color from the pen's palette and a stroke width
/// with a slider, over a sample stroke. The palette has 30 colors, six for the highlighter, and the
/// width goes from 1 to 24 DIPs, 12 to 64 for the highlighter. The eraser's flyout switches the
/// target canvas between its stroke and point erasers (InkCanvasControl::set_erase_mode()) and
/// offers "Erase all ink", which clears the canvas. The toolbar is one tab stop. The arrow keys
/// move between the tools, and Space or Enter does what a click does.
///
/// The app can add tools of its own after the built-in ones. A custom pen draws with attributes it
/// makes from the picked color and width. A custom tool turns the canvas's inking off, so the app
/// handles its input through on_unprocessed_input(), for example for a lasso. A custom toggle sits
/// after the tools. add_stencil_button() adds the button that shows a ruler or a protractor on the
/// canvas. `ui::InkToolbar` is the declarative form.
///
/// Example:
/// @code
/// auto  canvas  = std::make_unique<ui::InkCanvasControl>();
/// auto  toolbar = std::make_unique<ui::InkToolbarControl>();
/// toolbar->set_target_ink_canvas(canvas.get());
/// toolbar->on_active_tool_changed([](ui::InkToolbarTool tool)
///                                 { std::println("tool {}", static_cast<int>(tool)); });
/// toolbar->set_active_tool(ui::InkToolbarTool::Highlighter); // the canvas now highlights
///
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// stack->add(std::move(toolbar));
/// canvas->set_height(300.0f);
/// stack->add(std::move(canvas));
/// window.set_content(std::move(stack));
/// @endcode
class InkToolbarControl final : public Element
{
public:
    /// Every built-in tool, in the default order.
    static constexpr std::array<InkToolbarTool, 4>
        AllTools{InkToolbarTool::BallpointPen, InkToolbarTool::Pencil, InkToolbarTool::Highlighter, InkToolbarTool::Eraser};

    /// A toolbar with a button for each of `tools`, in that order. A repeated tool appears once.
    /// The first is the active tool.
    explicit InkToolbarControl(std::span<const InkToolbarTool> tools = AllTools);
    ~InkToolbarControl() override;

    // ---- tools ----
    /// The button for `tool`, or nullptr when this toolbar has none.
    [[nodiscard]] InkToolbarToolButtonControl* tool_button(InkToolbarTool tool) const;

    /// The selected tool. CustomPen or CustomTool for a custom button. active_tool_button() tells
    /// custom buttons apart.
    [[nodiscard]] InkToolbarTool active_tool() const
    {
        return active_;
    }
    /// Selects `tool` and sets the target canvas up for it, raising on_active_tool_changed(). For a
    /// pen whose attributes differ, it also raises on_ink_drawing_attributes_changed(). Does nothing
    /// when the toolbar has no button for `tool`. CustomPen and CustomTool select the first button
    /// of their kind.
    void set_active_tool(InkToolbarTool tool);
    /// The active tool's button.
    [[nodiscard]] InkToolbarToolButtonControl* active_tool_button() const;
    /// Selects `button`, one of this toolbar's, as set_active_tool() does. Does nothing for a
    /// button of another toolbar.
    void set_active_tool_button(InkToolbarToolButtonControl* button);

    /// Adds a pen after the tools. It draws with what `button.pen` makes of the color and width
    /// picked in its flyout. Returns the new button.
    InkToolbarToolButtonControl* add_custom_pen(InkToolbarCustomButton button);
    /// Adds a tool after the tools. While it is active, the target canvas inks nothing
    /// (InkCanvasInputMode::None) and reports its input through
    /// InkCanvasControl::on_unprocessed_input() for the app to act on. Returns the new button.
    InkToolbarToolButtonControl* add_custom_tool(InkToolbarCustomButton button);
    /// Adds a toggle after the tools and the toggles before it. Returns the new toggle.
    InkToolbarToggleButtonControl* add_custom_toggle(InkToolbarCustomButton button);
    /// The toggles, in order.
    [[nodiscard]] std::vector<InkToolbarToggleButtonControl*> toggle_buttons() const;

    // ---- stencils ----
    /// Adds the stencil button after the tools, before the toggles, and returns it. A second call
    /// returns the one already there.
    InkToolbarStencilButtonControl* add_stencil_button();
    /// The stencil button, or nullptr when there is none.
    [[nodiscard]] InkToolbarStencilButtonControl* stencil_button() const
    {
        return stencil_;
    }
    /// Whether a stencil shows on the target canvas.
    [[nodiscard]] bool stencil_button_checked() const
    {
        return stencil_ != nullptr && stencil_->checked_;
    }
    /// Shows the selected stencil on the target canvas, or hides both. Raises
    /// on_stencil_button_checked_changed() on a change. Does nothing without a stencil button.
    void set_stencil_button_checked(bool checked);
    /// Raised when the stencil button turns on or off, with the stencil it shows.
    void on_stencil_button_checked_changed(Callback<void(const InkToolbarStencilChangedArgs&)> handler)
    {
        on_stencil_checked_changed_ = std::move(handler);
    }

    /// Whether the button for `tool` is shown. False when the toolbar has none.
    [[nodiscard]] bool tool_visible(InkToolbarTool tool) const;
    /// Shows or collapses the button for `tool`. Hiding the active tool leaves it active, so
    /// select another tool first.
    void set_tool_visible(InkToolbarTool tool, bool visible);

    /// What the last active pen draws with. Until a pen has been active, the ballpoint pen's
    /// attributes.
    [[nodiscard]] const InkDrawingAttributes& ink_drawing_attributes() const
    {
        return attributes_;
    }

    // ---- appearance ----
    /// Whether the tools form a row or a column. Horizontal by default.
    [[nodiscard]] Orientation orientation() const;
    /// Lays the tools out in a row or a column.
    void set_orientation(Orientation orientation);
    /// Where the tool flyouts open. Auto, which is below, by default.
    [[nodiscard]] InkToolbarButtonFlyoutPlacement button_flyout_placement() const
    {
        return placement_;
    }
    /// Sets where the tool flyouts open. Applies to the next flyout opened, and moves the chevron
    /// to that side.
    void set_button_flyout_placement(InkToolbarButtonFlyoutPlacement placement);

    // ---- target ----
    /// The canvas the tools drive, or nullptr when none is set or it was destroyed.
    [[nodiscard]] InkCanvasControl* target_ink_canvas() const;
    /// Attaches to `canvas` and sets it up for the active tool at once. nullptr detaches. The
    /// toolbar doesn't own the canvas and lets go of it when the canvas is destroyed, so the canvas
    /// may go first. Until a canvas is set, the tools only change the toolbar's own state.
    void set_target_ink_canvas(InkCanvasControl* canvas);

    /// Clears the target canvas and raises on_erase_all_clicked(), like the eraser flyout's
    /// "Erase all ink" item.
    void erase_all();

    // ---- flyouts ----
    /// Opens the flyout of `tool`'s button. Returns false when it has none, as for a hidden or
    /// missing tool or an eraser without "Erase all ink".
    bool open_flyout(InkToolbarTool tool);
    /// Closes whichever tool flyout is open.
    void close_flyout();
    /// The tool whose flyout is open, or nullopt when none is.
    [[nodiscard]] std::optional<InkToolbarTool> open_flyout_tool() const;

    // ---- events ----
    /// Raised with the new tool.
    void on_active_tool_changed(Callback<void(InkToolbarTool)> handler)
    {
        on_active_tool_changed_ = std::move(handler);
    }
    /// Raised when the active pen's color, width or kind changes.
    void on_ink_drawing_attributes_changed(Callback<void(const InkDrawingAttributes&)> handler)
    {
        on_attributes_changed_ = std::move(handler);
    }
    /// Raised after "Erase all ink" has cleared the canvas.
    void on_erase_all_clicked(Callback<void()> handler)
    {
        on_erase_all_clicked_ = std::move(handler);
    }


    // ---- focus ----
    /// Whether the toolbar has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// A tab stop while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }
    /// The toolbar is one tab stop. Its buttons aren't.
    void collect_focusables(std::vector<Element*>& out) override
    {
        if (focusable())
        {
            out.push_back(this);
        }
    }
    /// The tool under the keyboard cursor while the toolbar is focused. nullptr while the cursor
    /// is on a toggle.
    [[nodiscard]] const InkToolbarToolButtonControl* focused_button() const;
    /// The internal panel.
    void for_each_child(ChildVisitor visit) const override;
    /// The visible tool buttons (through their flyouts) and the toggles, not the internal panel.
    void automation_children(std::vector<Element*>& out) const override;
    /// Exposed to UI Automation as a toolbar, like ui::ToolbarControl.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::ToolBar;
    }

protected:
    // ---- Element ----
    /// The tools in a row or column, 40 DIPs a tool, inside a 2 DIP margin.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the tools.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// The tools, and the focus ring around the tool under the keyboard cursor.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // Enter acts on the tool under the cursor.
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // The arrows along the toolbar's axis.
    [[nodiscard]] bool takes_arrow_key(Key key) const;
    friend class InkToolbarToolButtonControl;
    friend class InkToolbarToggleButtonControl;
    friend class InkToolbarStencilButtonControl;

    struct Slot
    {
        InkToolbarToolButtonControl* button       = nullptr;
        FlyoutControl*               flyout       = nullptr; // null for a tool without a flyout
        Element*                     item         = nullptr; // what sits in the panel: the flyout or the button
        SliderControl*               width_slider = nullptr; // a pen flyout's stroke width slider
        Element*                     stroke_item  = nullptr; // the eraser flyout's three items
        Element*                     point_item   = nullptr;
        Element*                     erase_all    = nullptr;
    };

    InkToolbarToolButtonControl* add_slot(std::unique_ptr<InkToolbarToolButtonControl> button);
    [[nodiscard]] Slot*          slot_of(const InkToolbarToolButtonControl* button);
    void                         activate(Slot& slot);
    bool                         open_slot_flyout(Slot& slot);
    // The keyboard cursor's element: a slot's button or, past the slots, the stencil button or a
    // toggle.
    [[nodiscard]] Element*                 cursor_element(std::size_t index) const;
    void                                   activate_extra(Element* extra);
    void                                   stencil_clicked();
    void                                   stencil_item_clicked(InkToolbarStencilKind kind);
    void                                   apply_stencil_to_target();
    void                                   clicked(InkToolbarToolButtonControl& button);
    void                                   button_attributes_changed(InkToolbarToolButtonControl& button);
    void                                   apply_to_target();
    void                                   sync_flyout_placement();
    [[nodiscard]] Slot*                    slot_for(InkToolbarTool tool);
    [[nodiscard]] const Slot*              slot_for(InkToolbarTool tool) const;
    [[nodiscard]] std::vector<std::size_t> visible_slots() const;
    void                                   move_cursor(int delta, bool to_end);

    std::unique_ptr<StackControl>               panel_;
    std::vector<Slot>                           slots_;
    std::vector<InkToolbarToggleButtonControl*> toggles_; // after the slots in the panel
    InkToolbarStencilButtonControl*             stencil_ = nullptr;
    // What follows the slots in the panel, in order: the stencil button, then the toggles.
    std::vector<Element*>           extras_;
    InkToolbarToolButtonControl*    active_button_ = nullptr;
    InkToolbarTool                  active_        = InkToolbarTool::BallpointPen;
    InkDrawingAttributes            attributes_{};
    InkToolbarButtonFlyoutPlacement placement_ = InkToolbarButtonFlyoutPlacement::Auto;
    std::size_t                     cursor_    = 0; // index into slots_ of the keyboard cursor

    ElementWatch target_; // the InkCanvasControl; goes null when the canvas is destroyed

    Callback<void(InkToolbarTool)>                      on_active_tool_changed_;
    Callback<void(const InkDrawingAttributes&)>         on_attributes_changed_;
    Callback<void()>                                    on_erase_all_clicked_;
    Callback<void(const InkToolbarStencilChangedArgs&)> on_stencil_checked_changed_;

protected:
    void enabled_changed() override;
    /// Puts the keyboard cursor on the active tool when focus arrives.
    void focus_changed(bool focused) override;
    /// Left and Right, or Up and Down in a vertical toolbar, move between the visible tools and
    /// toggles. Home and End move to the ends. Space and Enter act on the one under the cursor as a
    /// click does.
    bool key_override(const KeyEvent& e) override;

protected:
    /// Around the tool or toggle under the keyboard cursor.
    [[nodiscard]] Rect focus_ring_rect(const Theme& theme) const override;
};

/// A row of ink tools for an InkCanvas. Builds a ui::InkToolbarControl, whose description lists
/// the tools and their flyouts.
///
/// `.target` names the canvas by its handle, and the canvas can be built before or after the
/// toolbar. Until the canvas exists, the tools only change the toolbar's own state. The active
/// tool's attributes reach the canvas once it is linked. `.active_tool = tool` is two-way.
///
/// Example:
/// @code
/// ui::Control<ui::InkCanvas>       canvas;
/// ui::Property<ui::InkToolbarTool> tool{ui::InkToolbarTool::BallpointPen};
///
/// window.set_content(ui::VerticalStack{.spacing = 8, .content = {
///     ui::InkToolbar{
///         .tools                = {ui::InkToolbarTool::BallpointPen, ui::InkToolbarTool::Highlighter,
///                                  ui::InkToolbarTool::Eraser},
///         .active_tool          = tool,
///         .target               = canvas, // the canvas draws with the active tool
///         .horizontal_alignment = ui::Alignment::Leading,
///         .accessibility        = {.name = "Drawing tools"},
///     },
///     ui::Border{.background = ui::colors::White,
///                .height     = 280.0f,
///                .content    = ui::InkCanvas{.control = canvas}},
/// }});
/// @endcode
struct InkToolbar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<InkToolbar>` points at.
    using control_type = InkToolbarControl;

    // ---- properties ----
    /// The tools, in order. The first is active unless `active_tool` names another.
    std::vector<InkToolbarTool> tools{InkToolbarControl::AllTools.begin(), InkToolbarControl::AllTools.end()};
    /// Pens of the app's own, after the tools (InkToolbarControl::add_custom_pen()).
    std::vector<InkToolbarCustomButton> custom_pens{};
    /// Tools of the app's own, after the custom pens (InkToolbarControl::add_custom_tool()).
    std::vector<InkToolbarCustomButton> custom_tools{};
    /// Toggles of the app's own, after the tools (InkToolbarControl::add_custom_toggle()).
    std::vector<InkToolbarCustomButton> custom_toggles{};
    /// The stencil button, showing a ruler or a protractor (InkToolbarControl::add_stencil_button()).
    bool stencil_button = false;
    /// The selected tool. Two-way with `ui::bind(prop)`. A tool not in `tools` is ignored.
    TwoWayField<InkToolbarTool> active_tool{InkToolbarTool::BallpointPen};
    /// A row, the default, or a column.
    Binding<Orientation> orientation{Orientation::Horizontal};
    /// Where the tool flyouts open.
    Binding<InkToolbarButtonFlyoutPlacement> button_flyout_placement{InkToolbarButtonFlyoutPlacement::Auto};
    /// The canvas the tools act on, by its handle (InkToolbarControl::set_target_ink_canvas()).
    ControlLink<InkCanvas> target{};

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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The new tool, from a click or the keyboard. A write to `active_tool` doesn't run it.
    Callback<void(InkToolbarTool)> on_active_tool_changed{};
    /// The active pen's attributes changed.
    Callback<void(const InkDrawingAttributes&)> on_ink_drawing_attributes_changed{};
    /// "Erase all ink" cleared the canvas.
    Callback<void()> on_erase_all_clicked{};
    /// The stencil button went on or off, with the stencil it shows.
    Callback<void(const InkToolbarStencilChangedArgs&)> on_stencil_button_checked_changed{};
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
    ControlOut<InkToolbar> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::InkToolbarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
