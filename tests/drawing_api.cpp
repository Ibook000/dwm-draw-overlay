#include "DwmDraw.h"
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
    draw.end();
    HANDLE handle = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping.c_str()); CHECK(handle);
    const auto* frame = static_cast<const DRAWARR*>(MapViewOfFile(handle, FILE_MAP_READ, 0, 0, sizeof(DRAWARR))); CHECK(frame);
    CHECK(ValidDrawHeader(frame)); CHECK(frame->m_DrawCount == 7);
    const auto& text = frame->m_DrawInfoArr[0];
    CHECK(text.m_DrawType == Text2_M); CHECK(!strcmp(text.From.Text.m_Str, u8"你好"));
    CHECK(text.From.Text.m_Outlined); CHECK(text.From.Text.m_X == 20 && text.From.Text.m_Y == 30);
    CHECK(std::fabs(text.From.Text.m_Color.y - 128.0f / 255) < 0.00001f);
    CHECK(std::fabs(text.From.Text.m_Color.w - 64.0f / 255) < 0.00001f);
    CHECK(frame->m_DrawInfoArr[1].From.Rect.m_W == 70);
    CHECK(frame->m_DrawInfoArr[1].From.Rect.m_thickness == 3);
    CHECK(draw.clear()); CHECK(frame->m_DrawCount == 0);
    UnmapViewOfFile(frame); CloseHandle(handle);
    puts("PASS: UTF-8 text, RGBA conversion, shape commands, frame submission and clearing.");
    return 0;
}
