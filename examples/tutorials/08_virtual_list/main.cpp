// SPDX-License-Identifier: MIT
// libwinui tutorial: long lists.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <algorithm>
#include <format>
#include <string>

#include <cstddef>
// --8<-- [end:includes]

namespace
{
// --8<-- [start:model]
struct Order
{
    std::size_t id = 0;
    std::string customer;
    double      total = 0.0;
};

void add_orders(ui::ObservableVector<Order>& orders, std::size_t count)
{
    const char* customers[] = {"Contoso", "Fabrikam", "Northwind", "Tailspin", "Wingtip"};

    for (std::size_t n = 0; n < count; ++n)
    {
        const std::size_t i = orders.size();
        orders.push_back({i + 1, customers[i % 5], static_cast<double>((i * 37) % 1000) + 0.99});
    }
}
// --8<-- [end:model]

// --8<-- [start:row]
ui::Spec order_row(const Order& order)
{
    return ui::HorizontalStack{
        .spacing = 16,
        .content =
            {
                ui::Text{.text = std::format("#{}", order.id), .width = 90, .vertical_alignment = ui::Alignment::Center},
                ui::Text{.text = order.customer, .width = 140, .vertical_alignment = ui::Alignment::Center},
                ui::Text{.text = std::format("{:.2f}", order.total), .vertical_alignment = ui::Alignment::Center},
            },
    };
}
// --8<-- [end:row]

} // namespace

int main()
{
    // --8<-- [start:state]
    // Declared before the window, so the collection outlives the list that shows it.
    ui::ObservableVector<Order> orders;
    add_orders(orders, 100'000);

    ui::Control<ui::ListView<Order>> list;
    ui::Property<std::string>        info{std::format("{} orders", orders.size())};

    auto report = [&]
    { info = std::format("{} orders, {} rows alive", orders.size(), list->repeater()->realized_count()); };
    // --8<-- [end:state]

    // --8<-- [start:list]
    ui::Spec order_list = ui::ListView<Order>{
        .items       = orders,
        .item_height = 36,
        .control     = list,
        .item        = order_row,
    };
    // --8<-- [end:list]

    // --8<-- [start:count]
    auto add = [&]
    {
        add_orders(orders, 1'000);
        report();
    };

    auto remove = [&]
    {
        const std::size_t count = std::min<std::size_t>(1'000, orders.size());
        orders.erase(orders.size() - count, count);
        report();
    };
    // --8<-- [end:count]

    ui::Window window{{.title = "Long lists", .size = {640, 720}}};

    // --8<-- [start:layout]
    window.set_content(ui::VerticalStack{
        .spacing = 12,
        .margin  = 16,
        .content =
            {
                ui::HorizontalStack{
                    .spacing = 8,
                    .content =
                        {
                            ui::Button{.text = "Add 1,000", .on_click = add},
                            ui::Button{.text = "Remove 1,000", .on_click = remove},
                            ui::Button{.text = "Top", .on_click = [&] { list->scroll_into_view(0); }},
                            ui::Button{.text = "End",
                                       .on_click =
                                           [&]
                                       {
                                           if (!orders.empty())
                                           {
                                               list->scroll_into_view(orders.size() - 1);
                                           }
                                       }},
                            ui::Button{.text = "Count live rows", .on_click = report},
                        },
                },
                ui::Text{.text = info},
                ui::Fill{.content = std::move(order_list)},
            },
    });
    // --8<-- [end:layout]

    return ui::run(window);
}
