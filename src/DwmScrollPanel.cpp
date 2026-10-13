#include "DwmScrollPanel.h"
#include <algorithm>
#include <vector>

namespace {
HHOOK wheelHook = nullptr;
DWORD hookThread = 0;
std::vector<DwmScrollPanel*> panels;
bool Valid(float value) { return std::isfinite(value) && std::fabs(value) <= 1000000; }
}

DwmScrollPanel::~DwmScrollPanel() { detachMouseWheel(); }
float DwmScrollPanel::maxScrollOffset() const { return (std::max)(0.0f, contentHeight - height); }
bool DwmScrollPanel::setPosition(float px, float py) {
    if (!Valid(px) || !Valid(py) || active) return false;
    x = px; y = py; return true;
}
bool DwmScrollPanel::setSize(float w, float h) {
    if (!Valid(w) || !Valid(h) || w <= 0 || h <= 0 || active) return false;
    width = w; height = h; offset = (std::min)(offset, maxScrollOffset()); return true;
}
bool DwmScrollPanel::setContentHeight(float h) {
    if (!Valid(h) || h < 0 || active) return false;
    contentHeight = h; offset = (std::min)(offset, maxScrollOffset()); return true;
}
bool DwmScrollPanel::setScrollStep(float pixels) {
    if (!Valid(pixels) || pixels <= 0) return false;
    step = pixels; return true;
}
bool DwmScrollPanel::scrollTo(float value) {
    if (!Valid(value) || active) return false;
    offset = (std::max)(0.0f, (std::min)(value, maxScrollOffset())); return true;
}
bool DwmScrollPanel::scrollBy(float pixels) {
    if (!Valid(pixels)) return false;
    return scrollTo(offset + pixels);
}
bool DwmScrollPanel::handleWheel(float delta, float screenX, float screenY) {
    if (!inputEnabled || !Valid(delta) || !Valid(screenX) || !Valid(screenY) ||
        screenX < x || screenY < y || screenX >= x + width || screenY >= y + height) return false;
    return scrollBy(-delta / WHEEL_DELTA * step);
}

bool DwmScrollPanel::attachMouseWheel() {
    if (attached) return true;
    if (hookThread && hookThread != GetCurrentThreadId()) { SetLastError(ERROR_INVALID_THREAD_ID); return false; }
    if (!wheelHook) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&MouseHook), &module)) return false;
        wheelHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, module, 0);
        if (!wheelHook) return false;
        hookThread = GetCurrentThreadId();
    }
    panels.push_back(this);
    attached = true;
    return true;
}
void DwmScrollPanel::detachMouseWheel() {
    if (!attached) return;
    panels.erase(std::remove(panels.begin(), panels.end(), this), panels.end());
    attached = false;
    if (panels.empty()) {
        UnhookWindowsHookEx(wheelHook);
        wheelHook = nullptr;
        hookThread = 0;
    }
}
bool DwmScrollPanel::pollEvents() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return true;
}
LRESULT CALLBACK DwmScrollPanel::MouseHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && message == WM_MOUSEWHEEL) {
        const auto& event = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        // Most recently attached panel wins when panels overlap.
        for (auto it = panels.rbegin(); it != panels.rend(); ++it) {
            auto& panel = **it;
            if (!panel.handleWheel(static_cast<SHORT>(HIWORD(event.mouseData)),
                static_cast<float>(event.pt.x), static_cast<float>(event.pt.y))) continue;
            if (panel.consumeWheel) return 1;
            break;
        }
    }
    return CallNextHookEx(wheelHook, code, message, data);
}
bool DwmScrollPanel::begin(DwmDraw& draw) {
    if (active) return false;
    previousX = draw.positionX(); previousY = draw.positionY();
    draw.setPosition(0, 0);
    draw.pushClipRect(x, y, width, height);
    draw.filledRect(x, y, width, height, background);
    draw.setPosition(x, y - offset);
    active = &draw;
    return true;
}
void DwmScrollPanel::end(DwmDraw& draw) {
    if (active != &draw) return;
    draw.popClipRect();
    draw.setPosition(previousX, previousY);
    active = nullptr;
}
