// SPDX-License-Identifier: MIT
#include "winui/ui/Printing.hpp"

#include "../platform/Backend.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <utility>

namespace ui
{

namespace
{
// UI-thread state, like the message box host (MessageBox.cpp).
PrintHost* g_installed_print_host = nullptr;

PrintResult run(const Window* owner, PrintJob job)
{
    if (!job.page_count || !job.draw_page)
    {
        return PrintResult::Canceled;
    }
    PrintHost* const host = g_installed_print_host;
    return host != nullptr ? host->print(owner, job) : detail::platform_print(owner, job);
}
} // namespace

PrintResult print(const Window& owner, PrintJob job)
{
    return run(&owner, std::move(job));
}

PrintResult print(PrintJob job)
{
    return run(nullptr, std::move(job));
}

void draw_element(Renderer& renderer, Element& content, const Rect& area, const PrintContext& context)
{
    (void)content.measure(Size{area.width, area.height}, context.measurer, context.theme);
    content.arrange(area);
    content.render(renderer, context.theme);
}

void detail::install_print_host(PrintHost* host)
{
    g_installed_print_host = host;
}

PrintHost* detail::installed_print_host()
{
    return g_installed_print_host;
}

} // namespace ui
