// SPDX-License-Identifier: MIT
#include "ControlChrome.hpp"

#include "winui/ui/themes/Theme.hpp"

#include <memory>

namespace ui::detail
{

const ControlChrome& default_control_chrome()
{
    static const ControlChrome chrome{};
    return chrome;
}

ControlChrome& edit_controls(Theme& theme)
{
    std::shared_ptr<const ControlChrome>& chrome = ControlChromeAccess::get(theme);
    if (!chrome)
    {
        chrome = std::make_shared<ControlChrome>();
    }
    else if (chrome.use_count() > 1)
    {
        chrome = std::make_shared<ControlChrome>(*chrome);
    }
    // Every record is created here, non-const, and this theme is now its only owner.
    return const_cast<ControlChrome&>(*chrome); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

} // namespace ui::detail
