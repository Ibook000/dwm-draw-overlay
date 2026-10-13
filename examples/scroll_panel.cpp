#include "DwmScrollPanel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
volatile LONG running = 1;
BOOL WINAPI Stop(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&running, 0); return TRUE;
    }
    return FALSE;
}
}
int main(int argc, char** argv) {
    float x = 80, y = 100, width = 460, height = 320, step = 48;
    bool once = false, checkInput = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--once")) { once = true; continue; }
        if (!strcmp(argv[i], "--check-input")) { checkInput = true; continue; }
        float* setting = nullptr;
        if (!strcmp(argv[i], "--x")) setting = &x;
        else if (!strcmp(argv[i], "--y")) setting = &y;
        else if (!strcmp(argv[i], "--width")) setting = &width;
        else if (!strcmp(argv[i], "--height")) setting = &height;
        else if (!strcmp(argv[i], "--step")) setting = &step;
        if (!setting || i + 1 >= argc) {
            puts("Usage: scroll_example [--x 80] [--y 100] [--width 460] [--height 320] [--step 48] [--once | --check-input]"); return 2;
        }
        char* end = nullptr;
        *setting = std::strtof(argv[++i], &end);
        if (!*argv[i] || *end || !std::isfinite(*setting)) { puts("Invalid numeric setting."); return 2; }
    }
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        printf("Cannot enable physical-pixel coordinates. Win32=%lu\n", GetLastError()); return 1;
    }
    DwmDraw draw;
    DwmScrollPanel panel;
    if (!panel.setPosition(x, y) || !panel.setSize(width, height) || !panel.setScrollStep(step)) {
        puts("Invalid panel position, size or scroll step."); return 2;
    }
    constexpr int rows = 40;
    panel.setContentHeight(rows * 26.0f + 24);
    if (checkInput) {
        if (!panel.attachMouseWheel()) { printf("Mouse wheel initialization failed. Win32=%lu\n", GetLastError()); return 1; }
        DwmScrollPanel::pollEvents(); panel.detachMouseWheel();
        puts("PASS: mouse wheel hook installation, message pump and cleanup."); return 0;
    }
    if (!draw.init()) { printf("Drawing channel init failed. Win32=%lu\n", GetLastError()); return 1; }
    if (!once && !panel.attachMouseWheel()) { printf("Mouse wheel initialization failed. Win32=%lu\n", GetLastError()); return 1; }
    SetConsoleCtrlHandler(Stop, TRUE);
    puts("Keep overlay_python.exe --listen running. Protocol v3 is required on both sides.");
    printf("Panel at (%.0f, %.0f), %.0f x %.0f. Wheel step: %.0f pixels.\n", x, y, width, height, step);
    puts("Move the pointer over the panel and use the mouse wheel. ESC or Ctrl+C clears and exits.");
    unsigned submitted = 0;
    do {
        if (!DwmScrollPanel::pollEvents()) break;
        // Do not consume wheel input while the receiver is hidden or absent.
        HWND receiver = FindWindowW(L"DwmDraw.CaptureExcludedOverlay", nullptr);
        panel.setInputEnabled(receiver && IsWindowVisible(receiver));
        if (draw.begin()) {
            draw.text(x, y - 28, u8"鼠标移到面板内，滚轮上下查看内容", {255, 220, 100, 255});
            if (panel.begin(draw)) {
                for (int row = 0; row < rows; ++row) {
                    char text[160]{};
                    sprintf_s(text, u8"第 %02d 行：这是可以上下滚动的绘制内容", row + 1);
                    draw.text(12, 12 + row * 26.0f, text,
                        row % 2 ? DwmColor{170, 210, 255, 255} : DwmColor{255, 255, 255, 255});
                }
                panel.end(draw);
            }
            draw.end(); ++submitted;
        }
        if (once) break;
        Sleep(16);
    } while (running && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000));
    panel.detachMouseWheel();
    if (!once) for (int attempt = 0; attempt < 20 && !draw.clear(); ++attempt) Sleep(10);
    printf("Submitted %u frames.\n", submitted);
    return submitted ? 0 : 3;
}
