// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/render/Text.hpp"

#include <atomic>
#include <concepts>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>

#include <cstdint>

namespace ui
{

/// An icon as a value: a glyph, a symbol, a path or a picture, held by whatever shows an icon
/// without hosting it as a child element.
///
/// Every control that shows an icon takes one in its `icon` field, and `ui::Icon` shows one on its
/// own. Copyable and cheap to pass around. A Symbol, an IconGlyph and an ImageSource (a Bitmap, an
/// SVG document or an animated image) convert to it implicitly; a path or a monochrome bitmap is
/// made with path() and bitmap(). A default-constructed IconSource is empty and draws nothing.
///
/// A control draws it with draw_in_slot(): a glyph at the slot's glyph size, anything else fitted
/// into the same square, so every kind of icon takes the room a glyph would.
///
/// Example:
/// @code
/// ui::IconSource save_icon = ui::Symbol::Save;                            // a symbol
/// ui::IconSource share     = ui::IconGlyph{0xE72D};                       // a font glyph
/// ui::IconSource sigma     = ui::IconGlyph{0x03A3, "Candara", 20.0f};     // another font
/// ui::IconSource arrow     = ui::IconSource::path(*ui::PathGeometry::parse("F1 M 20,20L 24,10L 24,24L 5,24"));
/// ui::IconSource logo      = ui::IconSource::bitmap(ui::Bitmap::read_png("logo.png"));
///
/// window.set_content(ui::CommandBar{
///     .primary = {
///         ui::AppBarButton{.text = "Save", .icon = save_icon, .on_click = [] { save(); }},
///         ui::AppBarButton{.text = "Share", .icon = share},
///     },
/// });
///
/// // Drawing one yourself, from a render_override():
/// share.measure(measurer); // a glyph's box is measured (and cached) before draw()
/// share.draw(renderer, ui::Rect{0, 0, 20, 20}, theme.palette.text_fill_primary);
/// @endcode
class IconSource
{
public:
    /// No icon (a null IconSource).
    IconSource() = default;

    /// One glyph at `glyph.size` in `glyph.font_family`.
    IconSource(IconGlyph glyph) : content_(glyph) // NOLINT(google-explicit-constructor): a glyph is an icon
    {
    }
    /// The symbol's Segoe Fluent Icons glyph at 20px.
    IconSource(Symbol symbol) : content_(symbol_glyph(symbol)) // NOLINT(google-explicit-constructor)
    {
    }

    /// The picture as it is, from an ImageSource or anything that converts to one (a Bitmap, an SVG
    /// document, an animated image); the same as image().
    template <typename P>
        requires std::convertible_to<P&&, ImageSource>
    IconSource(P&& picture) :
        IconSource(image(ImageSource(std::forward<P>(picture)))) // NOLINT(google-explicit-constructor)
    {
    }

    /// A path icon drawing `data`.
    [[nodiscard]] static IconSource path(PathGeometry data);
    /// The picture, recolored to the foreground while `monochrome` (true by default). The
    /// foreground's RGB is drawn over the picture's own alpha.
    [[nodiscard]] static IconSource bitmap(Bitmap image, bool monochrome = true);
    /// The picture as it is (an animated one shows its first frame).
    [[nodiscard]] static IconSource image(ImageSource source);

    /// True for a null IconSource (nothing to draw).
    [[nodiscard]] bool empty() const
    {
        return std::holds_alternative<std::monostate>(content_);
    }
    /// True when there is an icon; `!empty()`.
    [[nodiscard]] explicit operator bool() const
    {
        return !empty();
    }

    /// The glyph for a font or symbol icon, else null. Points into this IconSource.
    [[nodiscard]] const IconGlyph* glyph() const
    {
        return std::get_if<IconGlyph>(&content_);
    }

    /// The icon's own size before any ScaleToFit scales it. A glyph is measured with `measurer` and
    /// the result kept for draw(); everything else ignores it. Safe to call from a layout worker
    /// while another thread draws the same icon.
    Size measure(const TextMeasurer& measurer) const;

    /// Draws the icon into `box` as a Uniform ScaleToFit would: its natural size scaled to fit and
    /// centered. `foreground` colors a glyph, a path and a monochrome bitmap; a picture keeps its
    /// own colors. Copies of one IconSource may be drawn on several threads at once.
    void draw(Renderer& renderer, const Rect& box, const Color& foreground) const;

    /// Draws the icon in a control's icon slot of `size` DIPs, centered in `box`: a glyph at `size`
    /// as draw_icon() draws it, anything else fitted into a `size` square as draw() does. A `size`
    /// of 0 is the glyph's own size, and fits anything else into `box`. `symbol_font`, when not
    /// empty, replaces the default symbol font of a glyph that names it (a theme's
    /// `icon_font_family`); a glyph from another font keeps its own.
    void draw_in_slot(Renderer&        renderer,
                      const Rect&      box,
                      const Color&     foreground,
                      float            size        = 0.0f,
                      std::string_view symbol_font = {}) const;

    /// The room draw_in_slot() takes for `size`: a `size` square; for a `size` of 0 the glyph's own
    /// square, or the natural size of anything else.
    [[nodiscard]] Size slot_size(float size = 0.0f) const;

    /// The slot size a control gives this icon: a glyph's own size, or `fallback` for anything
    /// else (a control's icon size, so a picture takes the room a glyph would).
    [[nodiscard]] float extent(float fallback) const
    {
        const IconGlyph* g = glyph();
        return g != nullptr ? g->size : fallback;
    }

    /// The same glyph, path, bitmap or picture.
    friend bool operator==(const IconSource& a, const IconSource& b)
    {
        return a.content_ == b.content_;
    }

private:
    // The silhouette of a monochrome bitmap in the last foreground asked for, shared by copies of
    // the source and guarded by its own mutex (IconSource.cpp).
    struct TintCache;
    struct BitmapContent
    {
        Bitmap                     image;
        bool                       monochrome = true;
        std::shared_ptr<TintCache> tint;

        friend bool operator==(const BitmapContent& a, const BitmapContent& b)
        {
            return a.monochrome == b.monochrome && a.image == b.image; // not the cache
        }
    };
    struct Picture
    {
        ImageSource source;

        friend bool operator==(const Picture&, const Picture&) = default;
    };

    [[nodiscard]] Size natural_size() const;

    // A glyph's measured box, from measure(); a square of its size until then. Both floats in one
    // atomic word, so a measure on a layout worker never tears what a draw reads.
    class GlyphBox
    {
    public:
        GlyphBox() = default;
        GlyphBox(const GlyphBox& other) noexcept : bits_(other.bits_.load(std::memory_order_relaxed))
        {
        }
        GlyphBox& operator=(const GlyphBox& other) noexcept
        {
            bits_.store(other.bits_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }
        ~GlyphBox() = default;
        [[nodiscard]] Size load() const;
        void               store(Size size);

    private:
        std::atomic<std::uint64_t> bits_{0};
    };

    std::variant<std::monostate, IconGlyph, PathGeometry, BitmapContent, Picture> content_;
    mutable GlyphBox                                                              glyph_box_;
};

} // namespace ui
