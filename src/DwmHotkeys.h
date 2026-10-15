#pragma once
#include <Windows.h>

// Global shortcut support for the drawing host process.
//
// Where this runs: dwm_draw.dll is loaded into the *producer* process (Python), and that
// process pumps its own thread message queue. RegisterHotKey(NULL, ...) posts WM_HOTKEY to
// that queue, so shortcuts work without the renderer window being focused. The renderer
// executable deliberately registers no hotkeys in --managed mode, so there is no clash.
//
// Threading: every function must be called from the thread that created the drawing
// session, because RegisterHotKey/UnregisterHotKey are thread-affine.
namespace DwmHotkeys {
// vk is a virtual-key code; modifiers is the OR of MOD_ALT/CONTROL/SHIFT/WIN and
// MOD_NOREPEAT. Pass repeat=true to also see auto-repeat while the key is held.
// Returns a non-zero shortcut id, or 0 on failure (GetLastError explains why).
int Register(int vk, int modifiers, bool repeat);
bool Unregister(int id);
// Level-triggered state of any virtual key or mouse button (VK_LBUTTON, 'A', VK_F1...).
bool IsDown(int vk);
// True when the shortcut fired at least once since the previous call; consumes the event.
bool Consume(int id);
// Pumps this thread's messages, recording WM_HOTKEY. Returns false on WM_QUIT.
// DwmScrollPanel::pollEvents() delegates here so a running message loop never swallows
// shortcut events before Consume() sees them.
bool PumpMessages();
}
