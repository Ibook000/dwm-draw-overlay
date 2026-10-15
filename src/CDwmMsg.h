#pragma once
#include "DrawProtocol.h"
#include <string>

// One producer thread owns a frame from ExeStartDraw through ExeEndDraw.
class CDwmMsg {
public:
    explicit CDwmMsg(const wchar_t* mappingName = MAPNAME, const wchar_t* mutexName = MUTEXNAME)
        : m_mappingName(mappingName), m_mutexName(mutexName) {}
    ~CDwmMsg();
    CDwmMsg(const CDwmMsg&) = delete;
    CDwmMsg& operator=(const CDwmMsg&) = delete;
    bool InitMsg();
    bool ExeStartDraw();
    void ExeEndDraw();
    void CancelDraw();
    // True when the last submitted frame dropped commands or points.
    bool lastFrameOverflowed() const { return m_lastOverflow; }

    // Fonts are interned per frame. Index 0 is always the renderer default font,
    // so passing 0 (or omitting the arguments) keeps the previous behaviour.
    // Returns a table index, or 0 when the font table is full.
    int AddFont(const char* family, float sizePixels, int ranges);
    static const int DEFAULT_FONT_INDEX = 0;

    void DrawNewText(float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
    void DrawNewTextStr(float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
    void DrawNewTextEx(float X, float Y, ImVec4 Color, bool Outlined, const char* Str, int fontIndex, float size);
    void DrawCircleFilled(float X, float Y, float Radius, ImVec4 Color, int Segments);
    void DrawCircle(float X, float Y, float Radius, ImVec4 Color, int Segments, float thickness);
    void DrawRect(float X, float Y, float W, float H, ImVec4 Color, float thickness);
    void DrawRectEx(float X, float Y, float W, float H, ImVec4 Color, float thickness);
    void DrawFilledRect(float X, float Y, float W, float H, ImVec4 Color);
    void DrawLine(float X1, float Y1, float X2, float Y2, ImVec4 Color, float thickness);
    void DrawRoundRect(float X, float Y, float W, float H, float Radius, ImVec4 Color, float thickness, bool filled);
    void DrawEllipse(float X, float Y, float RX, float RY, ImVec4 Color, float thickness, int Segments, bool filled);
    void DrawPolygon(const DwmVec2* points, int count, ImVec4 Color, float thickness, bool filled, bool closed);
    void DrawBezier(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
        ImVec4 Color, float thickness, int segments);
    void DrawArc(float X, float Y, float Radius, float startDegrees, float endDegrees,
        ImVec4 Color, float thickness, int segments);
    void PushClipRect(float x, float y, float width, float height);
    void PopClipRect();
    float GetFps();
private:
    DrawInfo* Append(DRAWTYPE type);
    void Text(DRAWTYPE type, float X, float Y, ImVec4 color, bool outlined, const char* str,
        int fontIndex = DEFAULT_FONT_INDEX, float size = 0.0f);
    // Appends vertices to the frame's shared point pool; returns the first index or -1.
    int AddPoints(const DwmVec2* points, int count);
    void SetOverflow();
    HANDLE m_hFileMapping = nullptr;
    HANDLE m_mutex = nullptr;
    PDRWARR m_pDrawList = nullptr;
    DWORD m_owner = 0;
    bool m_lastOverflow = false;
    std::wstring m_mappingName, m_mutexName;
};
