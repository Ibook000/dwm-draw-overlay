#pragma once
#include "DrawProtocol.h"
#include <vector>

// Low-level shape emitters. Everything lands in the overlay draw list; coordinates are
// absolute screen pixels, already origin-shifted by the caller.
namespace DwmPrimitives {
// Winding-agnostic polygon fill. ImGui 1.70 only offers AddConvexPolyFilled, so concave
// outlines are triangulated by ear clipping instead of rendering as a wrong blob.
void FillPolygon(ImDrawList* list, const ImVec2* points, int count, ImU32 color);
// Stroke for an open or closed run of points.
void StrokePolyline(ImDrawList* list, const ImVec2* points, int count, ImU32 color, float thickness, bool closed);
// Ellipse as an exact parametric polyline; segments are clamped to a sane range.
void StrokeEllipse(ImDrawList* list, float cx, float cy, float rx, float ry, ImU32 color, float thickness, int segments);
void FillEllipse(ImDrawList* list, float cx, float cy, float rx, float ry, ImU32 color, int segments);
}
