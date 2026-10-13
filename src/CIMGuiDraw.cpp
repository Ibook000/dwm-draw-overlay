#include "save_state.hpp"
#include "CIMGuiDraw.h"
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <algorithm>
using Microsoft::WRL::ComPtr;
namespace {
struct ContextScope {
    ImGuiContext* previous;
    explicit ContextScope(ImGuiContext* current) : previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(current); }
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
bool Position(float v) { return std::isfinite(v) && std::fabs(v) <= 1000000.0f; }
bool Color(DrawColor& c) {
    float* channels[] = {&c.x,&c.y,&c.z,&c.w};
    for (float* v : channels) { if (!std::isfinite(*v)) return false; *v=(std::max)(0.0f,(std::min)(1.0f,*v)); }
    return true;
}
bool Thickness(float t) { return std::isfinite(t) && t > 0 && t <= 10000; }
void DrawCommand(DrawInfo& p, unsigned& clipDepth) {
    switch(p.m_DrawType) {
    case ClipPush_M: {
        const auto& r = p.From.Rect;
        const bool valid = Position(r.m_X) && Position(r.m_Y) && Position(r.m_W) && Position(r.m_H) && r.m_W >= 0 && r.m_H >= 0;
        const ImVec2 start = valid ? ImVec2(r.m_X, r.m_Y) : ImVec2(0, 0);
        const ImVec2 end = valid ? ImVec2(r.m_X + r.m_W, r.m_Y + r.m_H) : ImVec2(0, 0);
        ImGui::GetOverlayDrawList()->PushClipRect(start, end, true);
        ++clipDepth;
        break;
    }
    case ClipPop_M:
        if (clipDepth) { ImGui::GetOverlayDrawList()->PopClipRect(); --clipDepth; }
        break;
    case Text_M: case Text2_M: {
        auto& t=p.From.Text;
        if(!Position(t.m_X)||!Position(t.m_Y)||!Color(t.m_Color)) break;
        t.m_Str[BUF_LEN-1]='\0';
        if(p.m_DrawType==Text_M) DrawNewText(t.m_X,t.m_Y,t.m_Color,t.m_Outlined,t.m_Str);
        else DrawNewTextStr(t.m_X,t.m_Y,t.m_Color,t.m_Outlined,t.m_Str);
        break;
    }
    case CircleFill_M: {
        auto& c=p.From.CircleFill;
        if(!Position(c.m_X)||!Position(c.m_Y)||!Position(c.m_Radius)||c.m_Radius<=0||!Color(c.m_Color)) break;
        DrawCircleFilled(c.m_X,c.m_Y,c.m_Radius,c.m_Color,(std::max)(3,(std::min)(512,c.m_Segments))); break;
    }
    case Circle_M: {
        auto& c=p.From.Circle;
        if(!Position(c.m_X)||!Position(c.m_Y)||!Position(c.m_Radius)||c.m_Radius<=0||!Color(c.m_Color)||!Thickness(c.m_thickness)) break;
        DrawCircle(c.m_X,c.m_Y,c.m_Radius,c.m_Color,(std::max)(3,(std::min)(512,c.m_Segments)),c.m_thickness); break;
    }
    case Rect_M: case RectEx_M: {
        auto& r=p.From.Rect;
        if(!Position(r.m_X)||!Position(r.m_Y)||!Position(r.m_W)||!Position(r.m_H)||r.m_W<0||r.m_H<0||!Color(r.m_Color)||!Thickness(r.m_thickness)) break;
        if(p.m_DrawType==Rect_M) DrawRect(r.m_X,r.m_Y,r.m_W,r.m_H,r.m_Color,r.m_thickness);
        else DrawRectEx(r.m_X,r.m_Y,r.m_W,r.m_H,r.m_Color,r.m_thickness);
        break;
    }
    case FilledRect_M: {
        auto& r=p.From.FilledRect;
        if(!Position(r.m_X)||!Position(r.m_Y)||!Position(r.m_W)||!Position(r.m_H)||r.m_W<0||r.m_H<0||!Color(r.m_Color)) break;
        DrawFilledRect(r.m_X,r.m_Y,r.m_W,r.m_H,r.m_Color); break;
    }
    case Line_M: {
        auto& l=p.From.Line;
        if(!Position(l.m_X1)||!Position(l.m_Y1)||!Position(l.m_X2)||!Position(l.m_Y2)||!Color(l.m_Color)||!Thickness(l.thickness)) break;
        DrawLine(l.m_X1,l.m_Y1,l.m_X2,l.m_Y2,l.m_Color,l.thickness); break;
    }
    default: break;
    }
}
}
CIMGuiDraw::~CIMGuiDraw() { Shutdown(); }
void CIMGuiDraw::ResetRenderer() {
    if(!m_context) return;
    ImGuiContext* previous=ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);
    ImGuiDx11_shutdown();
    ImGui::DestroyContext(m_context);
    ImGui::SetCurrentContext(previous==m_context ? nullptr : previous);
    m_context=nullptr;
    m_lastTime.QuadPart=0;
}
void CIMGuiDraw::Shutdown() {
    ResetRenderer();
    if(m_shared) { UnmapViewOfFile(m_shared); m_shared=nullptr; }
    if(m_mapping) { CloseHandle(m_mapping); m_mapping=nullptr; }
    if(m_mutex) { CloseHandle(m_mutex); m_mutex=nullptr; }
    m_frame.m_DrawCount=0;
}
bool CIMGuiDraw::InitMessage() {
    if(m_shared) return true;
    const auto now=GetTickCount64();
    if(now<m_nextConnect) return false;
    m_nextConnect=now+1000;
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,m_mutexName.c_str());
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,m_mappingName.c_str());
    auto* shared=mapping ? static_cast<PDRWARR>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(DRAWARR))) : nullptr;
    bool valid=false;
    if(mutex && shared) {
        const auto wait=WaitForSingleObject(mutex,0);
        if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED) {
            valid=ValidDrawHeader(shared);
            if(valid && wait==WAIT_ABANDONED) { shared->m_DrawCount=0; ++shared->frameNumber; }
            ReleaseMutex(mutex);
        }
    }
    if(!valid) {
        if(shared) UnmapViewOfFile(shared);
        if(mapping) CloseHandle(mapping);
        if(mutex) CloseHandle(mutex);
        return false;
    }
    m_mutex=mutex; m_mapping=mapping; m_shared=shared;
    OutputDebugStringA("[DwmDraw] Shared protocol v3 connected.\n");
    return true;
}
bool CIMGuiDraw::Snapshot() {
    const auto wait=WaitForSingleObject(m_mutex,0); // Never block the rendering thread on the producer.
    if(wait==WAIT_TIMEOUT) return true; // Draw the last complete frame.
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) return false;
    bool valid=ValidDrawHeader(m_shared);
    if(valid) {
        if(wait==WAIT_ABANDONED) { m_shared->m_DrawCount=0; ++m_shared->frameNumber; }
        const int count=m_shared->m_DrawCount;
        if(count<0 || count>MAX_DRAW) {
            m_frame.m_DrawCount=0;
            OutputDebugStringA("[DwmDraw] Rejected invalid command count.\n");
        } else {
            m_frame.m_DrawCount=count;
            memcpy(m_frame.m_DrawInfoArr,m_shared->m_DrawInfoArr,count*sizeof(DrawInfo));
        }
    }
    ReleaseMutex(m_mutex);
    return valid;
}
bool CIMGuiDraw::InitMiGuiDx11(IDXGISwapChain* chain,ID3D11Device* device) {
    if(m_context && device==g_pd3dDevice) return true;
    ResetRenderer();
    ImGuiContext* previous=ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    m_context=ImGui::CreateContext();
    if(!m_context) { ImGui::SetCurrentContext(previous); return false; }
    ImGui::SetCurrentContext(m_context);
    const bool ok=ImGuiDx11_init(chain,device);
    if(!ok) { ImGui::DestroyContext(m_context); m_context=nullptr; }
    ImGui::SetCurrentContext(previous);
    QueryPerformanceFrequency(&m_frequency);
    return ok;
}
bool CIMGuiDraw::ImGuiDx11Draw(IDXGISwapChain* chain) {
    if(!chain || !InitMessage() || !Snapshot()) return false;
    ComPtr<ID3D11Device> device;
    HRESULT hr=chain->GetDevice(IID_PPV_ARGS(&device));
    if(FAILED(hr) || !InitMiGuiDx11(chain,device.Get())) return false;
    ContextScope context(m_context);
    // Reacquire and release the current back buffer every callback; no retained RTV blocks ResizeBuffers.
    ComPtr<ID3D11Texture2D> buffer;
    ComPtr<ID3D11RenderTargetView> target;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))) || FAILED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target))) return false;
    D3D11_TEXTURE2D_DESC desc{}; buffer->GetDesc(&desc);
    if(!desc.Width || !desc.Height) return false;
    ImGuiIO& io=ImGui::GetIO();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const double delta=m_lastTime.QuadPart && m_frequency.QuadPart ? double(now.QuadPart-m_lastTime.QuadPart)/double(m_frequency.QuadPart) : 1.0/60.0;
    m_lastTime=now;
    io.DeltaTime=static_cast<float>((std::max)(0.000001,(std::min)(1.0,delta)));
    io.DisplaySize=ImVec2(static_cast<float>(desc.Width),static_cast<float>(desc.Height));
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    unsigned clipDepth = 0;
    for(int i=0;i<m_frame.m_DrawCount;++i) DrawCommand(m_frame.m_DrawInfoArr[i], clipDepth);
    // A full command buffer may have dropped a trailing pop. Never leak clip state into the next frame.
    while (clipDepth) { ImGui::GetOverlayDrawList()->PopClipRect(); --clipDepth; }
    ImGui::Render();
    // Save before changing OM bindings, and restore those bindings before shader resources.
    D3D11StateSaver state;
    if(FAILED(state.saveCurrentState(g_pd3dDeviceContext))) return false;
    ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* oldDepth=nullptr;
    g_pd3dDeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,oldTargets,&oldDepth);
    auto* current=target.Get();
    g_pd3dDeviceContext->OMSetRenderTargets(1,&current,nullptr);
    g_pd3dDeviceContext->GSSetShader(nullptr,nullptr,0);
    if(device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_0) {
        g_pd3dDeviceContext->HSSetShader(nullptr,nullptr,0);
        g_pd3dDeviceContext->DSSetShader(nullptr,nullptr,0);
    }
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pd3dDeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,oldTargets,oldDepth);
    for(auto* p:oldTargets) if(p) p->Release();
    if(oldDepth) oldDepth->Release();
    state.restoreSavedState();
    const float fps=io.Framerate;
    LONG bits; memcpy(&bits,&fps,sizeof(bits));
    InterlockedExchange(&m_shared->fpsBits,bits);
    return true;
}
