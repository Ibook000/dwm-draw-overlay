#pragma once
#include <Windows.h>
#include <cstdint>
#include <type_traits>
#include "imgui.h"

// Producers and the overlay must use this protocol version, including clip commands.
constexpr int BUF_LEN = 256;
constexpr int MAX_DRAW = 500;
constexpr wchar_t MAPNAME[] = L"Local\\DwmDraw.Commands.v3";
constexpr wchar_t MUTEXNAME[] = L"Local\\DwmDraw.Commands.v3.Lock";
constexpr LONG DRAW_MAGIC = 0x334D5744;
constexpr std::uint32_t DRAW_VERSION = 3;

// No constructors in wire members: mapped storage and union payloads remain POD.
struct DrawColor {
    float x, y, z, w;
    operator ImVec4() const { return ImVec4(x,y,z,w); }
};
inline DrawColor WireColor(ImVec4 c) { return {c.x,c.y,c.z,c.w}; }
struct DrawTextStr { float m_X, m_Y; DrawColor m_Color; bool m_Outlined; char m_Str[BUF_LEN]; };
struct DrawCircleFillStr { float m_X, m_Y, m_Radius; DrawColor m_Color; int m_Segments; };
struct DrawCircleStr { float m_X, m_Y, m_Radius; DrawColor m_Color; int m_Segments; float m_thickness; };
struct DrawRectStr { float m_X, m_Y, m_W, m_H; DrawColor m_Color; float m_thickness; };
using DrawRectExStr = DrawRectStr;
struct DrawFilledRectStr { float m_X, m_Y, m_W, m_H; DrawColor m_Color; };
struct DrawLineStr { float m_X1, m_Y1, m_X2, m_Y2; DrawColor m_Color; float thickness; };
struct FPS { float Fps; };
enum DRAWTYPE { Text_M, Text2_M, CircleFill_M, Circle_M, Rect_M, RectEx_M, FilledRect_M, Line_M, FPS_M, ClipPush_M, ClipPop_M };
struct DrawInfo {
    DRAWTYPE m_DrawType;
    union Payload {
        DrawTextStr Text;
        DrawCircleFillStr CircleFill;
        DrawCircleStr Circle;
        DrawRectStr Rect;
        DrawRectExStr RectEx;
        DrawFilledRectStr FilledRect;
        DrawLineStr Line;
        FPS Fps;
    } From;
};
struct DRAWARR {
    LONG magic;
    std::uint32_t version;
    std::uint32_t byteSize;
    std::uint32_t frameNumber;
    LONG fpsBits;
    int m_DrawCount;
    DrawInfo m_DrawInfoArr[MAX_DRAW];
};
using PDRWARR = DRAWARR*;
constexpr DWORD MAPINGLEN = sizeof(DRAWARR);
static_assert(sizeof(ImVec4) == 16 && sizeof(DrawInfo) == 288, "Unexpected command ABI");
static_assert(sizeof(DRAWARR) == 144024, "Unexpected shared-memory ABI");
static_assert(std::is_trivially_copyable<DRAWARR>::value, "Protocol must be byte-copyable");
inline bool ValidDrawHeader(const DRAWARR* p) {
    return p && p->magic == DRAW_MAGIC && p->version == DRAW_VERSION && p->byteSize == sizeof(DRAWARR);
}
