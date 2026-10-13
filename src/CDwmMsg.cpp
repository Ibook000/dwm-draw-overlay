#include "CDwmMsg.h"
#include <cstring>
#include <cmath>

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
    if (list && wait == WAIT_ABANDONED) list->m_DrawCount = 0;
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
    return true;
}

void CDwmMsg::ExeEndDraw() {
    if (!m_owner || m_owner != GetCurrentThreadId()) return;
    ++m_pDrawList->frameNumber;
    m_owner = 0;
    ReleaseMutex(m_mutex); // Release/acquire publishes the whole frame together.
}
void CDwmMsg::CancelDraw() {
    if (!m_owner || m_owner != GetCurrentThreadId()) return;
    m_pDrawList->m_DrawCount = 0;
    ExeEndDraw();
}

DrawInfo* CDwmMsg::Append(DRAWTYPE type) {
    if (!m_pDrawList || m_owner != GetCurrentThreadId()) return nullptr;
    const int count = m_pDrawList->m_DrawCount;
    if (count < 0 || count >= MAX_DRAW) return nullptr;
    DrawInfo* info = &m_pDrawList->m_DrawInfoArr[count];
    ZeroMemory(info, sizeof(*info));
    info->m_DrawType = type;
    m_pDrawList->m_DrawCount = count + 1;
    return info;
}

void CDwmMsg::Text(DRAWTYPE type, float X, float Y, ImVec4 color, bool outlined, const char* str) {
    if (!str || !std::isfinite(X) || !std::isfinite(Y)) return;
    auto* info = Append(type);
    if (!info) return;
    auto& text = info->From.Text;
    text.m_X = X; text.m_Y = Y; text.m_Color = WireColor(color); text.m_Outlined = outlined;
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
