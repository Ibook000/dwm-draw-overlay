#pragma once
#include "MyImGui.h"
#include "DrawProtocol.h"
#include "DwmFontLibrary.h"
#include <string>

class CIMGuiDraw {
public:
    explicit CIMGuiDraw(const wchar_t* mappingName = MAPNAME, const wchar_t* mutexName = MUTEXNAME)
        : m_mappingName(mappingName), m_mutexName(mutexName) {}
    ~CIMGuiDraw();
    CIMGuiDraw(const CIMGuiDraw&) = delete;
    CIMGuiDraw& operator=(const CIMGuiDraw&) = delete;
    bool InitMessage();
    bool InitMiGuiDx11(IDXGISwapChain* swapChain, ID3D11Device* device);
    bool ImGuiDx11Draw(IDXGISwapChain* swapChain);
    void Shutdown(); // Call on the rendering thread.
private:
    bool Snapshot();
    void ResetRenderer();
    // Resolves this frame's font table into ImFont pointers. Must run before NewFrame()
    // because the atlas is Locked between NewFrame() and Render().
    void PrepareFonts();
    HANDLE m_mapping = nullptr;
    HANDLE m_mutex = nullptr;
    PDRWARR m_shared = nullptr;
    DRAWARR m_frame{};
    // Resolved font per frame-table slot; index 0 is the renderer default (nullptr).
    ImFont* m_resolvedFonts[MAX_FONTS]{};
    float m_resolvedSizes[MAX_FONTS]{};
    DwmFontLibrary m_fonts;
    ImGuiContext* m_context = nullptr;
    LARGE_INTEGER m_frequency{}, m_lastTime{};
    ULONGLONG m_nextConnect = 0;
    std::wstring m_mappingName, m_mutexName;
};
