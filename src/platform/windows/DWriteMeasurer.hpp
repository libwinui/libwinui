// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "DWriteCache.hpp"
#include "Win32String.hpp"
#include "winui/core/PerfCounters.hpp"
#include "winui/render/Text.hpp"

#include <dwrite.h>
#include <dwrite_1.h>
#include <string>
#include <string_view>
#include <wrl/client.h>

#include <cmath>

namespace ui::win32
{

using Microsoft::WRL::ComPtr;

// Decorations and character spacing need a layout, not a format. Shared with
// D2DRenderer::draw_text() so measurement and drawing agree.
inline void apply_run_format(IDWriteTextLayout* layout, const TextStyle& style, UINT32 length)
{
    const DWRITE_TEXT_RANGE all{0, length};
    if (style.underline)
    {
        layout->SetUnderline(TRUE, all);
    }
    if (style.strikethrough)
    {
        layout->SetStrikethrough(TRUE, all);
    }
    if (style.character_spacing != 0)
    {
        ComPtr<IDWriteTextLayout1> layout1;
        if (SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(&layout1))))
        {
            layout1->SetCharacterSpacing(0.0f, style.character_spacing_dips(), 0.0f, all);
        }
    }
}

// Whether a run needs apply_run_format(), i.e. a layout rather than a plain DrawText.
[[nodiscard]] inline bool needs_run_format(const TextStyle& style)
{
    return style.underline || style.strikethrough || style.character_spacing != 0;
}

// The UTF-8 is widened at the call; a measurement is a whole string, so no offset crosses the seam.
// Results come from the process-wide cache in DWriteCache.hpp, which also holds the formats the
// renderer draws with, so measuring and drawing agree.
class DWriteMeasurer final : public TextMeasurer
{
public:
    explicit DWriteMeasurer(IDWriteFactory* dwrite) : dwrite_(dwrite)
    {
    }

    [[nodiscard]] TextMetrics measure(std::string_view text, const TextStyle& style) const override
    {
        detail::count(detail::PerfCounter::TextMeasurements);
        return line(text, style);
    }

    [[nodiscard]] TextMetrics measure_wrapped(std::string_view text, const TextStyle& style, float max_width) const override
    {
        detail::count(detail::PerfCounter::TextMeasurements);
        if (style.wrapping == TextWrapping::NoWrap || !(max_width > 0.0f) || !std::isfinite(max_width))
        {
            return line(text, style);
        }
        // Text that fits on one line measures exactly as measure() does, so switching wrapping on
        // never moves a label that did not need it.
        const TextMetrics single = line(text, style);
        if (single.width <= max_width && text.find('\n') == std::string_view::npos)
        {
            return single;
        }
        return cached_text_metrics(text,
                                   style,
                                   max_width,
                                   [&]
                                   {
                                       return layout_metrics(text,
                                                             style,
                                                             style.wrapping == TextWrapping::WrapWholeWords
                                                                 ? DWRITE_WORD_WRAPPING_WHOLE_WORD
                                                                 : DWRITE_WORD_WRAPPING_WRAP,
                                                             max_width);
                                   });
    }

private:
    [[nodiscard]] TextMetrics line(std::string_view text, const TextStyle& style) const
    {
        return cached_text_metrics(text,
                                   style,
                                   0.0f,
                                   [&] { return layout_metrics(text, style, DWRITE_WORD_WRAPPING_NO_WRAP, 1e6f); });
    }

    [[nodiscard]] TextMetrics layout_metrics(std::string_view     text,
                                             const TextStyle&     style,
                                             DWRITE_WORD_WRAPPING wrapping,
                                             float                max_width) const
    {
        const ComPtr<IDWriteTextFormat> fmt = cached_text_format(dwrite_, style);
        if (!fmt)
        {
            return {};
        }
        const std::wstring        s = widen(text);
        ComPtr<IDWriteTextLayout> layout;
        detail::count(detail::PerfCounter::TextLayoutsCreated);
        if (FAILED(dwrite_->CreateTextLayout(s.c_str(),
                                             static_cast<UINT32>(s.size()),
                                             fmt.Get(),
                                             max_width,
                                             /*maxHeight*/ 1e6f,
                                             &layout)))
        {
            return {};
        }
        // The shared format wraps as the style says; a measurement asks for its own wrapping,
        // and measures from the leading edge whatever the alignment.
        layout->SetWordWrapping(wrapping);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        apply_run_format(layout.Get(), style, static_cast<UINT32>(s.size()));

        DWRITE_TEXT_METRICS m{};
        if (FAILED(layout->GetMetrics(&m)))
        {
            return {};
        }
        DWRITE_LINE_METRICS first{};
        UINT32              lines = 0;
        // E_NOT_SUFFICIENT_BUFFER for more than one line still fills the first.
        (void)layout->GetLineMetrics(&first, 1, &lines);
        // A broken line's trailing space hangs past the layout width rather than taking room on
        // it, so a wrapped block is as wide as its widest line without one.
        return {.width      = wrapping == DWRITE_WORD_WRAPPING_NO_WRAP ? m.widthIncludingTrailingWhitespace : m.width,
                .height     = m.height,
                .baseline   = first.baseline,
                .line_count = static_cast<int>(m.lineCount)};
    }

    IDWriteFactory* dwrite_ = nullptr;
};

} // namespace ui::win32

#endif // _WIN32
