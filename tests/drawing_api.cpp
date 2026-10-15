#include "DwmDraw.h"
#include "DwmHotkeys.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
int main() {
    const auto mapping = L"Local\\DwmDraw.ApiTest." + std::to_wstring(GetCurrentProcessId());
    const auto mutex = mapping + L".Lock";
    DwmDraw draw(mapping.c_str(), mutex.c_str());
    CHECK(draw.init()); CHECK(draw.begin());
    draw.text(20, 30, u8"你好", {255, 128, 0, 64});
    draw.rect(50, 60, 70, 80, {0, 255, 0, 255}, 3);
    draw.line(1, 2, 3, 4); draw.circle(100, 100, 20);
    draw.filledRect(1, 2, 3, 4); draw.filledCircle(5, 6, 7); draw.cornerRect(1, 2, 3, 4);
    // New primitives.
    draw.roundRect(10, 10, 100, 40, 8, {}, 2);
    draw.filledRoundRect(10, 60, 100, 40, 8);
    draw.ellipse(200, 100, 40, 20, {}, 2, 32);
    draw.filledEllipse(200, 200, 40, 20);
    draw.triangle(0, 0, 20, 0, 10, 20, {}, 2, true);
    const DwmVec2 quad[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    draw.polygon(quad, 4, {}, 2, false, true);
    draw.polyline(quad, 4, {}, 2);
    draw.bezier(0, 0, 10, 10, 20, -10, 30, 0, {}, 2, 8);
    draw.arc(300, 300, 25, 0, 180, {}, 2, 16);
    // Custom font selection.
    draw.text(20, 300, u8"大号字", DwmFont{"SimHei", 28, DwmRanges_Auto}, {255, 255, 0, 255});
    draw.end();
    CHECK(!draw.overflowed());
    HANDLE handle = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping.c_str()); CHECK(handle);
    const auto* frame = static_cast<const DRAWARR*>(MapViewOfFile(handle, FILE_MAP_READ, 0, 0, sizeof(DRAWARR))); CHECK(frame);
    CHECK(ValidDrawHeader(frame)); CHECK(frame->m_DrawCount == 17);
    const auto& text = frame->m_DrawInfoArr[0];
    CHECK(text.m_DrawType == Text2_M); CHECK(!strcmp(text.From.Text.m_Str, u8"你好"));
    CHECK(text.From.Text.m_Outlined); CHECK(text.From.Text.m_X == 20 && text.From.Text.m_Y == 30);
    CHECK(std::fabs(text.From.Text.m_Color.y - 128.0f / 255) < 0.00001f);
    CHECK(std::fabs(text.From.Text.m_Color.w - 64.0f / 255) < 0.00001f);
    // Default text keeps index 0 (renderer default) and no explicit size.
    CHECK(text.From.Text.m_FontIndex == 0); CHECK(text.From.Text.m_Size == 0);
    CHECK(frame->m_DrawInfoArr[1].From.Rect.m_W == 70);
    CHECK(frame->m_DrawInfoArr[1].From.Rect.m_thickness == 3);
    // Geometry commands landed with the expected types and flags.
    CHECK(frame->m_DrawInfoArr[7].m_DrawType == RoundRect_M);
    CHECK(frame->m_DrawInfoArr[7].From.RoundRect.m_Radius == 8);
    CHECK(frame->m_DrawInfoArr[7].From.RoundRect.m_Filled == 0);
    CHECK(frame->m_DrawInfoArr[8].From.RoundRect.m_Filled == 1);
    CHECK(frame->m_DrawInfoArr[9].m_DrawType == Ellipse_M);
    CHECK(frame->m_DrawInfoArr[9].From.Ellipse.m_RX == 40 && frame->m_DrawInfoArr[9].From.Ellipse.m_Filled == 0);
    CHECK(frame->m_DrawInfoArr[10].From.Ellipse.m_Filled == 1);
    CHECK(frame->m_DrawInfoArr[11].m_DrawType == Points_M);
    CHECK(frame->m_DrawInfoArr[11].From.Points.m_Count == 3 && frame->m_DrawInfoArr[11].From.Points.m_Filled == 1);
    CHECK(frame->m_DrawInfoArr[12].From.Points.m_Count == 4 && frame->m_DrawInfoArr[12].From.Points.m_Closed == 1);
    CHECK(frame->m_DrawInfoArr[13].From.Points.m_Closed == 0);
    // The bezier flattened into segments+1 vertices.
    CHECK(frame->m_DrawInfoArr[14].From.Points.m_Count == 9);
    CHECK(frame->m_DrawInfoArr[15].From.Points.m_Count == 17); // arc segments+1
    // The custom font was registered and the last text command references it.
    CHECK(frame->m_FontCount == 2);
    CHECK(!strcmp(frame->m_Fonts[1].m_Family, "SimHei"));
    CHECK(frame->m_Fonts[1].m_Size == 28);
    const auto& custom = frame->m_DrawInfoArr[16];
    CHECK(custom.m_DrawType == Text2_M);
    CHECK(custom.From.Text.m_FontIndex == 1);
    CHECK(custom.From.Text.m_Size == 28);
    // The point pool holds 3 + 4 + 4 + 9 + 17 vertices in total.
    CHECK(frame->m_PointCount == 37);
    CHECK(draw.clear()); CHECK(frame->m_DrawCount == 0);
    UnmapViewOfFile(frame); CloseHandle(handle);

    // Hotkeys: registration, duplicate rejection and cleanup.
    const int hotkey = DwmHotkeys::Register('Q', MOD_CONTROL | MOD_ALT, false);
    CHECK(hotkey > 0);
    // The same combination cannot be registered twice on one thread.
    CHECK(DwmHotkeys::Register('Q', MOD_CONTROL | MOD_ALT, false) == 0);
    CHECK(GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED);
    // Other applications may already own a combination, so probe until one is free
    // rather than assuming a particular key is available on every machine.
    int second = 0;
    for (int offset = 0; offset < 8 && !second; ++offset)
        second = DwmHotkeys::Register('W' + offset, MOD_CONTROL | MOD_SHIFT, false);
    CHECK(second > 0);
    CHECK(second != hotkey);
    CHECK(DwmHotkeys::Register('Q', 0, false) == 0);                    // needs a modifier
    CHECK(DwmHotkeys::Register(0, MOD_CONTROL, false) == 0);            // invalid key
    CHECK(!DwmHotkeys::Consume(hotkey));                                // nothing pressed
    CHECK(!DwmHotkeys::IsDown(0xFF));                                   // invalid vk is false
    CHECK(DwmHotkeys::Unregister(second));
    CHECK(DwmHotkeys::Unregister(hotkey));
    CHECK(!DwmHotkeys::Unregister(hotkey));                             // already gone
    puts("PASS: text fonts/sizes, geometry commands, point pool, overflow flag and hotkeys.");
    return 0;
}
