// SPDX-License-Identifier: MIT
// <windows.h> before the umbrella header, the way an app that never heard of NOMINMAX includes
// them: the installed headers must not trip over its min and max macros. The build defines
// NOMINMAX and WIN32_LEAN_AND_MEAN for this target, so they are taken back here.
#ifdef _WIN32

#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef max
#error "<windows.h> was expected to define the max macro here"
#endif

#include "winui/winui.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("winui.hpp compiles after windows.h without NOMINMAX", "[build]")
{
    ui::SliderControl slider;
    slider.set_range(0.0, 10.0);
    REQUIRE((slider.max)() == 10.0);
}

#endif // _WIN32
