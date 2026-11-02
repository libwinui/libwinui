// SPDX-License-Identifier: MIT
#include <string>

import winui.testing;

#include "Fixture.hpp"

namespace bench
{

ui::testing::OffscreenCapture& offscreen()
{
    static ui::testing::OffscreenCapture capture;
    return capture;
}

} // namespace bench
