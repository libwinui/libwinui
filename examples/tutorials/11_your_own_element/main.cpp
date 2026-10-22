// SPDX-License-Identifier: MIT
// libwinui tutorial: your own element.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>
// --8<-- [end:includes]

namespace app
{
// --8<-- [start:element]
class SwatchControl final : public ui::Element, private ui::InvokeProvider
{
public:
    SwatchControl(ui::Color color, std::string name) : color_(color), name_(std::move(name))
    {
    }

    // --8<-- [start:setters]
    [[nodiscard]] ui::Color color() const
    {
        return color_;
    }
    void set_color(ui::Color color)
    {
        if (color == color_)
        {
            return;
        }
        color_ = color;
        mark_dirty(ui::DirtyFlags::Visual);
    }
    void set_selected(bool selected)
    {
        if (selected == selected_)
        {
            return;
        }
        selected_ = selected;
        mark_dirty(ui::DirtyFlags::Visual);
    }
    void on_click(ui::Callback<void()> handler)
    {
        on_click_ = std::move(handler);
    }
    // --8<-- [end:setters]

    // --8<-- [start:focus]
    [[nodiscard]] bool focusable() const override
    {
        return true;
    }

protected:
    // Space and Enter click; any other key is left to the parents, the shortcuts and focus moves.
    bool key_override(const ui::KeyEvent& e) override
    {
        if (e.key != ui::Key::Space && e.key != ui::Key::Enter)
        {
            return false;
        }
        if (e.action == ui::KeyAction::Down)
        {
            key_down_ = true;
        }
        else if (key_down_)
        {
            key_down_ = false;
            click();
        }
        return true;
    }

public:
    // --8<-- [end:focus]

    // --8<-- [start:automation]
    [[nodiscard]] ui::AutomationRole automation_role() const override
    {
        return ui::AutomationRole::Button;
    }

protected:
    // What a screen reader can do with it: invoke it, as a click would.
    void automation_providers(ui::AutomationProviders& out) override
    {
        out.invoke = this;
    }

    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return name_;
    }

private:
    bool invoke() override
    {
        click();
        return true;
    }
    // --8<-- [end:automation]

protected:
    // --8<-- [start:core]
    [[nodiscard]] ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
    {
        return {40.0f, 40.0f};
    }

    void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    {
        const ui::Theme& theme = context.theme;
        // A disabled swatch (its own flag or an ancestor's) fades, as built-in controls do.
        const ui::Color fill = is_enabled() ? color_ : color_.with_alpha(color_.a * 0.4f);
        renderer.fill_rounded_rect(bounds(), theme.corner_radius, fill);
        if (selected_)
        {
            renderer.stroke_rounded_rect(bounds(), theme.corner_radius, theme.colors().text, 2.0f);
        }
    }

    bool pointer_override(const ui::PointerEvent& e) override
    {
        if (e.action == ui::PointerAction::Down && e.button == ui::PointerButton::Right)
        {
            show_details();
            return true;
        }
        if (e.button != ui::PointerButton::Left)
        {
            return false;
        }
        if (e.action == ui::PointerAction::Down)
        {
            pointer_down_ = true; // a handled press holds the pointer until it is released
            return true;
        }
        if (e.action == ui::PointerAction::Up)
        {
            const bool was_down = pointer_down_;
            pointer_down_       = false;
            if (was_down && bounds().contains(e.position))
            {
                click();
            }
            return true;
        }
        return false;
    }
    // --8<-- [end:core]

private:
    // --8<-- [start:popup]
    // A right-click floats the color's name and code under the swatch. The swatch owns the
    // content; the window places it, draws it with a shadow over everything else, and closes it on
    // a click outside or Escape.
    void show_details()
    {
        if (popup_open())
        {
            return;
        }
        const auto channel = [](float v) { return static_cast<int>(std::lround(v * 255.0f)); };
        details_ =
            ui::Border{
                .background    = [](const ui::Theme& theme) { return theme.colors().overlay; },
                .corner_radius = 8,
                .padding       = ui::Symmetric{.horizontal = 12, .vertical = 8},
                .content       = ui::Text{.text = std::format("{}  #{:02X}{:02X}{:02X}",
                                                              name_,
                                                              channel(color_.r),
                                                              channel(color_.g),
                                                              channel(color_.b))},
            }
                .build();
        open_popup({
            .content             = details_.get(),
            .placement           = ui::PopupPlacement::Below,
            .placement_alignment = ui::PopupPlacementAlignment::Center,
        });
    }
    // --8<-- [end:popup]

    void click()
    {
        if (on_click_)
        {
            on_click_();
        }
    }

    ui::Color                    color_;
    std::string                  name_;
    bool                         selected_     = false;
    bool                         pointer_down_ = false;
    bool                         key_down_     = false;
    ui::Callback<void()>         on_click_;
    std::unique_ptr<ui::Element> details_;
};
// --8<-- [end:element]


// --8<-- [start:spec]
struct Swatch
{
    using control_type = SwatchControl;

    // ---- properties ----
    ui::Color         color = ui::colors::Black;
    std::string       name{};
    ui::Binding<bool> selected{false};

    // ---- appearance / layout ----
    ui::Style                                 style{};
    ui::Binding<std::optional<ui::Thickness>> margin{};
    ui::Binding<std::optional<float>>         width{};
    ui::Binding<std::optional<float>>         height{};
    ui::Binding<std::optional<float>>         min_width{};
    ui::Binding<std::optional<float>>         min_height{};
    ui::Binding<std::optional<float>>         max_width{};
    ui::Binding<std::optional<float>>         max_height{};
    ui::Binding<std::optional<ui::Alignment>> horizontal_alignment{};
    ui::Binding<std::optional<ui::Alignment>> vertical_alignment{};
    ui::Binding<bool>                         visible{true};
    ui::Binding<bool>                         enabled{true};
    ui::Accessibility                         accessibility{};

    // ---- behavior ----
    ui::Action on_click{};

    // ---- identity ----
    ui::ControlOut<Swatch> control{};

    [[nodiscard]] std::unique_ptr<ui::Element> build() &&
    {
        auto node = std::make_unique<SwatchControl>(color, std::move(name));
        ui::apply(selected, *node, [raw = node.get()](bool on) { raw->set_selected(on); });
        ui::apply(enabled, *node, [raw = node.get()](bool on) { raw->set_enabled(on); });
        ui::apply_common(*this, *node);
        if (on_click)
        {
            node->on_click(std::move(on_click));
        }
        ui::publish_control(control, node.get());
        return node;
    }
};
// --8<-- [end:spec]

} // namespace app

int main()
{
    // --8<-- [start:use]
    const std::vector<std::pair<ui::Color, std::string>> palette = {
        {ui::Color::from_argb(0xFF0078D4), "Blue"},
        {ui::Color::from_argb(0xFF107C10), "Green"},
        {ui::Color::from_argb(0xFFC42B1C), "Red"},
        {ui::Color::from_argb(0xFFFFB900), "Yellow"},
    };
    ui::Property<std::size_t> picked{0};
    ui::Property<bool>        locked{false};
    ui::Control<app::Swatch>  first;

    auto swatch = [&](std::size_t i)
    {
        return app::Swatch{
            .color    = palette[i].first,
            .name     = palette[i].second,
            .selected = ui::bind(picked, [i](std::size_t p) { return p == i; }),
            .on_click = [&picked, i] { picked = i; },
            .control  = i == 0 ? &first : nullptr,
        };
    };

    ui::Window window{{.title = "Your own element", .size = {480, 320}}};

    window.set_content(ui::VerticalStack{
        .spacing         = 16,
        .child_alignment = ui::Alignment::Leading,
        .margin          = 24,
        .content =
            {
                ui::HorizontalStack{
                    .spacing = 12,
                    .enabled = ui::bind(locked, [](bool on) { return !on; }),
                    .content = {swatch(0), swatch(1), swatch(2), swatch(3)},
                },
                ui::Text{.text = ui::bind(picked, [&palette](std::size_t p) { return "Picked: " + palette[p].second; })},
                ui::CheckBox{.text = "Lock the palette", .checked = ui::bind(locked)},
                ui::Button{
                    .text = "Darken the first swatch",
                    .on_click =
                        [&]
                    {
                        const ui::Color c = first->color();
                        first->set_color(ui::Color{c.r * 0.8f, c.g * 0.8f, c.b * 0.8f, c.a});
                    },
                },
            },
    });
    // --8<-- [end:use]

    return ui::run(window);
}
