// SPDX-License-Identifier: MIT
// DirectWrite objects that outlive a frame, shared by the measurer and the renderer of every window:
// text formats keyed by the style fields a format carries, text layouts keyed by the string, its
// style and the box it is laid out in, and measured metrics keyed by the string, style and width.
// One cache per process, locked, since layout workers measure in parallel.
//
// Entries are aged by frames: next_frame() (called after every frame a window draws) drops the
// layouts and metrics not used for a while once the cache has grown past its soft limit, so
// scrolling through a long list does not keep every row's text alive.
#pragma once
#ifdef _WIN32

#include "winui/render/Text.hpp"

#include <dwrite.h>
#include <string_view>
#include <wrl/client.h>

namespace ui::win32
{

using Microsoft::WRL::ComPtr;

// A text format for `style`, created with `dwrite` the first time it is asked for. Carries the
// style's family, weight, italic, size, wrapping, line height and both alignments; a format is
// never changed after it is made, so one may be shared.
[[nodiscard]] ComPtr<IDWriteTextFormat> cached_text_format(IDWriteFactory* dwrite, const TextStyle& style);

// A text layout of `text` in `style` within a `max_width` x `max_height` box, with the
// decorations and character spacing applied.
[[nodiscard]] ComPtr<IDWriteTextLayout> cached_text_layout(
    IDWriteFactory*  dwrite,
    std::string_view text,
    const TextStyle& style,
    float            max_width,
    float            max_height);

// What `measure` returns for `text` in `style` wrapped at `max_width` (0: unwrapped), from the
// cache or, the first time, from `measure` itself.
template <typename Measure>
[[nodiscard]] TextMetrics cached_text_metrics(std::string_view text, const TextStyle& style, float max_width, Measure&& measure);

// Ages every entry by a frame; see the file comment.
void next_text_frame();

// cached_text_metrics()'s lookup and store.
[[nodiscard]] bool find_text_metrics(std::string_view text, const TextStyle& style, float max_width, TextMetrics& out);
void store_text_metrics(std::string_view text, const TextStyle& style, float max_width, const TextMetrics& metrics);

template <typename Measure>
TextMetrics cached_text_metrics(std::string_view text, const TextStyle& style, float max_width, Measure&& measure)
{
    TextMetrics metrics{};
    if (find_text_metrics(text, style, max_width, metrics))
    {
        return metrics;
    }
    metrics = measure();
    store_text_metrics(text, style, max_width, metrics);
    return metrics;
}

} // namespace ui::win32

#endif // _WIN32
