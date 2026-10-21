// SPDX-License-Identifier: MIT
#include "winui/render/IconSource.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Text.hpp"

#include <algorithm>
#include <bit>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui
{

struct IconSource::TintCache
{
    std::mutex mutex;
    Bitmap     tinted;
    Color      tinted_for{};
};

Size IconSource::GlyphBox::load() const
{
    const std::uint64_t bits = bits_.load(std::memory_order_relaxed);
    return Size{std::bit_cast<float>(static_cast<std::uint32_t>(bits)),
                std::bit_cast<float>(static_cast<std::uint32_t>(bits >> 32))};
}

void IconSource::GlyphBox::store(Size size)
{
    const std::uint64_t bits = std::uint64_t{std::bit_cast<std::uint32_t>(size.width)} |
                               (std::uint64_t{std::bit_cast<std::uint32_t>(size.height)} << 32);
    bits_.store(bits, std::memory_order_relaxed);
}

IconSource IconSource::path(PathGeometry data)
{
    IconSource source;
    source.content_ = std::move(data);
    return source;
}

IconSource IconSource::bitmap(Bitmap image, bool monochrome)
{
    IconSource source;
    source.content_ = BitmapContent{std::move(image), monochrome, std::make_shared<TintCache>()};
    return source;
}

IconSource IconSource::image(ImageSource picture)
{
    IconSource source;
    source.content_ = Picture{std::move(picture)};
    return source;
}

Size IconSource::measure(const TextMeasurer& measurer) const
{
    if (const IconGlyph* g = glyph())
    {
        TextStyle style;
        style.font_family = g->font_family != nullptr ? g->font_family : "Segoe Fluent Icons";
        style.font_size   = g->size;
        glyph_box_.store(measurer.measure(encode_utf8(g->codepoint), style));
    }
    return natural_size();
}

Size IconSource::natural_size() const
{
    if (const IconGlyph* g = glyph())
    {
        const Size box = glyph_box_.load();
        return box.width > 0.0f && box.height > 0.0f ? box : Size{g->size, g->size};
    }
    if (const PathGeometry* geometry = std::get_if<PathGeometry>(&content_))
    {
        // A path icon's measure: the geometry from the origin to its bottom-right corner.
        const Rect b = geometry->bounds();
        return Size{std::max(0.0f, b.right()), std::max(0.0f, b.bottom())};
    }
    if (const BitmapContent* bitmap = std::get_if<BitmapContent>(&content_))
    {
        return Size{static_cast<float>(bitmap->image.width()), static_cast<float>(bitmap->image.height())};
    }
    if (const Picture* picture = std::get_if<Picture>(&content_))
    {
        return picture->source.natural_size();
    }
    return Size{};
}

Size IconSource::slot_size(float size) const
{
    if (size > 0.0f)
    {
        return Size{size, size};
    }
    if (const IconGlyph* g = glyph())
    {
        return Size{g->size, g->size};
    }
    return natural_size();
}

void IconSource::draw_in_slot(Renderer& renderer, const Rect& box, const Color& foreground, float size, std::string_view symbol_font) const
{
    if (const IconGlyph* g = glyph())
    {
        const bool symbol = g->font_family.view() == IconGlyph{}.font_family.view();
        draw_icon(renderer, *g, box, foreground, size, symbol ? symbol_font : std::string_view{});
        return;
    }
    if (size <= 0.0f)
    {
        draw(renderer, box, foreground);
        return;
    }
    draw(renderer, Rect{box.x + (box.width - size) * 0.5f, box.y + (box.height - size) * 0.5f, size, size}, foreground);
}

void IconSource::draw(Renderer& renderer, const Rect& box, const Color& foreground) const
{
    const Size natural = natural_size();
    if (empty() || natural.width <= 0.0f || natural.height <= 0.0f || box.width <= 0.0f || box.height <= 0.0f)
    {
        return;
    }
    // ScaleToFit, Stretch Uniform: scaled to fit and centered in the slot.
    const float scale = std::min(box.width / natural.width, box.height / natural.height);
    const Size  size{natural.width * scale, natural.height * scale};
    const Rect rect{box.x + (box.width - size.width) * 0.5f, box.y + (box.height - size.height) * 0.5f, size.width, size.height};

    if (const IconGlyph* g = glyph())
    {
        draw_icon(renderer, *g, rect, foreground, g->size * scale);
        return;
    }
    if (const PathGeometry* geometry = std::get_if<PathGeometry>(&content_))
    {
        DrawGeometry cmd;
        cmd.geometry  = geometry->transformed(Matrix{scale, 0.0f, 0.0f, scale, rect.x, rect.y});
        cmd.fill      = foreground;
        cmd.brush_box = rect;
        renderer.draw_geometry(cmd);
        return;
    }
    if (const BitmapContent* bitmap = std::get_if<BitmapContent>(&content_))
    {
        const Rect source{0.0f, 0.0f, natural.width, natural.height};
        if (!bitmap->monochrome)
        {
            renderer.draw_image_rect(bitmap->image, source, rect);
            return;
        }
        // A monochrome bitmap icon takes the foreground color.
        TintCache&                        cache = *bitmap->tint;
        const std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.tinted.empty() || !(cache.tinted_for == foreground))
        {
            Bitmap     tinted = bitmap->image.converted(PixelFormat::Rgba8);
            auto       bytes  = tinted.bytes();
            const auto r      = static_cast<std::uint8_t>(std::lround(foreground.r * 255.0f));
            const auto g      = static_cast<std::uint8_t>(std::lround(foreground.g * 255.0f));
            const auto b      = static_cast<std::uint8_t>(std::lround(foreground.b * 255.0f));
            for (std::size_t i = 0; i + 3 < bytes.size(); i += 4)
            {
                bytes[i + 0] = r;
                bytes[i + 1] = g;
                bytes[i + 2] = b;
                bytes[i + 3] = static_cast<std::uint8_t>(std::lround(bytes[i + 3] * foreground.a));
            }
            cache.tinted     = std::move(tinted);
            cache.tinted_for = foreground;
        }
        renderer.draw_image_rect(cache.tinted, source, rect);
        return;
    }
    if (const Picture* picture = std::get_if<Picture>(&content_))
    {
        if (const Bitmap* image = picture->source.bitmap())
        {
            renderer.draw_image_rect(*image, Rect{0.0f, 0.0f, natural.width, natural.height}, rect);
        }
        else if (const AnimatedImage* animated = picture->source.animated();
                 animated != nullptr && !animated->frames.empty())
        {
            renderer.draw_image_rect(animated->frames.front(), Rect{0.0f, 0.0f, natural.width, natural.height}, rect);
        }
        else if (const SvgImage* svg = picture->source.svg())
        {
            renderer.draw_svg(*svg, rect);
        }
    }
}

} // namespace ui
