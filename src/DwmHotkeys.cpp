#include "DwmHotkeys.h"
#include <set>
#include <vector>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace {
// RegisterHotKey ids must be stable per thread; ids are only unique within a thread and
// a session is thread-affine, so a simple counter is enough.
int g_nextId = 1;
// Hotkey ids that fired since the last Consume(). WM_HOTKEY is delivered to this thread's
// queue, and both pump functions below run on that same thread, so no lock is required.
std::set<int> g_pending;
std::set<int> g_registered;

// Rejects vk/modifier combinations that RegisterHotKey cannot accept, so callers get a
// clear failure instead of an unpredictable ERROR_INVALID_PARAMETER.
bool ValidVirtualKey(int vk) {
    if (vk <= 0 || vk > 0xFF) return false;
    // Modifier keys themselves cannot be the trigger key.
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) return false;
    return true;
}
}

namespace DwmHotkeys {

int Register(int vk, int modifiers, bool repeat) {
    if (!ValidVirtualKey(vk)) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    // Strip anything outside the documented modifier set.
    const int allowed = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN | MOD_NOREPEAT;
    if (modifiers & ~allowed) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }

    // Naked keys cannot be registered as global hotkeys (the OS reserves them).
    const int bare = modifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
    if (!bare) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }

    const int id = g_nextId++;
    const UINT flags = static_cast<UINT>(modifiers & ~MOD_NOREPEAT) |
        static_cast<UINT>(repeat ? 0 : MOD_NOREPEAT);
    if (!::RegisterHotKey(nullptr, id, flags, static_cast<UINT>(vk))) {
        return 0;
    }
    g_registered.insert(id);
    return id;
}

bool Unregister(int id) {
    if (id <= 0) return false;
    const auto found = g_registered.find(id);
    if (found == g_registered.end()) return false;
    const bool ok = ::UnregisterHotKey(nullptr, id) != FALSE;
    g_registered.erase(found);
    g_pending.erase(id);
    return ok;
}

bool IsDown(int vk) {
    if (vk <= 0 || vk > 0xFF) return false;
    return (::GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool Consume(int id) {
    const auto found = g_pending.find(id);
    if (found == g_pending.end()) return false;
    g_pending.erase(found);
    return true;
}

// Records a WM_HOTKEY for later Consume() calls. Shared with DwmScrollPanel::pollEvents so
// whichever pump drains the queue first still observes the shortcut.
void Record(int id) { if (id > 0) g_pending.insert(id); }

bool PumpMessages() {
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false;
        if (message.message == WM_HOTKEY) Record(static_cast<int>(message.wParam));
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return true;
}
}
