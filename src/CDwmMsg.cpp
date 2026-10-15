#include "CDwmMsg.h"
#include <cstring>
#include <cmath>

namespace {
// Rasterizes a cubic bezier into the frame's point pool using adaptive-free fixed steps.
// ImGui itself has AddBezierCurve, but flattening here keeps the wire format pure points
// so polygons, polylines, triangles and beziers all share one code path.
int FlattenBezier(DwmVec2* out, int capacity,
    float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, int segments) {
    if (segments < 2) segments = 2;
    if (segments > capacity) segments = capacity;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float u = 1.0f - t;
        const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        out[i] = { a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3 };
    }
    return segments + 1;
}
}

CDwmMsg::~CDwmMsg() {
    if (m_owner == GetCurrentThreadId() && m_mutex) ReleaseMutex(m_mutex);
    if (m_pDrawList) UnmapViewOfFile(m_pDrawList);
    if (m_hFileMapping) CloseHandle(m_hFileMapping);
    if (m_mutex) CloseHandle(m_mutex);
}

bool CDwmMsg::InitMsg() {
    if (m_pDrawList) return true;
    // Default security: no world-writable NULL DACL. Both peers use the same session.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, m_mutexName.c_str());
    if (!mutex) return false;
    const DWORD wait = WaitForSingleObject(mutex, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) { CloseHandle(mutex); return false; }
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DRAWARR), m_mappingName.c_str());
    const bool fresh = mapping && GetLastError() != ERROR_ALREADY_EXISTS;
    auto* list = mapping ? static_cast<PDRWARR>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(DRAWARR))) : nullptr;
    if (list && fresh) {
        ZeroMemory(list, sizeof(DRAWARR));
        list->version = DRAW_VERSION;
        list->byteSize = sizeof(DRAWARR);
        list->magic = DRAW_MAGIC;
    }
    if (list && wait == WAIT_ABANDONED) { list->m_DrawCount = 0; list->m_PointCount = 0; }
    const bool valid = ValidDrawHeader(list);
    ReleaseMutex(mutex);
    if (!valid) {
        if (list) UnmapViewOfFile(list);
        if (mapping) CloseHandle(mapping);
        CloseHandle(mutex);
        return false;
    }
    m_mutex = mutex;
    m_hFileMapping = mapping;
    m_pDrawList = list;
    return true;
}

bool CDwmMsg::ExeStartDraw() {
    if (!m_pDrawList || m_owner) return false;
    const DWORD wait = WaitForSingleObject(m_mutex, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;
    if (!ValidDrawHeader(m_pDrawList)) { ReleaseMutex(m_mutex); return false; }
    m_owner = GetCurrentThreadId();
    m_pDrawList->m_DrawCount = 0;
    m_pDrawList->m_PointCount = 0;
    m_pDrawList->m_Overflow = 0;
    // Index 0 is reserved for the renderer default; the table always starts at 1.
    m_pDrawList->m_FontCount = 1;
    return true;
}

void CDwmMsg::ExeEndDraw() {
    if (!m_owner || m_owner != GetCurrentThreadId()) return;
    m_lastOverflow = m_pDrawList->m_Overflow != 0;
    ++m_pDrawList->frameNumber;
    m_owner = 0;
    ReleaseMutex(m_mutex); // Release/acquire publishes the whole frame together.
}
void CDwmMsg::CancelDraw() {
    if (!m_owner || m_owner != GetCurrentThreadId()) return;
    m_pDrawList->m_DrawCount = 0;
    m_pDrawList->m_PointCount = 0;
    ExeEndDraw();
}

void CDwmMsg::SetOverflow() {
    if (m_pDrawList && m_owner == GetCurrentThreadId()) m_pDrawList->m_Overflow = 1;
}

DrawInfo* CDwmMsg::Append(DRAWTYPE type) {
    if (!m_pDrawList || m_owner != GetCurrentThreadId()) return nullptr;
    const int count = m_pDrawList->m_DrawCount;
    if (count < 0 || count >= MAX_DRAW) { SetOverflow(); return nullptr; }
    DrawInfo* info = &m_pDrawList->m_DrawInfoArr[count];
    ZeroMemory(info, sizeof(*info));
    info->m_DrawType = type;
    m_pDrawList->m_DrawCount = count + 1;
    return info;
}

int CDwmMsg::AddFont(const char* family, float sizePixels, int ranges) {
    if (!m_pDrawList || m_owner != GetCurrentThreadId()) return DEFAULT_FONT_INDEX;
    DwmFontSpec spec{};
    if (family) {
        // Copy with an explicit bound; never trust the caller's length.
        size_t n = strnlen_s(family, FONT_NAME_LEN - 1);
        memcpy(spec.m_Family, family, n);
        spec.m_Family[n] = '\0';
    }
    spec.m_Size = sizePixels;
    spec.m_Ranges = ranges;
    const int count = m_pDrawList->m_FontCount;
    // Reuse an identical request so repeated text calls do not fill the table.
    for (int i = 1; i < count; ++i)
        if (SameFont(m_pDrawList->m_Fonts[i], spec)) return i;
    if (count <= 0 || count >= MAX_FONTS) { SetOverflow(); return DEFAULT_FONT_INDEX; }
    m_pDrawList->m_Fonts[count] = spec;
    m_pDrawList->m_FontCount = count + 1;
    return count;
}

void CDwmMsg::Text(DRAWTYPE type, float X, float Y, ImVec4 color, bool outlined, const char* str,
    int fontIndex, float size) {
    if (!str || !std::isfinite(X) || !std::isfinite(Y)) return;
    auto* info = Append(type);
    if (!info) return;
    auto& text = info->From.Text;
    text.m_X = X; text.m_Y = Y; text.m_Color = WireColor(color); text.m_Outlined = outlined;
    text.m_FontIndex = fontIndex;
    text.m_Size = size;
    // Keep UTF-8 truncation at a character boundary. The ANSI path stays compatible.
    size_t n = strnlen_s(str, BUF_LEN - 1);
    if (type == Text2_M && n == BUF_LEN - 1 && str[n] != '\0') {
        while (n && (static_cast<unsigned char>(str[n]) & 0xC0) == 0x80) --n;
    }
    memcpy(text.m_Str, str, n);
    text.m_Str[n] = '\0';
}
void CDwmMsg::DrawNewText(float x,float y,ImVec4 c,bool o,const char* s) { Text(Text_M,x,y,c,o,s); }
void CDwmMsg::DrawNewTextStr(float x,float y,ImVec4 c,bool o,const char* s) { Text(Text2_M,x,y,c,o,s); }
void CDwmMsg::DrawNewTextEx(float x,float y,ImVec4 c,bool o,const char* s,int fontIndex,float size) {
    Text(Text2_M,x,y,c,o,s,fontIndex,size);
}
void CDwmMsg::DrawCircleFilled(float x,float y,float r,ImVec4 c,int segments) {
    auto* p=Append(CircleFill_M); if(p) p->From.CircleFill={x,y,r,WireColor(c),segments};
}
void CDwmMsg::DrawCircle(float x,float y,float r,ImVec4 c,int segments,float t) {
    auto* p=Append(Circle_M); if(p) p->From.Circle={x,y,r,WireColor(c),segments,t};
}
void CDwmMsg::DrawRect(float x,float y,float w,float h,ImVec4 c,float t) {
    auto* p=Append(Rect_M); if(p) p->From.Rect={x,y,w,h,WireColor(c),t};
}
void CDwmMsg::DrawRectEx(float x,float y,float w,float h,ImVec4 c,float t) {
    auto* p=Append(RectEx_M); if(p) p->From.RectEx={x,y,w,h,WireColor(c),t};
}
void CDwmMsg::DrawFilledRect(float x,float y,float w,float h,ImVec4 c) {
    auto* p=Append(FilledRect_M); if(p) p->From.FilledRect={x,y,w,h,WireColor(c)};
}
void CDwmMsg::DrawLine(float x1,float y1,float x2,float y2,ImVec4 c,float t) {
    auto* p=Append(Line_M); if(p) p->From.Line={x1,y1,x2,y2,WireColor(c),t};
}
void CDwmMsg::DrawRoundRect(float x,float y,float w,float h,float radius,ImVec4 c,float t,bool filled) {
    auto* p=Append(RoundRect_M);
    if(p) p->From.RoundRect={x,y,w,h,radius,WireColor(c),t,filled?1:0};
}
void CDwmMsg::DrawEllipse(float x,float y,float rx,float ry,ImVec4 c,float t,int segments,bool filled) {
    auto* p=Append(Ellipse_M);
    if(p) p->From.Ellipse={x,y,rx,ry,WireColor(c),t,segments,filled?1:0};
}
int CDwmMsg::AddPoints(const DwmVec2* points, int count) {
    if (!m_pDrawList || m_owner != GetCurrentThreadId() || !points || count <= 0) { SetOverflow(); return -1; }
    const int first = m_pDrawList->m_PointCount;
    // A single command may need every pool slot; require the whole run to fit.
    if (first < 0 || count > MAX_POINTS - first) { SetOverflow(); return -1; }
    memcpy(m_pDrawList->m_Points + first, points, sizeof(DwmVec2) * static_cast<size_t>(count));
    m_pDrawList->m_PointCount = first + count;
    return first;
}
void CDwmMsg::DrawPolygon(const DwmVec2* points, int count, ImVec4 c, float t, bool filled, bool closed) {
    if (!points || count < 2) return;
    int effective = count;
    if (effective > MAX_POINTS) { effective = MAX_POINTS; SetOverflow(); }
    const int first = AddPoints(points, effective);
    if (first < 0) return;
    auto* p = Append(Points_M);
    if (!p) return;
    p->From.Points = {WireColor(c), t, first, effective, filled ? 1 : 0, closed ? 1 : 0};
}
void CDwmMsg::DrawBezier(float x0,float y0,float x1,float y1,float x2,float y2,float x3,float y3,
    ImVec4 c,float t,int segments) {
    // Reserve the pool first so a failed append does not leave orphaned vertices.
    DwmVec2 buffer[257];
    const int budget = static_cast<int>(sizeof(buffer) / sizeof(buffer[0]));
    if (segments >= budget) { segments = budget - 1; SetOverflow(); }
    const int count = FlattenBezier(buffer, budget, x0, y0, x1, y1, x2, y2, x3, y3, segments);
    if (count < 2) return;
    const int first = AddPoints(buffer, count);
    if (first < 0) return;
    auto* p = Append(Points_M);
    if (!p) return;
    p->From.Points = {WireColor(c), t, first, count, 0, 0};
}
void CDwmMsg::DrawArc(float x,float y,float r,float start,float end,ImVec4 c,float t,int segments) {
    if (segments < 2) segments = 2;
    const int budget = 1025;
    if (segments >= budget) { segments = budget - 1; SetOverflow(); }
    DwmVec2 buffer[1025];
    if (!std::isfinite(start) || !std::isfinite(end)) return;
    const float step = (end - start) / static_cast<float>(segments);
    for (int i = 0; i <= segments; ++i) {
        const float angle = (start + step * static_cast<float>(i)) * 3.14159265358979323846f / 180.0f;
        buffer[i] = { x + r * std::cos(angle), y + r * std::sin(angle) };
    }
    const int first = AddPoints(buffer, segments + 1);
    if (first < 0) return;
    auto* p = Append(Points_M);
    if (!p) return;
    p->From.Points = {WireColor(c), t, first, segments + 1, 0, 0};
}
float CDwmMsg::GetFps() {
    if (!m_pDrawList) return 0;
    LONG bits = InterlockedCompareExchange(&m_pDrawList->fpsBits, 0, 0);
    float fps; memcpy(&fps, &bits, sizeof(fps)); return fps;
}
void CDwmMsg::PushClipRect(float x, float y, float width, float height) {
    auto* p = Append(ClipPush_M);
    if (p) p->From.Rect = {x, y, width, height, {0, 0, 0, 0}, 0};
}
void CDwmMsg::PopClipRect() { Append(ClipPop_M); }
