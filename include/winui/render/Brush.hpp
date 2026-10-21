// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/OptionalFloat.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <cstddef>

namespace ui
{

/// One color stop of a gradient brush.
///
/// Stops are read in vector order; between two stops the color is interpolated linearly.
///
/// Example:
/// @code
/// const ui::GradientStop top{0.0f, ui::Color::from_rgba(255, 255, 255)};
/// const ui::GradientStop bottom{1.0f, ui::Color::from_rgba(200, 200, 200)};
/// @endcode
struct GradientStop
{
    /// Position along the gradient axis (or out along the radius), 0..1.
    float offset = 0.0f;
    /// The color at `offset`, alpha included.
    Color color;

    friend bool operator==(const GradientStop&, const GradientStop&) = default;
};

/// The stops of a gradient brush, in order: a list kept inside the brush up to four stops, so
/// handing a theme's gradient to a control or a renderer allocates nothing. More stops go to the
/// heap. Built like a vector, from a braced list or from a `std::vector<GradientStop>`, and read
/// like one.
///
/// Example:
/// @code
/// ui::GradientStops stops{{0.0f, ui::colors::White}, {1.0f, ui::colors::Black}};
/// stops.push_back({0.5f, ui::Color::from_rgba(128, 128, 128)});
/// for (const ui::GradientStop& stop : stops) { (void)stop.offset; }
/// @endcode
class GradientStops
{
public:
    /// How many stops fit without allocating.
    static constexpr std::size_t InlineCapacity = 4;

    GradientStops() = default;
    /// The stops in `stops`, in order.
    GradientStops(std::initializer_list<GradientStop> stops) // NOLINT(google-explicit-constructor): a list of stops is stops
    {
        assign(stops.begin(), stops.size());
    }
    /// The stops in `stops`, in order.
    GradientStops(const std::vector<GradientStop>& stops) // NOLINT(google-explicit-constructor)
    {
        assign(stops.data(), stops.size());
    }
    GradientStops(const GradientStops& other)
    {
        assign(other.data(), other.size_);
    }
    GradientStops(GradientStops&& other) noexcept :
        inline_(other.inline_),
        heap_(std::move(other.heap_)),
        capacity_(other.capacity_),
        size_(other.size_)
    {
        other.capacity_ = 0;
        other.size_     = 0;
    }
    GradientStops& operator=(const GradientStops& other)
    {
        if (this != &other)
        {
            assign(other.data(), other.size_);
        }
        return *this;
    }
    GradientStops& operator=(GradientStops&& other) noexcept
    {
        inline_         = other.inline_;
        heap_           = std::move(other.heap_);
        capacity_       = other.capacity_;
        size_           = other.size_;
        other.capacity_ = 0;
        other.size_     = 0;
        return *this;
    }
    ~GradientStops() = default;

    /// How many stops there are.
    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }
    /// Whether there are none, which paints nothing.
    [[nodiscard]] bool empty() const noexcept
    {
        return size_ == 0;
    }
    /// The first stop.
    [[nodiscard]] const GradientStop* begin() const noexcept
    {
        return data();
    }
    /// One past the last stop.
    [[nodiscard]] const GradientStop* end() const noexcept
    {
        return data() + size_;
    }
    /// The first stop.
    [[nodiscard]] GradientStop* begin() noexcept
    {
        return data();
    }
    /// One past the last stop.
    [[nodiscard]] GradientStop* end() noexcept
    {
        return data() + size_;
    }
    /// The stops as one array of size() elements.
    [[nodiscard]] const GradientStop* data() const noexcept
    {
        return heap_ ? heap_.get() : inline_.data();
    }
    /// The stops as one array of size() elements.
    [[nodiscard]] GradientStop* data() noexcept
    {
        return heap_ ? heap_.get() : inline_.data();
    }
    /// Stop `i`, below size().
    [[nodiscard]] const GradientStop& operator[](std::size_t i) const noexcept
    {
        return data()[i];
    }
    /// Stop `i`, below size().
    [[nodiscard]] GradientStop& operator[](std::size_t i) noexcept
    {
        return data()[i];
    }
    /// The first stop; there must be one.
    [[nodiscard]] const GradientStop& front() const noexcept
    {
        return data()[0];
    }
    /// The last stop; there must be one.
    [[nodiscard]] const GradientStop& back() const noexcept
    {
        return data()[size_ - 1];
    }
    /// The first stop; there must be one.
    [[nodiscard]] GradientStop& front() noexcept
    {
        return data()[0];
    }
    /// The last stop; there must be one.
    [[nodiscard]] GradientStop& back() noexcept
    {
        return data()[size_ - 1];
    }
    /// Adds `stop` after the last one.
    void push_back(const GradientStop& stop)
    {
        reserve(size_ + 1);
        data()[size_++] = stop;
    }
    /// Removes every stop.
    void clear() noexcept
    {
        size_ = 0;
    }
    /// The stops as a vector.
    [[nodiscard]] std::vector<GradientStop> to_vector() const
    {
        return {begin(), end()};
    }

    /// The same stops in the same order.
    friend bool operator==(const GradientStops& a, const GradientStops& b) noexcept
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end());
    }

private:
    // Room for `count` stops, keeping the ones there are.
    void reserve(std::size_t count)
    {
        const std::size_t room = heap_ ? capacity_ : InlineCapacity;
        if (count <= room)
        {
            return;
        }
        const std::size_t grown  = (std::max)(count, room * 2);
        auto              bigger = std::make_unique<GradientStop[]>(grown);
        std::copy(data(), data() + size_, bigger.get());
        heap_     = std::move(bigger);
        capacity_ = grown;
    }
    void assign(const GradientStop* first, std::size_t count)
    {
        size_ = 0;
        reserve(count);
        std::copy(first, first + count, data());
        size_ = count;
    }

    // Kept out of std::vector, which in an MSVC debug build allocates even when empty, so that
    // copying a brush with few stops allocates nothing in any build.
    std::array<GradientStop, InlineCapacity> inline_{};
    std::unique_ptr<GradientStop[]>          heap_; // every stop, once there are more than fit inline
    std::size_t                              capacity_ = 0;
    std::size_t                              size_     = 0;
};

/// The direction a LinearGradientBrush runs across the painted bounds.
///
/// FillRoundedRectGradient honors all three. StrokeRoundedRectGradient, Fluent's elevation
/// border, always runs vertically.
enum class GradientAxis
{
    Vertical,   ///< Top to bottom, as in Luna and Aero button chrome.
    Horizontal, ///< Left to right, as in 98 and XP caption bars and toolbar backgrounds.
    Diagonal,   ///< Top-left to bottom-right.
};

/// A linear gradient running along one GradientAxis of the painted bounds.
///
/// Flat-color drawing keeps taking a plain `Color`. A control that wants gradient chrome passes
/// one of these to Renderer::fill_rounded_rect_gradient(), and a Shape takes it as one
/// alternative of `Brush`.
///
/// Example:
/// @code
/// const ui::LinearGradientBrush luna{
///     .stops = {{0.0f, ui::Color::from_rgba(255, 255, 255)},
///               {1.0f, ui::Color::from_rgba(214, 208, 197)}},
/// };
/// renderer.fill_rounded_rect_gradient(ui::Rect{0, 0, 75, 23}, 3.0f, luna);
///
/// auto rect = std::make_unique<ui::RectangleControl>();
/// rect->set_fill(ui::LinearGradientBrush{.stops = luna.stops, .axis = ui::GradientAxis::Horizontal});
/// @endcode
struct LinearGradientBrush
{
    /// The color stops, in increasing `offset` order. Empty paints nothing.
    GradientStops stops;
    /// The direction the stops run; Vertical by default.
    GradientAxis axis = GradientAxis::Vertical;

    friend bool operator==(const LinearGradientBrush&, const LinearGradientBrush&) = default;
};

/// Whether a gradient's points are fractions of the painted
/// shape's bounding box (0,0 its top-left, 1,1 its bottom-right) or DIPs from its top-left.
enum class BrushMappingMode
{
    RelativeToBoundingBox, ///< 0..1 fractions of the bounding box (the default).
    Absolute,              ///< DIPs from the bounding box's top-left.
};

/// What a gradient paints past its last stop: the end colors held
/// (`Pad`), the gradient mirrored (`Reflect`) or started over (`Repeat`).
enum class GradientSpreadMethod
{
    Pad,     ///< The end colors extend outwards (the default).
    Reflect, ///< The gradient repeats mirrored.
    Repeat,  ///< The gradient repeats from its first stop.
};

/// A radial gradient. The defaults: an ellipse centered on the box with radii of half its size,
/// the gradient starting at the center. `gradient_origin` is a point in the same space as
/// `center`, not an offset from it.
///
/// Example:
/// @code
/// auto rect = std::make_unique<ui::RectangleControl>();
/// rect->set_fill(ui::RadialGradientBrush{
///     .stops  = {{0.0f, ui::Color::from_rgba(255, 255, 0)}, {1.0f, ui::Color::from_rgba(0, 0, 255)}},
///     .center = ui::Point{0.25f, 0.25f},
/// });
/// @endcode
struct RadialGradientBrush
{
    /// The color stops, from `gradient_origin` (offset 0) out to the ellipse's edge (offset 1).
    GradientStops stops;
    /// Center of the outer ellipse, in the space `mapping_mode` picks.
    Point center{0.5f, 0.5f};
    /// Horizontal radius of the outer ellipse, in the space `mapping_mode` picks.
    float radius_x = 0.5f;
    /// Vertical radius of the outer ellipse, in the space `mapping_mode` picks.
    float radius_y = 0.5f;
    /// Where offset 0 sits; moving it off `center` gives an off-center highlight.
    Point gradient_origin{0.5f, 0.5f};
    /// Whether the points and radii above are box fractions or DIPs.
    BrushMappingMode mapping_mode = BrushMappingMode::RelativeToBoundingBox;
    /// What is painted outside the outer ellipse.
    GradientSpreadMethod spread_method = GradientSpreadMethod::Pad;

    friend bool operator==(const RadialGradientBrush&, const RadialGradientBrush&) = default;
};

/// An in-app acrylic material: what is behind the shape, over an opaque fallback, blurred (a
/// Gaussian of standard deviation 30), then a `Luminosity` blend with effective_luminosity()
/// and a `Color` blend with effective_tint(). A tint that works out opaque paints only the tint.
/// A backend that cannot blur paints `fallback_color`, and so does `always_use_fallback`.
///
/// The defaults: a white tint at 80 % alpha, full tint opacity, no luminosity opacity (so one
/// is derived from the tint), and a transparent fallback.
///
/// The blur reads what is already drawn, so a shape with an acrylic fill must render after the
/// content it sits on (see DrawGeometry).
///
/// Example:
/// @code
/// auto backdrop = std::make_unique<ui::RectangleControl>();
/// backdrop->set_fill(ui::AcrylicBrush{
///     .tint_color     = ui::Color::from_rgba(0, 120, 215),
///     .tint_opacity   = 0.6f,
///     .fallback_color = ui::Color::from_rgba(0, 90, 160),
/// });
/// @endcode
struct AcrylicBrush
{
    /// The tint laid over the blurred backdrop; its alpha counts.
    Color tint_color = Color::from_argb(0xCCFFFFFF);
    /// Multiplies the tint's alpha, 0..1.
    float tint_opacity = 1.0f;
    /// The opacity of the luminosity blend; nullopt derives it from the tint (see effective_luminosity()).
    OptionalFloat tint_luminosity_opacity{};
    /// What a backend paints when it cannot blur, or when `always_use_fallback` is set.
    Color fallback_color = colors::Transparent;
    /// Paint `fallback_color` even where acrylic could be drawn.
    bool always_use_fallback = false;

    /// The tint with its alpha multiplied by `tint_opacity`. When no luminosity opacity was
    /// named, it is also capped, which holds a pure white tint to 45 % and pure black to 85 %,
    /// relaxing as saturation rises.
    [[nodiscard]] Color effective_tint() const;
    /// The luminosity layer's color: the tint's color at the named luminosity
    /// opacity, or else at its HSV value clamped to [0.125, 0.965] and the tint opacity mapped
    /// into [0.15, 1.03] (capped at 1).
    [[nodiscard]] Color effective_luminosity() const;

    friend bool operator==(const AcrylicBrush&, const AcrylicBrush&) = default;
};

/// Any paint a Shape's fill or stroke accepts: a solid `Color` or one
/// of the brush structs. Renderer::draw_geometry() takes it; control chrome keeps plain `Color`.
using Brush = std::variant<Color, LinearGradientBrush, RadialGradientBrush, AcrylicBrush>;

} // namespace ui
