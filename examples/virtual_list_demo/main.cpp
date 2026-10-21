// SPDX-License-Identifier: MIT
#include <winui/winui.hpp>

#include <format>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include <cstddef>

int main()
{
    constexpr std::size_t ItemCount = 100'000;

    // The items are their own positions; the click counts live beside the list, because a row
    // that scrolls out of view is destroyed and takes whatever it kept with it.
    std::vector<std::size_t> items(ItemCount);
    std::iota(items.begin(), items.end(), std::size_t{0});
    std::vector<int> clicks(ItemCount, 0);

    auto label_for = [](std::size_t index, int count) { return std::format("Item {} — {} clicks", index, count); };

    ui::Window window{{.title = "libwinui — 100,000-item list (scroll: mouse wheel)", .size = {480, 640}}};

    window.set_content(ui::ListView<std::size_t>{
        .items          = std::move(items),
        .item_height    = 36.0f,
        .selection_mode = ui::SelectionMode::None,
        // Runs once per realized row, not once per item.
        .item = [&clicks, label_for](const std::size_t& index) -> ui::Spec
        {
            // A row that relabels itself needs a pointer to the node the spec is still building,
            // and .control only publishes it once build() returns. So: declare, then wire.
            ui::Control<ui::Button>      row;
            std::unique_ptr<ui::Element> node = ui::build(ui::Button{
                .text    = label_for(index, clicks[index]),
                .control = row,
            });
            row->on_click([&clicks, label_for, index, row] { row->set_text(label_for(index, ++clicks[index])); });
            return node;
        },
    });

    return ui::run(window);
}
