"""End-to-end Python/renderer test on a private desktop; never switches the user's desktop."""
import ctypes as C
from pathlib import Path
import subprocess
import sys
import time
import traceback
import os
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from dwm_draw import _PROTOCOL_VERSION


def child(result, orphan=False, legacy=False):
    from dwm_draw import DwmDraw
    try:
        if legacy:
            with DwmDraw() as old:
                old_process = old._process
                old._user32.RemovePropW.argtypes = [C.c_void_p, C.c_wchar_p]
                old._user32.RemovePropW.restype = C.c_void_p
                removed = old._user32.RemovePropW(old._window, "DwmDraw.ProtocolVersion")
                if removed != _PROTOCOL_VERSION:
                    raise AssertionError(f"Could not simulate the old renderer properties: {removed}")
                start_renderer = DwmDraw._start_renderer
                attempted = []
                def denied_legacy_message(draw, executable):
                    post = draw._user32.PostMessageW
                    def guarded(window, message, wparam, lparam):
                        if window == old._window:
                            attempted.append((message, wparam))
                            C.set_last_error(5)
                            return 0
                        return post(window, message, wparam, lparam)
                    draw._user32.PostMessageW = guarded
                    return start_renderer(draw, executable)
                with patch.object(DwmDraw, "_start_renderer", denied_legacy_message):
                    code = child(result)
                if code:
                    return code
                if old_process.poll() is not None or attempted:
                    raise AssertionError("Python attempted to control the unrelated legacy renderer")
            with result.open("a", encoding="utf-8") as log:
                log.write("PASS: rendering succeeds alongside a legacy window whose control messages would be denied; no messages sent to that window.\n")
            return 0
        with DwmDraw() as draw:
            process = draw._process
            if process is None:
                raise AssertionError("Expected this test to own its renderer")
            panel = draw.scroll_panel(x=20, y=20, width=300, height=200, content_height=600)
            draw.hide()
            if draw._user32.IsWindowVisible(draw._window):
                raise AssertionError("Python hide failed")
            with DwmDraw() as other:
                if other._process is None or other._window == draw._window or other._session == draw._session:
                    raise AssertionError("Automatic renderers did not get independent windows/channels")
                if draw._user32.IsWindowVisible(draw._window):
                    raise AssertionError("Starting another renderer unexpectedly showed the first window")
                with other.frame() as ready:
                    if ready:
                        other.text(400, 20, "独立显示端")
            if process.poll() is not None:
                raise AssertionError("Closing another drawing object stopped this renderer")
            draw.show()
            if not draw._user32.IsWindowVisible(draw._window):
                raise AssertionError("Python show failed")
            panel.scroll_to(40)
            # Exercise fonts, sizes and the full geometry set through the real renderer.
            hotkey = draw.hotkey("ctrl+alt+f10")
            small = draw.font("Consolas", 11)
            large = draw.font("SimHei", 30)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if not draw.running(stop_on_escape=False):
                    raise AssertionError("Renderer stopped early")
                with draw.frame() as ready:
                    if ready:
                        with panel:
                            draw.text(12, 60, "Python 自动启动和滚动面板")
                            draw.rect(12, 100, 100, 40, (255, 0, 0), filled=True)
                        draw.text(300, 30, "小号等宽", (200, 220, 255), font=small)
                        draw.text(300, 60, "大号黑体", (255, 220, 100), font=large)
                        draw.text(300, 100, "按字号覆盖", (150, 255, 150), font="SimHei", size=18)
                        draw.filled_round_rect(300, 130, 120, 40, 10, (40, 90, 160, 200))
                        draw.round_rect(300, 180, 120, 40, 10, (255, 255, 255), thickness=2)
                        draw.filled_ellipse(360, 250, 55, 30, (90, 200, 120, 180))
                        draw.ellipse(360, 250, 55, 30, (255, 255, 255), thickness=2)
                        # A concave star exercises the ear-clipping fill path.
                        draw.polygon([(500, 300), (515, 340), (560, 340), (525, 365),
                                      (540, 405), (500, 380), (460, 405), (475, 365),
                                      (440, 340), (485, 340)], (255, 200, 60), filled=True)
                        draw.bezier(500, 420, 540, 380, 560, 470, 600, 430,
                                    (120, 200, 255), thickness=3, segments=24)
                        draw.arc(680, 250, 50, 0, 270, (255, 120, 120), thickness=3, segments=32)
                        draw.triangle(650, 320, 700, 320, 675, 370, (200, 120, 255), filled=True)
                if draw.fps > 0:
                    break
                time.sleep(0.03)
            if draw.fps <= 0:
                raise AssertionError("Renderer did not consume Python commands")
            # A registered but unpressed shortcut must report false, not raise.
            if hotkey.fired:
                raise AssertionError("Hotkey reported fired without a key press")
            if draw.key_down("f10") and not draw.key_down("ctrl"):
                raise AssertionError("Unexpected f10 state")
            hotkey.close()
            fps = draw.fps
            if orphan:
                result.write_text(str(process.pid), encoding="utf-8")
                os._exit(0)  # Deliberately bypass Python context-manager cleanup.
        if process.poll() != 0:
            raise AssertionError(f"Owned renderer did not exit cleanly: {process.poll()}")
        result.write_text(f"PASS: Python auto-start, wheel setup, shared-memory rendering (fps={fps:.1f}), clear and owned-renderer shutdown.\n", encoding="utf-8")
        return 0
    except BaseException:
        result.write_text(traceback.format_exc(), encoding="utf-8")
        return 1


def parent(orphan=False, legacy=False):
    user32, kernel32 = C.WinDLL("user32", use_last_error=True), C.WinDLL("kernel32", use_last_error=True)
    user32.CreateDesktopW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32, C.c_void_p]
    user32.CreateDesktopW.restype = C.c_void_p
    user32.CloseDesktop.argtypes = [C.c_void_p]
    class StartupInfo(C.Structure):
        _fields_ = [("cb", C.c_uint32), ("reserved", C.c_wchar_p), ("desktop", C.c_wchar_p),
                    ("title", C.c_wchar_p), ("x", C.c_uint32), ("y", C.c_uint32),
                    ("xsize", C.c_uint32), ("ysize", C.c_uint32), ("xchars", C.c_uint32),
                    ("ychars", C.c_uint32), ("fill", C.c_uint32), ("flags", C.c_uint32),
                    ("show", C.c_ushort), ("reservedsize", C.c_ushort), ("reservedptr", C.c_void_p),
                    ("stdin", C.c_void_p), ("stdout", C.c_void_p), ("stderr", C.c_void_p)]
    class ProcessInfo(C.Structure):
        _fields_ = [("process", C.c_void_p), ("thread", C.c_void_p), ("pid", C.c_uint32), ("tid", C.c_uint32)]
    kernel32.CreateProcessW.argtypes = [C.c_wchar_p, C.c_wchar_p, C.c_void_p, C.c_void_p, C.c_int,
                                       C.c_uint32, C.c_void_p, C.c_wchar_p, C.POINTER(StartupInfo), C.POINTER(ProcessInfo)]
    kernel32.WaitForSingleObject.argtypes = [C.c_void_p, C.c_uint32]
    kernel32.GetExitCodeProcess.argtypes = [C.c_void_p, C.POINTER(C.c_uint32)]
    kernel32.CloseHandle.argtypes = [C.c_void_p]
    kernel32.OpenProcess.argtypes = [C.c_uint32, C.c_int, C.c_uint32]
    kernel32.OpenProcess.restype = C.c_void_p
    name = f"DwmPythonTest{time.time_ns()}"
    desktop = user32.CreateDesktopW(name, None, None, 0, 0x01FF, None)
    if not desktop:
        raise C.WinError(C.get_last_error())
    result = ROOT / ".build" / ("python-orphan-result.txt" if orphan else "python-legacy-result.txt" if legacy else "python-gui-result.txt")
    info, startup = ProcessInfo(), StartupInfo()
    startup.cb, startup.desktop, startup.flags, startup.show = C.sizeof(startup), "WinSta0\\" + name, 1, 0
    command = C.create_unicode_buffer(subprocess.list2cmdline([sys.executable, str(Path(__file__).resolve()),
        "--orphan-child" if orphan else "--legacy-child" if legacy else "--child", str(result)]))
    try:
        result.parent.mkdir(parents=True, exist_ok=True)
        if not kernel32.CreateProcessW(sys.executable, command, None, None, False, 0x08000000, None, str(ROOT), C.byref(startup), C.byref(info)):
            raise C.WinError(C.get_last_error())
        if kernel32.WaitForSingleObject(info.process, 20000) != 0:
            raise TimeoutError("Private-desktop GUI test timed out")
        exit_code = C.c_uint32()
        kernel32.GetExitCodeProcess(info.process, C.byref(exit_code))
        if orphan and exit_code.value == 0:
            renderer_pid = int(result.read_text(encoding="utf-8"))
            renderer = kernel32.OpenProcess(0x00100000 | 0x1000, False, renderer_pid)
            if renderer:
                try:
                    if kernel32.WaitForSingleObject(renderer, 5000) != 0:
                        raise AssertionError("Renderer survived its Python parent")
                    renderer_exit = C.c_uint32()
                    kernel32.GetExitCodeProcess(renderer, C.byref(renderer_exit))
                    if renderer_exit.value != 0:
                        raise AssertionError("Renderer did not shut down cleanly after parent exit")
                finally:
                    kernel32.CloseHandle(renderer)
            elif C.get_last_error() != 87:
                raise C.WinError(C.get_last_error())
            print("PASS: renderer automatically exits after abrupt Python parent termination.")
            return 0
        print(result.read_text(encoding="utf-8"))
        return exit_code.value
    finally:
        if info.thread: kernel32.CloseHandle(info.thread)
        if info.process: kernel32.CloseHandle(info.process)
        user32.CloseDesktop(desktop)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("--child", "--orphan-child", "--legacy-child"):
        raise SystemExit(child(Path(sys.argv[2]), sys.argv[1] == "--orphan-child", sys.argv[1] == "--legacy-child"))
    raise SystemExit(parent("--orphan" in sys.argv, "--legacy" in sys.argv))
