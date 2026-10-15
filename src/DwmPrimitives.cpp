#include "DwmPrimitives.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr float kEpsilon = 1e-6f;
// Above this vertex count ear clipping's O(n^2) cost is not worth it per frame; such
// outlines are assumed convex and handed to ImGui's fan fill.
constexpr int kEarClipLimit = 512;

float Cross(const ImVec2& a, const ImVec2& b) { return a.x * b.y - a.y * b.x; }

// Shoelace sum. Positive means clockwise on screen because ImGui's Y axis points down,
// which is the winding ImDrawList's anti-aliased fill expects.
float SignedArea(const ImVec2* points, int count) {
    float total = 0;
    for (int i = 0, j = count - 1; i < count; j = i++)
        total += (points[j].x * points[i].y) - (points[i].x * points[j].y);
    return total * 0.5f;
}

bool PointInTriangle(const ImVec2& p, const ImVec2& a, const ImVec2& b, const ImVec2& c) {
    // Only valid for a positively oriented (clockwise) triangle.
    const float d1 = Cross(ImVec2(b.x - a.x, b.y - a.y), ImVec2(p.x - a.x, p.y - a.y));
    const float d2 = Cross(ImVec2(c.x - b.x, c.y - b.y), ImVec2(p.x - b.x, p.y - b.y));
    const float d3 = Cross(ImVec2(a.x - c.x, a.y - c.y), ImVec2(p.x - c.x, p.y - c.y));
    return d1 >= -kEpsilon && d2 >= -kEpsilon && d3 >= -kEpsilon;
}

bool IsConvex(const ImVec2* points, int count) {
    // A simple polygon is convex when every turn has the same sign.
    bool sawPositive = false, sawNegative = false;
    for (int i = 0; i < count; ++i) {
        const ImVec2& p0 = points[(i + count - 1) % count];
        const ImVec2& p1 = points[i];
        const ImVec2& p2 = points[(i + 1) % count];
        const float turn = Cross(ImVec2(p1.x - p0.x, p1.y - p0.y), ImVec2(p2.x - p1.x, p2.y - p1.y));
        if (turn > kEpsilon) sawPositive = true;
        else if (turn < -kEpsilon) sawNegative = true;
        if (sawPositive && sawNegative) return false;
    }
    return true;
}

// Ear clipping for a simple, positively oriented (clockwise on screen) polygon.
// Returns false when no ear could be found, which happens for self-intersecting input.
bool EarClip(const std::vector<ImVec2>& points, std::vector<int>& triangles) {
    const int count = static_cast<int>(points.size());
    std::vector<int> remaining(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) remaining[static_cast<size_t>(i)] = i;

    int guard = count * count; // Bound the search so bad input cannot spin forever.
    while (remaining.size() > 3) {
        bool clipped = false;
        const int n = static_cast<int>(remaining.size());
        for (int i = 0; i < n; ++i) {
            const int i0 = remaining[static_cast<size_t>((i + n - 1) % n)];
            const int i1 = remaining[static_cast<size_t>(i)];
            const int i2 = remaining[static_cast<size_t>((i + 1) % n)];
            const ImVec2& a = points[static_cast<size_t>(i0)];
            const ImVec2& b = points[static_cast<size_t>(i1)];
            const ImVec2& c = points[static_cast<size_t>(i2)];

            // Convex corners of a clockwise polygon turn clockwise (positive cross).
            if (Cross(ImVec2(b.x - a.x, b.y - a.y), ImVec2(c.x - b.x, c.y - b.y)) <= kEpsilon) continue;

            bool contains = false;
            for (int other : remaining) {
                if (other == i0 || other == i1 || other == i2) continue;
                if (PointInTriangle(points[static_cast<size_t>(other)], a, b, c)) { contains = true; break; }
            }
            if (contains) continue;

            triangles.push_back(i0); triangles.push_back(i1); triangles.push_back(i2);
            remaining.erase(remaining.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) return false;
        if (--guard <= 0) return false;
    }
    if (remaining.size() == 3) {
        triangles.push_back(remaining[0]);
        triangles.push_back(remaining[1]);
        triangles.push_back(remaining[2]);
    }
    return triangles.size() >= 3;
}

// Emits flat, non-anti-aliased triangles directly. Per-ear AddConvexPolyFilled would draw
// its own AA band along every interior shared edge and leave visible seams.
void EmitTriangles(ImDrawList* list, const std::vector<ImVec2>& points,
    const std::vector<int>& triangles, ImU32 color) {
    const int triangleCount = static_cast<int>(triangles.size()) / 3;
    if (triangleCount <= 0) return;
    list->PrimReserve(triangleCount * 3, triangleCount * 3);
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    for (int t = 0; t < triangleCount; ++t) {
        for (int v = 0; v < 3; ++v) {
            const ImVec2& p = points[static_cast<size_t>(triangles[static_cast<size_t>(t * 3 + v)])];
            list->PrimWriteIdx(static_cast<ImDrawIdx>(list->_VtxCurrentIdx));
            list->PrimWriteVtx(p, uv, color);
        }
    }
}
}

namespace DwmPrimitives {

void FillPolygon(ImDrawList* list, const ImVec2* points, int count, ImU32 color) {
    if (!list || !points || count < 3) return;
    if ((color & IM_COL32_A_MASK) == 0) return;

    std::vector<ImVec2> work(points, points + count);
    // Normalise winding so the fill and the ear tests agree on orientation.
    if (SignedArea(work.data(), count) < 0) std::reverse(work.begin(), work.end());

    if (count <= kEarClipLimit && !IsConvex(work.data(), count)) {
        std::vector<int> triangles;
        if (EarClip(work, triangles)) {
            EmitTriangles(list, work, triangles, color);
            // Re-stroke the outline so the flat fill keeps a smooth silhouette.
            StrokePolyline(list, work.data(), count, color, 1.0f, true);
            return;
        }
    }
    // Convex, very large, or not triangulable: ImGui's fan fill is correct for convex input.
    list->AddConvexPolyFilled(work.data(), count, color);
}

void StrokePolyline(ImDrawList* list, const ImVec2* points, int count, ImU32 color, float thickness, bool closed) {
    if (!list || !points || count < 2) return;
    list->AddPolyline(points, count, color, closed, thickness);
}

void StrokeEllipse(ImDrawList* list, float cx, float cy, float rx, float ry, ImU32 color, float thickness, int segments) {
    if (!list || rx <= 0 || ry <= 0) return;
    if (segments < 12) segments = 12;
    if (segments > 512) segments = 512;
    // One point per step over [0, 2u) plus an explicit closing point, stroked closed.
    std::vector<ImVec2> outline(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float angle = 6.283185307179586f * static_cast<float>(i) / static_cast<float>(segments);
        outline[static_cast<size_t>(i)] = ImVec2(cx + rx * std::cos(angle), cy + ry * std::sin(angle));
    }
    list->AddPolyline(outline.data(), segments, color, true, thickness);
}

void FillEllipse(ImDrawList* list, float cx, float cy, float rx, float ry, ImU32 color, int segments) {
    if (!list || rx <= 0 || ry <= 0) return;
    if (segments < 12) segments = 12;
    if (segments > 512) segments = 512;
    // Increasing angle is clockwise on screen (Y down), matching ImGui's AA fill winding.
    std::vector<ImVec2> outline(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float angle = 6.283185307179586f * static_cast<float>(i) / static_cast<float>(segments);
        outline[static_cast<size_t>(i)] = ImVec2(cx + rx * std::cos(angle), cy + ry * std::sin(angle));
    }
    list->AddConvexPolyFilled(outline.data(), segments, color);
}
}
