// SPDX-License-Identifier: MIT
// The tests draw every theme variant, so the period packs are linked in before any test runs.
#include "winui/ui/themes/ThemeSource.hpp"

namespace
{
const bool era_themes = []
{
    ui::enable_era_themes();
    return true;
}();
} // namespace
