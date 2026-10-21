// SPDX-License-Identifier: MIT
// How a stroke turns into geometry, shared by the canvas and the toolbar's stroke preview.
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/media/InkCanvas.hpp"

#include <optional>
#include <span>
#include <vector>

namespace ui::detail
{

// The factor a point's pressure scales the nib by: 0.25 at none, 1 at 0.5, 1.75 at full.
[[nodiscard]] float ink_pressure_scale(const InkDrawingAttributes& attributes, float pressure);

// The color the stroke is painted in: half alpha for a highlighter, `pencil_opacity` for pencil.
[[nodiscard]] Color ink_paint_color(const InkDrawingAttributes& attributes);

// The stroke's center line, smoothed when `fit_to_curve` and flattened to about 2 DIP steps,
// with the pressure interpolated along it.
[[nodiscard]] std::vector<InkPoint> ink_centerline(const InkStroke& stroke);

// The one command that paints `stroke`, in its own (canvas) coordinates; nullopt for no points.
// A round nib of even width at constant pressure is a stroked Bézier path; anything else is the
// nib swept from sample to sample, a filled union of convex hulls.
[[nodiscard]] std::optional<DrawGeometry> ink_stroke_drawing(const InkStroke& stroke);

// Every command that paints `stroke`, in order: ink_stroke_drawing() for pen ink; for pencil ink
// that stroke lighter, then its grain. Empty for no points.
[[nodiscard]] std::vector<DrawGeometry> ink_stroke_drawings(const InkStroke& stroke);

// What paints a selected stroke: its shape in white inside a 1 DIP rim of its color, so it reads
// as hollow at any width. Empty for no points.
[[nodiscard]] std::vector<DrawGeometry> ink_selected_drawing(const InkStroke& stroke);

// Whether `p` is inside the polygon `outline` (closed back to its first point), by the even-odd rule.
[[nodiscard]] bool ink_point_in_polygon(Point p, std::span<const Point> outline);

// Whether the segment a-b passes within `tolerance` of the stroke's painted area.
[[nodiscard]] bool ink_stroke_touches(const InkStroke& stroke, Point a, Point b, float tolerance);

// The point eraser: what is left of `stroke` once an eraser of `radius` has swept the segment
// a-b, as the runs of its center line outside the swept area (each a stroke with id 0 and the
// same attributes); nullopt when the eraser missed it. A run of a single sample is dropped.
[[nodiscard]] std::optional<std::vector<InkStroke>> ink_stroke_cut(const InkStroke& stroke, Point a, Point b, float radius);

} // namespace ui::detail
