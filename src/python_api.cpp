#include "DwmScrollPanel.h"
#include <memory>
#include <mutex>
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
struct Session {
    DwmDraw draw;
    DWORD thread = GetCurrentThreadId();
    bool frame = false;
    HWND receiver = nullptr;
    DWORD receiverPid = 0;
    Id nextPanel = 1;
    std::unordered_map<Id, std::unique_ptr<Panel>> panels;
    std::vector<Id> active;
    Session(const wchar_t* mapping, const wchar_t* mutex) : draw(mapping, mutex) {}
    void EndPanels() {
        while (!active.empty()) { panels.at(active.back())->value.end(draw); active.pop_back(); }
    }
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
API unsigned DD_ApiVersion() { return 2; }
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
        if (cancel) s.draw.cancel(); else s.draw.end(); s.frame = false;
    });
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
