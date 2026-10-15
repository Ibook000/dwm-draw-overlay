#include "MyImGui.h"
#include "DwmPrimitives.h"
#include "DrawProtocol.h"
#include <Windows.h>
#include <string>
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

namespace {
// Fixed default size; callers pick a different size per text command via DwmFont / DD_TextEx.
constexpr float g_defaultFontSize = DEFAULT_FONT_SIZE;
}

bool ImGuiDx11_init(IDXGISwapChain*, ID3D11Device* device) {
    if (!device) return false;
    g_pd3dDevice = device;
    g_pd3dDevice->AddRef();
    device->GetImmediateContext(&g_pd3dDeviceContext);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // The overlay supplies display dimensions and frame timing itself.
    // ImGui_ImplDX11_Init must run before any font texture work: it publishes the device
    // to the renderer backend that CreateDeviceObjects() uploads the atlas through.
    if (!g_pd3dDeviceContext || !ImGui_ImplDX11_Init(device, g_pd3dDeviceContext)) {
        ImGuiDx11_shutdown();
        return false;
    }
    // The default font goes through the same loader the runtime font library uses, so a
    // missing SimHei no longer degrades Chinese text to boxes.
    if (!ImGuiDx11_refreshFontTexture()) {
        ImGuiDx11_shutdown();
        return false;
    }
    return true;
}

// Builds (or rebuilds) the ImGui font texture. Must not be called between
// ImGui::NewFrame() and ImGui::Render(), because the atlas is Locked there.
bool ImGuiDx11_refreshFontTexture() {
    // A newly created context has no default font yet: add one before anything else so
    // ImDrawList::_Data->Font is never null.
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas->Fonts.empty()) {
        char path[MAX_PATH]{};
        const UINT length = GetWindowsDirectoryA(path, MAX_PATH);
        ImFont* font = nullptr;
        // Prefer a CJK-capable default; the previous renderer hard-coded simhei.ttf only.
        static const char* const candidates[] = { "\\Fonts\\msyh.ttc", "\\Fonts\\simhei.ttf", "\\Fonts\\simsun.ttc" };
        if (length && length < MAX_PATH - 20) {
            for (const char* candidate : candidates) {
                strcat_s(path, candidate);
                if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
                    font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, g_defaultFontSize, nullptr,
                        ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
                    if (font) break;
                }
                path[length] = '\0'; // Rewind so the next candidate appends cleanly.
            }
        }
        if (!font) ImGui::GetIO().Fonts->AddFontDefault();
    }
    ImGui_ImplDX11_InvalidateDeviceObjects();
    return ImGui_ImplDX11_CreateDeviceObjects();
}

float ImGuiDx11_defaultFontSize() { return g_defaultFontSize; }

void ImGuiDx11_shutdown() {
    ImGui_ImplDX11_Shutdown();
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void DrawNewTextEx(const ImFont* font, float size, float x, float y, ImVec4 color, bool outlined, const char* text) {
    if (!text) return;
    auto* list = ImGui::GetOverlayDrawList();
    if (size <= 0.0f) size = g_defaultFontSize;
    const ImU32 packed = ImGui::ColorConvertFloat4ToU32(color);
    if (outlined) list->AddText(font, size, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 255), text);
    list->AddText(font, size, ImVec2(x, y), packed, text);
}

void DrawNewTextStr(float x, float y, ImVec4 c, bool outlined, const char* text) {
    DrawNewTextEx(nullptr, g_defaultFontSize, x, y, c, outlined, text);
}

void DrawNewText(float x, float y, ImVec4 c, bool outlined, const char* text) {
    // ANSI (system code page) input: convert to UTF-8 then draw.
    const int size = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (size <= 0) return;
    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], size)) return;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], bytes, nullptr, nullptr))
        DrawNewTextStr(x, y, c, outlined, utf8.c_str());
}
void DrawCircleFilled(float x,float y,float r,ImVec4 c,int segments) {
    ImGui::GetOverlayDrawList()->AddCircleFilled(ImVec2(x,y),r,ImGui::ColorConvertFloat4ToU32(c),segments);
}
void DrawCircle(float x,float y,float r,ImVec4 c,int segments,float t) {
    ImGui::GetOverlayDrawList()->AddCircle(ImVec2(x,y),r,ImGui::ColorConvertFloat4ToU32(c),segments,t);
}
void DrawRect(float x,float y,float w,float h,ImVec4 c,float t) {
    ImGui::GetOverlayDrawList()->AddRect(ImVec2(x,y),ImVec2(x+w,y+h),ImGui::ColorConvertFloat4ToU32(c),0,0,t);
}
void DrawRectEx(float x,float y,float w,float h,ImVec4 c,float t) {
    // Corner brackets (an HUD-style frame), kept for backwards compatibility.
    const float a=w/4.5f,b=h/3.5f;
    auto* list=ImGui::GetOverlayDrawList(); const auto color=ImGui::ColorConvertFloat4ToU32(c);
    list->AddLine(ImVec2(x,y),ImVec2(x+a,y),color,t); list->AddLine(ImVec2(x,y),ImVec2(x,y+b),color,t);
    list->AddLine(ImVec2(x+w-a,y),ImVec2(x+w,y),color,t); list->AddLine(ImVec2(x+w,y),ImVec2(x+w,y+b),color,t);
    list->AddLine(ImVec2(x,y+h),ImVec2(x+a,y+h),color,t); list->AddLine(ImVec2(x,y+h-b),ImVec2(x,y+h),color,t);
    list->AddLine(ImVec2(x+w-a,y+h),ImVec2(x+w,y+h),color,t); list->AddLine(ImVec2(x+w,y+h-b),ImVec2(x+w,y+h),color,t);
}
void DrawFilledRect(float x,float y,float w,float h,ImVec4 c) {
    ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(x,y),ImVec2(x+w,y+h),ImGui::ColorConvertFloat4ToU32(c));
}
void DrawLine(float x1,float y1,float x2,float y2,ImVec4 c,float t) {
    ImGui::GetOverlayDrawList()->AddLine(ImVec2(x1,y1),ImVec2(x2,y2),ImGui::ColorConvertFloat4ToU32(c),t);
}
void DrawRoundRect(float x,float y,float w,float h,float radius,ImVec4 c,float thickness,bool filled) {
    auto* list = ImGui::GetOverlayDrawList();
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(c);
    // Clamp so the radius can never exceed half of either side (ImGui asserts otherwise).
    float limit = (w < h ? w : h) * 0.5f;
    if (limit < 0) limit = 0;
    if (radius < 0) radius = 0;
    if (radius > limit) radius = limit;
    const ImVec2 minimum(x, y), maximum(x + w, y + h);
    if (filled) list->AddRectFilled(minimum, maximum, color, radius, ImDrawCornerFlags_All);
    else list->AddRect(minimum, maximum, color, radius, ImDrawCornerFlags_All, thickness);
}
void DrawEllipse(float x,float y,float rx,float ry,ImVec4 c,float t,int segments,bool filled) {
    auto* list = ImGui::GetOverlayDrawList();
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(c);
    if (filled) DwmPrimitives::FillEllipse(list, x, y, rx, ry, color, segments);
    else DwmPrimitives::StrokeEllipse(list, x, y, rx, ry, color, t, segments);
}
void DrawPoints(const ImVec2* points,int count,ImVec4 c,float t,bool filled,bool closed) {
    if (!points || count < 2) return;
    auto* list = ImGui::GetOverlayDrawList();
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(c);
    if (filled && count >= 3) DwmPrimitives::FillPolygon(list, points, count, color);
    else DwmPrimitives::StrokePolyline(list, points, count, color, t, closed);
}
