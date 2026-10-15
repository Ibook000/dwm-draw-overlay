#pragma once
#include "CDwmMsg.h"
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

// Public convenience API. RGB/alpha use 0..255; positions use screen pixels.
struct DwmColor {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    ImVec4 normalized() const {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }
};

// A font request: family name (or a full .ttf path), pixel size and glyph coverage.
// Descriptors are cheap value types; pass one to DwmDraw::text on every frame.
struct DwmFont {
    std::string family;              // e.g. "Microsoft YaHei", "consola", or "C:\\x\\my.ttf".
    float size = 0;                  // Pixel height; 0 uses the renderer default.
    int ranges = DwmRanges_Auto;     // DwmGlyphRanges.
};

class DwmDraw {
public:
    explicit DwmDraw(const wchar_t* mapping = MAPNAME, const wchar_t* mutex = MUTEXNAME)
        : channel(mapping, mutex) {}

    bool init() { return channel.InitMsg(); }
    bool begin() { return channel.ExeStartDraw(); }
    void end() { channel.ExeEndDraw(); }
    void cancel() { channel.CancelDraw(); }
    // True when the frame just submitted dropped commands or points.
    bool overflowed() const { return channel.lastFrameOverflowed(); }
    // Set the origin used by all following drawing calls (absolute physical pixels).
    bool setPosition(float x, float y) {
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        originX = x; originY = y;
        return true;
    }
    float positionX() const { return originX; }
    float positionY() const { return originY; }
    void pushClipRect(float x, float y, float width, float height) {
        channel.PushClipRect(x + originX, y + originY, width, height);
    }
    void popClipRect() { channel.PopClipRect(); }

    // UTF-8 text: use u8"中文" in a C++17 source file.
    void text(float x, float y, const char* utf8, DwmColor color = {}, bool outlined = true) {
        channel.DrawNewTextStr(x + originX, y + originY, color.normalized(), outlined, utf8);
    }
    // Choose a family/size. The renderer interns identical requests, so calling this
    // on every frame is cheap and does not rebuild font atlases.
    void text(float x, float y, const char* utf8, const DwmFont& font, DwmColor color = {}, bool outlined = true) {
        const int index = channel.AddFont(font.family.empty() ? nullptr : font.family.c_str(),
            font.size, font.ranges);
        channel.DrawNewTextEx(x + originX, y + originY, color.normalized(), outlined, utf8, index, font.size);
    }
    void rect(float x, float y, float width, float height, DwmColor color = {}, float thickness = 2) {
        channel.DrawRect(x + originX, y + originY, width, height, color.normalized(), thickness);
    }
    void filledRect(float x, float y, float width, float height, DwmColor color = {}) {
        channel.DrawFilledRect(x + originX, y + originY, width, height, color.normalized());
    }
    void cornerRect(float x, float y, float width, float height, DwmColor color = {}, float thickness = 2) {
        channel.DrawRectEx(x + originX, y + originY, width, height, color.normalized(), thickness);
    }
    // True rounded rectangle with per-pixel anti-aliased corners.
    void roundRect(float x, float y, float width, float height, float radius,
        DwmColor color = {}, float thickness = 2) {
        channel.DrawRoundRect(x + originX, y + originY, width, height, radius, color.normalized(), thickness, false);
    }
    void filledRoundRect(float x, float y, float width, float height, float radius, DwmColor color = {}) {
        channel.DrawRoundRect(x + originX, y + originY, width, height, radius, color.normalized(), 1.0f, true);
    }
    void ellipse(float cx, float cy, float rx, float ry, DwmColor color = {}, float thickness = 2, int segments = 64) {
        channel.DrawEllipse(cx + originX, cy + originY, rx, ry, color.normalized(), thickness, segments, false);
    }
    void filledEllipse(float cx, float cy, float rx, float ry, DwmColor color = {}, int segments = 64) {
        channel.DrawEllipse(cx + originX, cy + originY, rx, ry, color.normalized(), 1.0f, segments, true);
    }
    void line(float x1, float y1, float x2, float y2, DwmColor color = {}, float thickness = 2) {
        channel.DrawLine(x1 + originX, y1 + originY, x2 + originX, y2 + originY, color.normalized(), thickness);
    }
    void circle(float x, float y, float radius, DwmColor color = {}, float thickness = 2, int segments = 64) {
        channel.DrawCircle(x + originX, y + originY, radius, color.normalized(), segments, thickness);
    }
    void filledCircle(float x, float y, float radius, DwmColor color = {}, int segments = 64) {
        channel.DrawCircleFilled(x + originX, y + originY, radius, color.normalized(), segments);
    }
    void triangle(float x1, float y1, float x2, float y2, float x3, float y3,
        DwmColor color = {}, float thickness = 2, bool filled = false) {
        const DwmVec2 points[3] = { {x1 + originX, y1 + originY}, {x2 + originX, y2 + originY}, {x3 + originX, y3 + originY} };
        channel.DrawPolygon(points, 3, color.normalized(), thickness, filled, true);
    }
    // points are content-local; closed=true strokes a loop, filled=true fills (convex or concave).
    void polygon(const DwmVec2* points, int count, DwmColor color = {}, float thickness = 2,
        bool filled = false, bool closed = true) {
        if (!points || count < 2) return;
        std::vector<DwmVec2> shifted(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) shifted[static_cast<size_t>(i)] = { points[i].x + originX, points[i].y + originY };
        channel.DrawPolygon(shifted.data(), count, color.normalized(), thickness, filled, closed);
    }
    void polyline(const DwmVec2* points, int count, DwmColor color = {}, float thickness = 2) {
        polygon(points, count, color, thickness, false, false);
    }
    void bezier(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
        DwmColor color = {}, float thickness = 2, int segments = 32) {
        channel.DrawBezier(x0 + originX, y0 + originY, x1 + originX, y1 + originY,
            x2 + originX, y2 + originY, x3 + originX, y3 + originY,
            color.normalized(), thickness, segments);
    }
    // Angles are degrees, clockwise on screen (0 = right, 90 = down).
    void arc(float cx, float cy, float radius, float startDegrees, float endDegrees,
        DwmColor color = {}, float thickness = 2, int segments = 32) {
        channel.DrawArc(cx + originX, cy + originY, radius, startDegrees, endDegrees,
            color.normalized(), thickness, segments);
    }
    bool clear() {
        if (!begin()) return false;
        end(); // An empty frame clears the last submitted content.
        return true;
    }
    float fps() { return channel.GetFps(); }

private:
    CDwmMsg channel;
    float originX = 0, originY = 0;
};
