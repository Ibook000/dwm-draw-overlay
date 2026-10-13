#include "DwmDraw.h"
#include <cstdio>
#include <cstring>

namespace {
volatile LONG running = 1;
BOOL WINAPI Stop(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&running, 0);
        return TRUE;
    }
    return FALSE;
}
}

int main(int argc, char** argv) {
    const bool once = argc == 2 && !strcmp(argv[1], "--once");
    if (argc > 1 && !once) {
        puts("Usage: draw_example [--once]");
        return 2;
    }
    DwmDraw draw;
    if (!draw.init()) {
        printf("Drawing channel initialization failed. Win32=%lu\n", GetLastError());
        return 1;
    }
    SetConsoleCtrlHandler(Stop, TRUE);
    puts("Keep overlay_python.exe --listen running to display these commands (protocol v3).");
    puts("ESC or Ctrl+C: clear and exit. Edit examples/draw_functions.cpp to change the drawing.");

    unsigned submitted = 0;
    do {
        if (draw.begin()) {
            draw.text(50, 50, u8"你好，这是我绘制的文字", {255, 255, 255, 255});
            draw.text(50, 80, u8"红字、绿框、黄线、蓝圆", {255, 80, 80, 255});
            draw.rect(50, 120, 240, 140, {0, 255, 100, 255}, 2);
            draw.line(50, 290, 350, 290, {255, 220, 0, 255}, 3);
            draw.circle(400, 190, 50, {80, 160, 255, 255}, 2);
            draw.filledRect(50, 330, 200, 50, {100, 80, 220, 160});
            draw.end();
            ++submitted;
        }
        if (once) break;
        Sleep(16);
    } while (running && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000));

    // --once preserves the submitted frame for inspection. Interactive exit clears it.
    if (!once) {
        for (int attempt = 0; attempt < 20 && !draw.clear(); ++attempt) Sleep(10);
    }
    printf("Submitted %u frames.\n", submitted);
    return submitted ? 0 : 3;
}
