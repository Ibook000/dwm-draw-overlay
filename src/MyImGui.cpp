#include "MyImGui.h"
#include <Windows.h>
#include <string>
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

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
    char path[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryA(path, MAX_PATH);
    ImFont* font = nullptr;
    if (length && length < MAX_PATH - 20) {
        strcat_s(path, "\\Fonts\\simhei.ttf");
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            font = io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    if (!font) io.Fonts->AddFontDefault();
    if (!g_pd3dDeviceContext || !ImGui_ImplDX11_Init(device, g_pd3dDeviceContext) || !ImGui_ImplDX11_CreateDeviceObjects()) {
        ImGuiDx11_shutdown();
        return false;
    }
    return true;
}
void ImGuiDx11_shutdown() {
    ImGui_ImplDX11_Shutdown();
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
void DrawNewTextStr(float x,float y,ImVec4 c,bool outlined,const char* text) {
    auto* list=ImGui::GetOverlayDrawList();
    if(outlined) list->AddText(ImVec2(x+1,y+1),IM_COL32(0,0,0,255),text);
    list->AddText(ImVec2(x,y),ImGui::ColorConvertFloat4ToU32(c),text);
}
void DrawNewText(float x,float y,ImVec4 c,bool outlined,const char* text) {
    const int size=MultiByteToWideChar(CP_ACP,0,text,-1,nullptr,0);
    if(size<=0) return;
    std::wstring wide(static_cast<size_t>(size),L'\0');
    if(!MultiByteToWideChar(CP_ACP,0,text,-1,&wide[0],size)) return;
    const int bytes=WideCharToMultiByte(CP_UTF8,0,wide.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(bytes<=0) return;
    std::string utf8(static_cast<size_t>(bytes),'\0');
    if(WideCharToMultiByte(CP_UTF8,0,wide.c_str(),-1,&utf8[0],bytes,nullptr,nullptr))
        DrawNewTextStr(x,y,c,outlined,utf8.c_str());
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
