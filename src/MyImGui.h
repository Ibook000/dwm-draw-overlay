#pragma once
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
bool ImGuiDx11_init(IDXGISwapChain* swapChain, ID3D11Device* device);
void ImGuiDx11_shutdown();
void DrawNewText(float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
void DrawNewTextStr(float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
void DrawCircleFilled(float X, float Y, float Radius, ImVec4 Color, int Segments);
void DrawCircle(float X, float Y, float Radius, ImVec4 Color, int Segments, float thickness);
void DrawRect(float X, float Y, float W, float H, ImVec4 Color, float thickness);
void DrawRectEx(float X, float Y, float W, float H, ImVec4 Color, float thickness);
void DrawFilledRect(float X, float Y, float W, float H, ImVec4 Color);
void DrawLine(float X1, float Y1, float X2, float Y2, ImVec4 Color, float thickness);
