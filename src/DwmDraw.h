#pragma once
#include "CDwmMsg.h"
#include <cstdint>
#include <cmath>

// Public convenience API. RGB/alpha use 0..255; positions use screen pixels.
struct DwmColor {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    ImVec4 normalized() const {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }
};

class DwmDraw {
public:
    explicit DwmDraw(const wchar_t* mapping = MAPNAME, const wchar_t* mutex = MUTEXNAME)
        : channel(mapping, mutex) {}

    bool init() { return channel.InitMsg(); }
    bool begin() { return channel.ExeStartDraw(); }
    void end() { channel.ExeEndDraw(); }
    void cancel() { channel.CancelDraw(); }
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
    // Text size currently follows the renderer's 16px font setting.
    void text(float x, float y, const char* utf8, DwmColor color = {}, bool outlined = true) {
        channel.DrawNewTextStr(x + originX, y + originY, color.normalized(), outlined, utf8);
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
    void line(float x1, float y1, float x2, float y2, DwmColor color = {}, float thickness = 2) {
        channel.DrawLine(x1 + originX, y1 + originY, x2 + originX, y2 + originY, color.normalized(), thickness);
    }
    void circle(float x, float y, float radius, DwmColor color = {}, float thickness = 2, int segments = 64) {
        channel.DrawCircle(x + originX, y + originY, radius, color.normalized(), segments, thickness);
    }
    void filledCircle(float x, float y, float radius, DwmColor color = {}, int segments = 64) {
        channel.DrawCircleFilled(x + originX, y + originY, radius, color.normalized(), segments);
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
