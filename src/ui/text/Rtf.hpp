// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/text/RichEditBox.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ui::detail
{

/// The document `rtf` holds, as spans with every format spelled out (no field left to the
/// control): the default font's name and size included. Font sizes are DIPs (a point is 4/3 of
/// one). One trailing paragraph mark, the one every RTF writer ends on, is dropped. Returns
/// nothing for text that does not start with `{\rtf`.
[[nodiscard]] std::vector<RichTextSpan> parse_rtf(std::string_view rtf, bool* is_rtf = nullptr);

/// `spans` as RTF. Formats that leave the font to the control are written in `base_family` and
/// `base_size`, colors left to it as the automatic color.
[[nodiscard]] std::string write_rtf(const std::vector<RichTextSpan>& spans, const std::string& base_family, float base_size);

} // namespace ui::detail
