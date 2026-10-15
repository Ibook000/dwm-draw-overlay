#include "save_state.hpp"
#include "CIMGuiDraw.h"
#include "DwmPrimitives.h"
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>
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
int ClampSegments(int segments) { return (std::max)(3, (std::min)(512, segments)); }

// Copies a point run out of the frame's pool. Returns false when the range is invalid,
// which keeps a corrupt producer from reading out of bounds.
bool CollectPoints(const DRAWARR& frame, const DrawPointsStr& p, std::vector<ImVec2>& out) {
    if (p.m_First < 0 || p.m_Count < 2 || p.m_Count > MAX_POINTS) return false;
    if (p.m_First > MAX_POINTS - p.m_Count) return false;
    out.resize(static_cast<size_t>(p.m_Count));
    for (int i = 0; i < p.m_Count; ++i) {
        const DwmVec2& source = frame.m_Points[p.m_First + i];
        if (!Position(source.x) || !Position(source.y)) return false;
        out[static_cast<size_t>(i)] = ImVec2(source.x, source.y);
    }
    return true;
}

// p is non-const because Color() clamps out-of-range channels in place before drawing.
void DrawCommand(const DRAWARR& frame, DrawInfo& p, const ImFont* const* fonts,
    const float* sizes, unsigned& clipDepth) {
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
        auto t=p.From.Text;
        if(!Position(t.m_X)||!Position(t.m_Y)||!Color(t.m_Color)) break;
        t.m_Str[BUF_LEN-1]='\0';
        // An out-of-range index degrades to the default font rather than dropping the text.
        const int index = (t.m_FontIndex >= 0 && t.m_FontIndex < MAX_FONTS) ? t.m_FontIndex : 0;
        const ImFont* font = fonts[index];
        float size = t.m_Size > 0 ? t.m_Size : sizes[index];
        if (size <= 0) size = ImGuiDx11_defaultFontSize();
        if(p.m_DrawType==Text_M) DrawNewTextEx(font, size, t.m_X, t.m_Y, t.m_Color, t.m_Outlined, t.m_Str);
        else DrawNewTextEx(font, size, t.m_X, t.m_Y, t.m_Color, t.m_Outlined, t.m_Str);
        break;
    }
    case CircleFill_M: {
        auto& c=p.From.CircleFill;
        if(!Position(c.m_X)||!Position(c.m_Y)||!Position(c.m_Radius)||c.m_Radius<=0||!Color(c.m_Color)) break;
        DrawCircleFilled(c.m_X,c.m_Y,c.m_Radius,c.m_Color,ClampSegments(c.m_Segments)); break;
    }
    case Circle_M: {
        auto& c=p.From.Circle;
        if(!Position(c.m_X)||!Position(c.m_Y)||!Position(c.m_Radius)||c.m_Radius<=0||!Color(c.m_Color)||!Thickness(c.m_thickness)) break;
        DrawCircle(c.m_X,c.m_Y,c.m_Radius,c.m_Color,ClampSegments(c.m_Segments),c.m_thickness); break;
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
    case RoundRect_M: {
        auto& r=p.From.RoundRect;
        if(!Position(r.m_X)||!Position(r.m_Y)||!Position(r.m_W)||!Position(r.m_H)||!Position(r.m_Radius)||
           r.m_W<0||r.m_H<0||r.m_Radius<0||!Color(r.m_Color)) break;
        const bool filled = r.m_Filled != 0;
        // A stroked rounded rect needs a valid thickness; a filled one does not.
        if(!filled && !Thickness(r.m_thickness)) break;
        DrawRoundRect(r.m_X,r.m_Y,r.m_W,r.m_H,r.m_Radius,r.m_Color,r.m_thickness,filled);
        break;
    }
    case Ellipse_M: {
        auto& e=p.From.Ellipse;
        if(!Position(e.m_X)||!Position(e.m_Y)||!Position(e.m_RX)||!Position(e.m_RY)||
           e.m_RX<=0||e.m_RY<=0||!Color(e.m_Color)) break;
        const bool filled = e.m_Filled != 0;
        if(!filled && !Thickness(e.m_thickness)) break;
        DrawEllipse(e.m_X,e.m_Y,e.m_RX,e.m_RY,e.m_Color,e.m_thickness,ClampSegments(e.m_Segments),filled);
        break;
    }
    case Points_M: {
        auto& pnt=p.From.Points;
        if(!Color(pnt.m_Color)) break;
        const bool filled = pnt.m_Filled != 0;
        if(!filled && !Thickness(pnt.m_thickness)) break;
        std::vector<ImVec2> points;
        if(!CollectPoints(frame,pnt,points)) break;
        DrawPoints(points.data(),static_cast<int>(points.size()),pnt.m_Color,pnt.m_thickness,
            filled,pnt.m_Closed!=0);
        break;
    }
    case Arc_M: {
        auto& a=p.From.Arc;
        if(!Position(a.m_X)||!Position(a.m_Y)||!Position(a.m_Radius)||!Position(a.m_Start)||!Position(a.m_End)||
           a.m_Radius<=0||!Color(a.m_Color)||!Thickness(a.m_thickness)) break;
        const int segments = (std::max)(2,(std::min)(1024,a.m_Segments));
        std::vector<ImVec2> points(static_cast<size_t>(segments)+1);
        const float step=(a.m_End-a.m_Start)/static_cast<float>(segments);
        for(int i=0;i<=segments;++i){
            const float angle=(a.m_Start+step*static_cast<float>(i))*3.14159265358979323846f/180.0f;
            points[static_cast<size_t>(i)]=ImVec2(a.m_X+a.m_Radius*std::cos(angle),a.m_Y+a.m_Radius*std::sin(angle));
        }
        DrawPoints(points.data(),segments+1,a.m_Color,a.m_thickness,false,false);
        break;
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
            if(valid && wait==WAIT_ABANDONED) { shared->m_DrawCount=0; shared->m_PointCount=0; ++shared->frameNumber; }
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
    OutputDebugStringA("[DwmDraw] Shared protocol v4 connected.\n");
    return true;
}
bool CIMGuiDraw::Snapshot() {
    const auto wait=WaitForSingleObject(m_mutex,0); // Never block the rendering thread on the producer.
    if(wait==WAIT_TIMEOUT) return true; // Draw the last complete frame.
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) return false;
    bool valid=ValidDrawHeader(m_shared);
    if(valid) {
        if(wait==WAIT_ABANDONED) { m_shared->m_DrawCount=0; m_shared->m_PointCount=0; ++m_shared->frameNumber; }
        const int count=m_shared->m_DrawCount;
        const int points=m_shared->m_PointCount;
        const int fonts=m_shared->m_FontCount;
        if(count<0 || count>MAX_DRAW || points<0 || points>MAX_POINTS || fonts<0 || fonts>MAX_FONTS) {
            m_frame.m_DrawCount=0;
            m_frame.m_PointCount=0;
            m_frame.m_FontCount=0;
            OutputDebugStringA("[DwmDraw] Rejected invalid frame header.\n");
        } else {
            m_frame.m_DrawCount=count;
            m_frame.m_PointCount=points;
            m_frame.m_FontCount=fonts;
            m_frame.m_Overflow=m_shared->m_Overflow;
            memcpy(m_frame.m_DrawInfoArr,m_shared->m_DrawInfoArr,static_cast<size_t>(count)*sizeof(DrawInfo));
            // Points are copied in one block; only the first `points` entries are live.
            memcpy(m_frame.m_Points,m_shared->m_Points,static_cast<size_t>(points)*sizeof(DwmVec2));
            if(fonts>0) memcpy(m_frame.m_Fonts,m_shared->m_Fonts,static_cast<size_t>(fonts)*sizeof(DwmFontSpec));
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
    // Fonts resolved so far belong to the destroyed atlas; drop the cache with it.
    m_fonts = DwmFontLibrary();
    for (int i = 0; i < MAX_FONTS; ++i) { m_resolvedFonts[i] = nullptr; m_resolvedSizes[i] = 0; }
    const bool ok=ImGuiDx11_init(chain,device);
    if(!ok) { ImGui::DestroyContext(m_context); m_context=nullptr; }
    ImGui::SetCurrentContext(previous);
    QueryPerformanceFrequency(&m_frequency);
    return ok;
}
void CIMGuiDraw::PrepareFonts() {
    // Slot 0 is the built-in default font: no ImFont, and the renderer's default size.
    m_resolvedFonts[0] = nullptr;
    m_resolvedSizes[0] = ImGuiDx11_defaultFontSize();
    const int count = m_frame.m_FontCount;
    for (int i = 1; i < count && i < MAX_FONTS; ++i) {
        float size = ImGuiDx11_defaultFontSize();
        m_resolvedFonts[i] = m_fonts.acquire(m_frame.m_Fonts[i], size);
        m_resolvedSizes[i] = size;
    }
    // A font added now invalidates the atlas texture; re-upload it before NewFrame() so
    // the glyphs used this frame actually exist in the texture.
    if (m_fonts.consumeAtlasDirty() && !ImGuiDx11_refreshFontTexture()) {
        OutputDebugStringA("[DwmDraw] Font texture refresh failed.\n");
    }
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
    // Resolve fonts before NewFrame(): the atlas is Locked from NewFrame() to Render().
    PrepareFonts();
    ImGuiIO& io=ImGui::GetIO();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const double delta=m_lastTime.QuadPart && m_frequency.QuadPart ? double(now.QuadPart-m_lastTime.QuadPart)/double(m_frequency.QuadPart) : 1.0/60.0;
    m_lastTime=now;
    io.DeltaTime=static_cast<float>((std::max)(0.000001,(std::min)(1.0,delta)));
    io.DisplaySize=ImVec2(static_cast<float>(desc.Width),static_cast<float>(desc.Height));
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    unsigned clipDepth = 0;
    // DrawInfo is mutated in place by the per-command colour clamping, so iterate non-const.
    for(int i=0;i<m_frame.m_DrawCount;++i) DrawCommand(m_frame, m_frame.m_DrawInfoArr[i], m_resolvedFonts, m_resolvedSizes, clipDepth);
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
