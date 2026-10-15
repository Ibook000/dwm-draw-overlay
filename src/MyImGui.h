#pragma once
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
bool ImGuiDx11_init(IDXGISwapChain* swapChain, ID3D11Device* device);
void ImGuiDx11_shutdown();
// Re-uploads the font atlas after fonts were added at runtime.
bool ImGuiDx11_refreshFontTexture();
// Pixel size used for text commands that do not name their own size.
float ImGuiDx11_defaultFontSize();
void DrawNewText(float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
// font may be null (fall back to the current/default font). size is in pixels.
void DrawNewTextEx(const ImFont* font, float size, float X, float Y, ImVec4 Color, bool Outlined, const char* Str);
void DrawCircleFilled(float X, float Y, float Radius, ImVec4 Color, int Segments);
void DrawCircle(float X, float Y, float Radius, ImVec4 Color, int Segments, float thickness);
void DrawRect(float X, float Y, float W, float H, ImVec4 Color, float thickness);
void DrawRectEx(float X, float Y, float W, float H, ImVec4 Color, float thickness);
void DrawFilledRect(float X, float Y, float W, float H, ImVec4 Color);
void DrawLine(float X1, float Y1, float X2, float Y2, ImVec4 Color, float thickness);
void DrawRoundRect(float X, float Y, float W, float H, float Radius, ImVec4 Color, float thickness, bool filled);
void DrawEllipse(float X, float Y, float RX, float RY, ImVec4 Color, float thickness, int Segments, bool filled);
// points are already in absolute screen pixels. closed strokes a loop; filled triangulates.
void DrawPoints(const ImVec2* points, int count, ImVec4 Color, float thickness, bool filled, bool closed);
