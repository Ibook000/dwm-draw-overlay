#include "DwmScrollPanel.h"
#include <cstdio>
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
int main() {
    const auto name = L"Local\\DwmDraw.ScrollTest." + std::to_wstring(GetCurrentProcessId());
    const auto mutex = name + L".Lock";
    DwmDraw draw(name.c_str(), mutex.c_str());
    DwmScrollPanel panel;
    CHECK(panel.setPosition(200, 150)); CHECK(panel.setSize(400, 300));
    CHECK(panel.setContentHeight(1000)); CHECK(panel.setScrollStep(48));
    CHECK(!panel.handleWheel(-120, 199, 150)); CHECK(panel.scrollOffset() == 0);
    CHECK(panel.handleWheel(-120, 210, 160)); CHECK(panel.scrollOffset() == 48);
    CHECK(panel.handleWheel(60, 210, 160)); CHECK(panel.scrollOffset() == 24);
    panel.setInputEnabled(false); CHECK(!panel.handleWheel(-120, 210, 160));
    panel.setInputEnabled(true); CHECK(panel.scrollTo(9999)); CHECK(panel.scrollOffset() == 700);
    CHECK(panel.setContentHeight(320)); CHECK(panel.scrollOffset() == 20);
    CHECK(panel.setContentHeight(1000)); CHECK(panel.scrollTo(80));
    CHECK(draw.init()); CHECK(draw.setPosition(9, 11)); CHECK(draw.begin());
    CHECK(panel.begin(draw)); CHECK(!panel.begin(draw));
    CHECK(!panel.setPosition(1, 2)); // Moving an active panel would desynchronize drawing and hit testing.
    draw.text(12, 24, u8"滚动测试"); panel.end(draw);
    CHECK(draw.positionX() == 9 && draw.positionY() == 11);
    draw.line(0, 0, 10, 10); draw.end();
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()); CHECK(mapping);
    const auto* frame = static_cast<const DRAWARR*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(DRAWARR))); CHECK(frame);
    CHECK(frame->m_DrawCount == 5);
    const auto* commands = frame->m_DrawInfoArr;
    CHECK(commands[0].m_DrawType == ClipPush_M);
    CHECK(commands[0].From.Rect.m_X == 200 && commands[0].From.Rect.m_Y == 150);
    CHECK(commands[1].From.FilledRect.m_Y == 150); // Background stays fixed while content scrolls.
    CHECK(commands[2].From.Text.m_X == 212 && commands[2].From.Text.m_Y == 94);
    CHECK(commands[3].m_DrawType == ClipPop_M);
    CHECK(commands[4].From.Line.m_X1 == 9 && commands[4].From.Line.m_Y1 == 11);
    CHECK(!panel.setSize(0, 300)); CHECK(!panel.setScrollStep(-1));
    UnmapViewOfFile(frame); CloseHandle(mapping);
    puts("PASS: wheel hit testing/direction, partial delta, disabled input, bounds, clip commands and coordinate restoration.");
    return 0;
}
