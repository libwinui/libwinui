// SPDX-License-Identifier: MIT
#include "Fixture.hpp"

namespace bench
{

ui::testing::OffscreenCapture& offscreen()
{
    static ui::testing::OffscreenCapture capture;
    return capture;
}

} // namespace bench
