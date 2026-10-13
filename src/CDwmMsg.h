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
    void DrawNewText(float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
    void DrawNewTextStr(float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
    void DrawCircleFilled(float X, float Y, float Radius, ImVec4 Color, int Segments);
    void DrawCircle(float X, float Y, float Radius, ImVec4 Color, int Segments, float thickness);
    void DrawRect(float X, float Y, float W, float H, ImVec4 Color, float thickness);
    void DrawRectEx(float X, float Y, float W, float H, ImVec4 Color, float thickness);
    void DrawFilledRect(float X, float Y, float W, float H, ImVec4 Color);
    void DrawLine(float X1, float Y1, float X2, float Y2, ImVec4 Color, float thickness);
    void PushClipRect(float x, float y, float width, float height);
    void PopClipRect();
    float GetFps();
private:
    DrawInfo* Append(DRAWTYPE type);
    void Text(DRAWTYPE type, float X, float Y, ImVec4 color, bool outlined, const char* str);
    HANDLE m_hFileMapping = nullptr;
    HANDLE m_mutex = nullptr;
    PDRWARR m_pDrawList = nullptr;
    DWORD m_owner = 0;
    std::wstring m_mappingName, m_mutexName;
};
