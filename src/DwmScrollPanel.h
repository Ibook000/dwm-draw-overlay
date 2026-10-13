#pragma once
#include "DwmDraw.h"

// All calls, including pollEvents(), run on the same thread as attachMouseWheel().
class DwmScrollPanel {
public:
    DwmScrollPanel() = default;
    ~DwmScrollPanel();
    DwmScrollPanel(const DwmScrollPanel&) = delete;
    DwmScrollPanel& operator=(const DwmScrollPanel&) = delete;

    bool setPosition(float x, float y);
    bool setSize(float width, float height);
    bool setContentHeight(float height);
    bool setScrollStep(float pixelsPerNotch);
    bool scrollTo(float offset);
    bool scrollBy(float pixels);
    float scrollOffset() const { return offset; }
    float maxScrollOffset() const;
    void setBackground(DwmColor color) { background = color; }
    void setConsumeWheel(bool consume) { consumeWheel = consume; }
    void setInputEnabled(bool enabled) { inputEnabled = enabled; }
    // Also usable from an application's own WM_MOUSEWHEEL handler. Delta uses WHEEL_DELTA units.
    bool handleWheel(float delta, float screenX, float screenY);

    bool attachMouseWheel();
    void detachMouseWheel();
    static bool pollEvents();

    // Draw calls inside begin/end use content-local coordinates and are clipped to this panel.
    bool begin(DwmDraw& draw);
    void end(DwmDraw& draw);

private:
    static LRESULT CALLBACK MouseHook(int code, WPARAM message, LPARAM data);
    float x = 50, y = 50, width = 420, height = 280;
    float contentHeight = 0, step = 48, offset = 0;
    float previousX = 0, previousY = 0;
    DwmColor background{20, 24, 32, 230};
    bool attached = false, consumeWheel = true, inputEnabled = true;
    DwmDraw* active = nullptr;
};
