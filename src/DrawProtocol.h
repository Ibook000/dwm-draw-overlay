#pragma once
#include <Windows.h>
#include <cstdint>
#include <type_traits>
#include "imgui.h"

// Producers and the overlay must use this protocol version, including clip commands.
constexpr int BUF_LEN = 256;
constexpr int MAX_DRAW = 500;
constexpr int MAX_POINTS = 4096;   // Shared vertex pool for polygons, polylines, beziers and arcs.
constexpr int MAX_FONTS = 16;      // Per-frame font request table (family + pixel size).
constexpr int FONT_NAME_LEN = 64;  // UTF-8 family name or full font file path.
// Pixel size used when a text command (and its font request) leaves the size at 0.
constexpr float DEFAULT_FONT_SIZE = 16.0f;
constexpr wchar_t MAPNAME[] = L"Local\\DwmDraw.Commands.v4";
constexpr wchar_t MUTEXNAME[] = L"Local\\DwmDraw.Commands.v4.Lock";
constexpr LONG DRAW_MAGIC = 0x344D5744; // 'DW M4' little-endian: "DWM4".
constexpr std::uint32_t DRAW_VERSION = 4;

// No constructors in wire members: mapped storage and union payloads remain POD.
struct DrawColor {
    float x, y, z, w;
    operator ImVec4() const { return ImVec4(x,y,z,w); }
};
inline DrawColor WireColor(ImVec4 c) { return {c.x,c.y,c.z,c.w}; }

struct DwmVec2 { float x, y; };

// Glyph coverage requested for a font. Auto picks a sensible set from the family name.
enum DwmGlyphRanges {
    DwmRanges_Auto = 0,
    DwmRanges_Default,   // Latin + Latin supplement
    DwmRanges_Chinese,   // ~2500 common simplified Chinese glyphs
    DwmRanges_Japanese,
    DwmRanges_Korean,
    DwmRanges_Cyrillic,
    DwmRanges_Count
};

// One font request. The producer fills the frame's table; text commands reference it by index.
struct DwmFontSpec {
    char m_Family[FONT_NAME_LEN]; // UTF-8 family name, or a path when it contains '\\' or ':'.
    float m_Size;                 // Pixel height. <= 0 means "renderer default".
    int m_Ranges;                 // DwmGlyphRanges.
};
inline bool SameFont(const DwmFontSpec& a, const DwmFontSpec& b) {
    return a.m_Size == b.m_Size && a.m_Ranges == b.m_Ranges && !strncmp(a.m_Family, b.m_Family, FONT_NAME_LEN);
}

struct DrawTextStr {
    float m_X, m_Y;
    DrawColor m_Color;
    bool m_Outlined;
    float m_Size;      // Accepted text size; 0 selects the font request's size.
    int m_FontIndex;   // Index into DRAWARR::m_Fonts; 0 is the built-in default font.
    char m_Str[BUF_LEN];
};
struct DrawCircleFillStr { float m_X, m_Y, m_Radius; DrawColor m_Color; int m_Segments; };
struct DrawCircleStr { float m_X, m_Y, m_Radius; DrawColor m_Color; int m_Segments; float m_thickness; };
struct DrawRectStr { float m_X, m_Y, m_W, m_H; DrawColor m_Color; float m_thickness; };
using DrawRectExStr = DrawRectStr;
struct DrawFilledRectStr { float m_X, m_Y, m_W, m_H; DrawColor m_Color; };
struct DrawLineStr { float m_X1, m_Y1, m_X2, m_Y2; DrawColor m_Color; float thickness; };
struct FPS { float Fps; };
struct DrawRoundRectStr { float m_X, m_Y, m_W, m_H, m_Radius; DrawColor m_Color; float m_thickness; int m_Filled; };
struct DrawEllipseStr { float m_X, m_Y, m_RX, m_RY; DrawColor m_Color; float m_thickness; int m_Segments; int m_Filled; };
// Polygon / polyline / triangle / bezier share the frame's point pool.
struct DrawPointsStr { DrawColor m_Color; float m_thickness; int m_First; int m_Count; int m_Filled; int m_Closed; };
struct DrawArcStr { float m_X, m_Y, m_Radius, m_Start, m_End; DrawColor m_Color; float m_thickness; int m_Segments; };

enum DRAWTYPE {
    Text_M, Text2_M, CircleFill_M, Circle_M, Rect_M, RectEx_M, FilledRect_M, Line_M, FPS_M, ClipPush_M, ClipPop_M,
    RoundRect_M, Ellipse_M, Points_M, Arc_M
};
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
        DrawRoundRectStr RoundRect;
        DrawEllipseStr Ellipse;
        DrawPointsStr Points;
        DrawArcStr Arc;
    } From;
};
struct DRAWARR {
    LONG magic;
    std::uint32_t version;
    std::uint32_t byteSize;
    std::uint32_t frameNumber;
    LONG fpsBits;
    int m_DrawCount;
    int m_PointCount;
    // Producers set this when a frame exceeded MAX_DRAW or MAX_POINTS, so callers can
    // detect dropped commands instead of silently rendering an incomplete frame.
    int m_Overflow;
    int m_FontCount;
    DwmFontSpec m_Fonts[MAX_FONTS];
    DrawInfo m_DrawInfoArr[MAX_DRAW];
    DwmVec2 m_Points[MAX_POINTS];
};
using PDRWARR = DRAWARR*;
static_assert(sizeof(ImVec4) == 16 && sizeof(DwmFontSpec) == 72, "Unexpected command ABI");
static_assert(sizeof(DrawInfo) == 296, "Unexpected command ABI");
static_assert(sizeof(DRAWARR) == 181956, "Unexpected shared-memory ABI");
static_assert(std::is_trivially_copyable<DRAWARR>::value, "Protocol must be byte-copyable");
inline bool ValidDrawHeader(const DRAWARR* p) {
    return p && p->magic == DRAW_MAGIC && p->version == DRAW_VERSION && p->byteSize == sizeof(DRAWARR);
}
