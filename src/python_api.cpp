#include "DwmScrollPanel.h"
#include "DwmHotkeys.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

#define API extern "C" __declspec(dllexport)
using Id = unsigned long long;
namespace {
struct ApiError { DWORD code; };
void Require(bool condition, DWORD error = ERROR_INVALID_PARAMETER) { if (!condition) throw ApiError{error}; }
DwmColor Color(unsigned rgba) {
    return {static_cast<std::uint8_t>(rgba >> 24), static_cast<std::uint8_t>(rgba >> 16),
        static_cast<std::uint8_t>(rgba >> 8), static_cast<std::uint8_t>(rgba)};
}
struct Panel { DwmScrollPanel value; bool inputEnabled = true; };
// A font descriptor registered through DD_FontAdd. The DLL owns it so the Python layer can
// pass a small stable handle instead of re-sending the family name on every text call.
struct FontDesc { std::string family; float size = 0; int ranges = DwmRanges_Auto; };
struct Session {
    DwmDraw draw;
    DWORD thread = GetCurrentThreadId();
    bool frame = false;
    bool overflow = false;
    HWND receiver = nullptr;
    DWORD receiverPid = 0;
    Id nextPanel = 1;
    // Index 0 is the renderer default font; user fonts start at 1.
    std::vector<FontDesc> fonts;
    std::unordered_map<Id, std::unique_ptr<Panel>> panels;
    std::vector<Id> active;
    // Hotkey ids registered by this session; unregistered when the session dies.
    std::vector<int> hotkeys;
    Session(const wchar_t* mapping, const wchar_t* mutex) : draw(mapping, mutex) { fonts.emplace_back(); }
    void EndPanels() {
        while (!active.empty()) { panels.at(active.back())->value.end(draw); active.pop_back(); }
    }
    void ReleaseHotkeys() {
        for (int id : hotkeys) DwmHotkeys::Unregister(id);
        hotkeys.clear();
    }
    ~Session() { ReleaseHotkeys(); }
};
struct Registry { std::mutex mutex; Id next = 1; std::unordered_map<Id, std::unique_ptr<Session>> sessions; };
// Explicit destroy performs cleanup outside DllMain; do not run User32 destructors under the loader lock.
Registry& registry() { static auto* value = new Registry; return *value; }
Session& Get(Id id) {
    const auto found = registry().sessions.find(id);
    Require(found != registry().sessions.end(), ERROR_INVALID_HANDLE);
    Require(found->second->thread == GetCurrentThreadId(), ERROR_INVALID_THREAD_ID);
    return *found->second;
}
Panel& GetPanel(Session& session, Id id) {
    const auto found = session.panels.find(id);
    Require(found != session.panels.end(), ERROR_INVALID_HANDLE); return *found->second;
}
template<class Function> int Action(Id id, Function function) noexcept {
    try {
        std::lock_guard<std::mutex> lock(registry().mutex);
        function(Get(id)); SetLastError(ERROR_SUCCESS); return 1;
    } catch (const ApiError& error) { SetLastError(error.code); }
      catch (...) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); }
    return 0;
}
template<class Function> int Draw(Id id, Function function) noexcept {
    return Action(id, [&](Session& session) { Require(session.frame, ERROR_INVALID_STATE); function(session.draw); });
}
}
// Version 3 adds text fonts/sizes, extra geometry primitives and global shortcuts.
API unsigned DD_ApiVersion() { return 3; }
API Id DD_Create(const wchar_t* mapping, const wchar_t* mutex) noexcept {
    try {
        std::lock_guard<std::mutex> lock(registry().mutex);
        auto session = std::make_unique<Session>(mapping ? mapping : MAPNAME, mutex ? mutex : MUTEXNAME);
        if (!session->draw.init()) throw ApiError{GetLastError() ? GetLastError() : ERROR_BUSY};
        const Id id = registry().next++;
        registry().sessions.emplace(id, std::move(session)); SetLastError(ERROR_SUCCESS); return id;
    } catch (const ApiError& error) { SetLastError(error.code); }
      catch (...) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); }
    return 0;
}
API int DD_Destroy(Id id) noexcept {
    return Action(id, [&](Session& session) {
        session.EndPanels();
        if (session.frame) session.draw.cancel(); else session.draw.clear();
        session.ReleaseHotkeys();
        session.panels.clear(); registry().sessions.erase(id);
    });
}
API int DD_Begin(Id id) noexcept {
    bool acquired = false;
    const int ok = Action(id, [&](Session& s) {
        Require(!s.frame, ERROR_BUSY); acquired = s.draw.begin(); s.frame = acquired;
    });
    return ok ? (acquired ? 1 : 0) : -1;
}
API int DD_End(Id id, int cancel) noexcept {
    return Action(id, [&](Session& s) {
        Require(s.frame, ERROR_INVALID_STATE); s.EndPanels();
        if (cancel) s.draw.cancel(); else s.draw.end();
        // lastFrameOverflowed() is only meaningful after end()/cancel().
        s.overflow = s.draw.overflowed();
        s.frame = false;
    });
}
API int DD_Overflow(Id id) noexcept {
    int overflow = 0;
    const int ok = Action(id, [&](Session& s) { overflow = s.overflow ? 1 : 0; });
    return ok ? overflow : -1;
}
API int DD_Clear(Id id) noexcept {
    bool cleared = false;
    const int ok = Action(id, [&](Session& s) { Require(!s.frame, ERROR_BUSY); cleared = s.draw.clear(); });
    return ok ? (cleared ? 1 : 0) : -1;
}
API int DD_Position(Id id, float x, float y) noexcept {
    return Action(id, [&](Session& s) { Require(s.draw.setPosition(x, y)); });
}
API int DD_Text(Id id, float x, float y, const char* utf8, unsigned rgba, int outlined) noexcept {
    return Draw(id, [&](DwmDraw& draw) { Require(utf8 != nullptr); draw.text(x, y, utf8, Color(rgba), outlined != 0); });
}
// fontIndex comes from DD_FontAdd; size <= 0 uses the size recorded with that font.
API int DD_TextEx(Id id, float x, float y, const char* utf8, unsigned rgba, int outlined,
    int fontIndex, float size) noexcept {
    // Uses Action (not Draw) because the font descriptor lives on the Session, not the
    // DwmDraw facade; the frame requirement is asserted explicitly instead.
    return Action(id, [&](Session& s) {
        Require(s.frame, ERROR_INVALID_STATE);
        Require(utf8 != nullptr);
        Require(fontIndex >= 0 && static_cast<size_t>(fontIndex) < s.fonts.size());
        const FontDesc& desc = s.fonts[static_cast<size_t>(fontIndex)];
        DwmFont font;
        font.family = desc.family;
        font.size = size > 0 ? size : desc.size;
        font.ranges = desc.ranges;
        s.draw.text(x, y, utf8, font, Color(rgba), outlined != 0);
    });
}
// Registers a font family (or a .ttf path) with a size and glyph range. Returns an index
// for DD_TextEx; identical requests reuse the same index.
API int DD_FontAdd(Id id, const char* family, float size, int ranges) noexcept {
    int index = 0;
    const int ok = Action(id, [&](Session& s) {
        Require(ranges >= 0 && ranges < DwmRanges_Count);
        const std::string name = family ? family : "";
        for (size_t i = 1; i < s.fonts.size(); ++i) {
            if (s.fonts[i].family == name && s.fonts[i].size == size && s.fonts[i].ranges == ranges) {
                index = static_cast<int>(i); return;
            }
        }
        s.fonts.push_back(FontDesc{ name, size, ranges });
        index = static_cast<int>(s.fonts.size()) - 1;
    });
    return ok ? index : -1;
}
API int DD_Rect(Id id, float x, float y, float w, float h, unsigned rgba, float thickness, int kind) noexcept {
    return Draw(id, [&](DwmDraw& draw) {
        Require(kind >= 0 && kind <= 2);
        if (kind == 0) draw.rect(x, y, w, h, Color(rgba), thickness);
        else if (kind == 1) draw.filledRect(x, y, w, h, Color(rgba));
        else draw.cornerRect(x, y, w, h, Color(rgba), thickness);
    });
}
API int DD_Line(Id id, float x1, float y1, float x2, float y2, unsigned rgba, float thickness) noexcept {
    return Draw(id, [&](DwmDraw& draw) { draw.line(x1, y1, x2, y2, Color(rgba), thickness); });
}
API int DD_Circle(Id id, float x, float y, float radius, unsigned rgba, float thickness, int segments, int filled) noexcept {
    return Draw(id, [&](DwmDraw& draw) {
        if (filled) draw.filledCircle(x, y, radius, Color(rgba), segments);
        else draw.circle(x, y, radius, Color(rgba), thickness, segments);
    });
}
API int DD_Clip(Id id, float x, float y, float w, float h, int push) noexcept {
    return Draw(id, [&](DwmDraw& draw) { if (push) draw.pushClipRect(x, y, w, h); else draw.popClipRect(); });
}
API int DD_Fps(Id id, float* result) noexcept {
    return Action(id, [&](Session& s) { Require(result != nullptr); *result = s.draw.fps(); });
}
API int DD_RoundRect(Id id, float x, float y, float w, float h, float radius,
    unsigned rgba, float thickness, int filled) noexcept {
    return Draw(id, [&](DwmDraw& draw) {
        if (filled) draw.filledRoundRect(x, y, w, h, radius, Color(rgba));
        else draw.roundRect(x, y, w, h, radius, Color(rgba), thickness);
    });
}
API int DD_Ellipse(Id id, float x, float y, float rx, float ry, unsigned rgba,
    float thickness, int segments, int filled) noexcept {
    return Draw(id, [&](DwmDraw& draw) {
        if (filled) draw.filledEllipse(x, y, rx, ry, Color(rgba), segments);
        else draw.ellipse(x, y, rx, ry, Color(rgba), thickness, segments);
    });
}
// xy is a flat [x0,y0,x1,y1,...] array of `count` points in content-local coordinates.
API int DD_Polygon(Id id, const float* xy, int count, unsigned rgba, float thickness,
    int filled, int closed) noexcept {
    return Draw(id, [&](DwmDraw& draw) {
        Require(xy != nullptr);
        Require(count >= 2 && count <= MAX_POINTS);
        std::vector<DwmVec2> points(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) points[static_cast<size_t>(i)] = { xy[i * 2], xy[i * 2 + 1] };
        if (filled && count >= 3) draw.polygon(points.data(), count, Color(rgba), thickness, true, closed != 0);
        else if (count >= 3 && closed) draw.polygon(points.data(), count, Color(rgba), thickness, false, true);
        else draw.polyline(points.data(), count, Color(rgba), thickness);
    });
}
API int DD_Bezier(Id id, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
    unsigned rgba, float thickness, int segments) noexcept {
    return Draw(id, [&](DwmDraw& draw) {
        draw.bezier(x0, y0, x1, y1, x2, y2, x3, y3, Color(rgba), thickness, segments);
    });
}
API int DD_Arc(Id id, float x, float y, float radius, float start, float end,
    unsigned rgba, float thickness, int segments) noexcept {
    return Draw(id, [&](DwmDraw& draw) {
        draw.arc(x, y, radius, start, end, Color(rgba), thickness, segments);
    });
}
API int DD_Receiver(Id id, HWND window) noexcept {
    return Action(id, [&](Session& s) {
        Require(IsWindow(window) != FALSE, ERROR_INVALID_WINDOW_HANDLE);
        DWORD pid = 0;
        Require(GetWindowThreadProcessId(window, &pid) != 0, ERROR_INVALID_WINDOW_HANDLE);
        s.receiver = window; s.receiverPid = pid;
    });
}
API int DD_Poll(Id id, int stopOnEscape) noexcept {
    bool running = true;
    const int ok = Action(id, [&](Session& s) {
        Require(!s.frame, ERROR_BUSY);
        HWND receiver = s.receiver ? s.receiver : FindWindowW(L"DwmDraw.CaptureExcludedOverlay", nullptr);
        DWORD pid = 0;
        if (receiver) GetWindowThreadProcessId(receiver, &pid);
        const bool visible = receiver && IsWindowVisible(receiver) && (!s.receiverPid || pid == s.receiverPid);
        for (auto& panel : s.panels) panel.second->value.setInputEnabled(visible && panel.second->inputEnabled);
        running = DwmScrollPanel::pollEvents();
        if (stopOnEscape && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) running = false;
    });
    return ok ? (running ? 1 : 0) : -1;
}
// Registers a global shortcut. vk is a virtual-key code, modifiers is MOD_ALT|MOD_CONTROL|
// MOD_SHIFT|MOD_WIN. Returns a hotkey id (>0) or 0 on failure.
API int DD_HotkeyAdd(Id id, int vk, int modifiers, int repeat) noexcept {
    int hotkey = 0;
    const int ok = Action(id, [&](Session& s) {
        hotkey = DwmHotkeys::Register(vk, modifiers, repeat != 0);
        Require(hotkey != 0, GetLastError() ? GetLastError() : ERROR_INVALID_PARAMETER);
        s.hotkeys.push_back(hotkey);
    });
    return ok ? hotkey : 0;
}
API int DD_HotkeyRemove(Id id, int hotkey) noexcept {
    return Action(id, [&](Session& s) {
        auto found = std::find(s.hotkeys.begin(), s.hotkeys.end(), hotkey);
        Require(found != s.hotkeys.end(), ERROR_INVALID_HANDLE);
        Require(DwmHotkeys::Unregister(hotkey), GetLastError());
        s.hotkeys.erase(found);
    });
}
// Consumes and reports whether the shortcut fired since the previous call.
API int DD_HotkeyFired(Id id, int hotkey) noexcept {
    int fired = 0;
    const int ok = Action(id, [&](Session&) { fired = DwmHotkeys::Consume(hotkey) ? 1 : 0; });
    return ok ? fired : -1;
}
API int DD_KeyDown(Id id, int vk) noexcept {
    int down = 0;
    const int ok = Action(id, [&](Session&) { down = DwmHotkeys::IsDown(vk) ? 1 : 0; });
    return ok ? down : -1;
}
API Id DD_PanelCreate(Id id) noexcept {
    Id panelId = 0;
    if (!Action(id, [&](Session& s) { panelId = s.nextPanel++; s.panels.emplace(panelId, std::make_unique<Panel>()); })) return 0;
    return panelId;
}
API int DD_PanelDestroy(Id id, Id panelId) noexcept {
    return Action(id, [&](Session& s) {
        GetPanel(s, panelId);
        for (Id active : s.active) Require(active != panelId, ERROR_BUSY);
        s.panels.erase(panelId);
    });
}
// kind: 0=position, 1=size, 2=content height, 3=step, 4=scrollTo, 5=scrollBy,
// 6=consume wheel, 7=input enabled, 8=attach, 9=detach.
API int DD_PanelSet(Id id, Id panelId, int kind, float a, float b) noexcept {
    return Action(id, [&](Session& s) {
        auto& p = GetPanel(s, panelId); bool ok = true;
        switch (kind) {
        case 0: ok = p.value.setPosition(a, b); break;
        case 1: ok = p.value.setSize(a, b); break;
        case 2: ok = p.value.setContentHeight(a); break;
        case 3: ok = p.value.setScrollStep(a); break;
        case 4: ok = p.value.scrollTo(a); break;
        case 5: ok = p.value.scrollBy(a); break;
        case 6: p.value.setConsumeWheel(a != 0); break;
        case 7: p.inputEnabled = a != 0; p.value.setInputEnabled(p.inputEnabled); break;
        case 8:
            if (!p.value.attachMouseWheel()) throw ApiError{GetLastError()};
            break;
        case 9: p.value.detachMouseWheel(); break;
        default: ok = false;
        }
        Require(ok);
    });
}
API int DD_PanelColor(Id id, Id panelId, unsigned rgba) noexcept {
    return Action(id, [&](Session& s) { GetPanel(s, panelId).value.setBackground(Color(rgba)); });
}
API int DD_PanelOffset(Id id, Id panelId, float* current, float* maximum) noexcept {
    return Action(id, [&](Session& s) {
        Require(current && maximum); auto& p = GetPanel(s, panelId);
        *current = p.value.scrollOffset(); *maximum = p.value.maxScrollOffset();
    });
}
API int DD_PanelBegin(Id id, Id panelId) noexcept {
    return Action(id, [&](Session& s) {
        Require(s.frame, ERROR_INVALID_STATE);
        Require(GetPanel(s, panelId).value.begin(s.draw), ERROR_BUSY); s.active.push_back(panelId);
    });
}
API int DD_PanelEnd(Id id, Id panelId) noexcept {
    return Action(id, [&](Session& s) {
        Require(!s.active.empty() && s.active.back() == panelId, ERROR_INVALID_STATE);
        GetPanel(s, panelId).value.end(s.draw); s.active.pop_back();
    });
}
