"""Windows x64 drawing and wheel-scroll panels. Uses only the Python standard library."""
from __future__ import annotations

import ctypes as C
from contextlib import contextmanager
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time
import uuid

WHITE = (255, 255, 255, 255)
# Shared-memory protocol version; must match DRAW_VERSION in src/DrawProtocol.h.
_PROTOCOL_VERSION = 4
# DD_ApiVersion() exported by dwm_draw.dll.
_API_VERSION = 3
_ROOT = Path(__file__).resolve().parent.parent
_ID, _F, _I, _U = C.c_ulonglong, C.c_float, C.c_int, C.c_uint32

# DwmGlyphRanges
_RANGES = {
    "auto": 0, "default": 1, "latin": 1, "chinese": 2, "zh": 2,
    "japanese": 3, "ja": 3, "korean": 4, "ko": 4, "cyrillic": 5, "ru": 5,
}
# MOD_* values used by RegisterHotKey.
_MODIFIERS = {"alt": 0x0001, "ctrl": 0x0002, "control": 0x0002, "shift": 0x0004,
              "win": 0x0008, "super": 0x0008, "meta": 0x0008}
# Named virtual-key codes that are not simple letters, digits or function keys.
# Modifier names are accepted here so key_down("ctrl") reports their level state; in
# _parse_hotkey they are consumed as modifiers before reaching _virtual_key.
_VK_NAMES = {
    "backspace": 0x08, "tab": 0x09, "enter": 0x0D, "return": 0x0D, "esc": 0x1B, "escape": 0x1B,
    "space": 0x20, "pageup": 0x21, "pgup": 0x21, "pagedown": 0x22, "pgdn": 0x22,
    "end": 0x23, "home": 0x24, "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28,
    "insert": 0x2D, "ins": 0x2D, "delete": 0x2E, "del": 0x2E,
    "shift": 0x10, "ctrl": 0x11, "control": 0x11, "alt": 0x12, "menu": 0x12,
    "win": 0x5B, "super": 0x5B, "meta": 0x5B, "lwin": 0x5B, "rwin": 0x5C,
    "num0": 0x60, "num1": 0x61, "num2": 0x62, "num3": 0x63, "num4": 0x64,
    "num5": 0x65, "num6": 0x66, "num7": 0x67, "num8": 0x68, "num9": 0x69,
    "multiply": 0x6A, "add": 0x6B, "subtract": 0x6D, "decimal": 0x6E, "divide": 0x6F,
    "numlock": 0x90, "scrolllock": 0x91,
    "pause": 0x13, "capslock": 0x14,
    "semicolon": 0xBA, "equals": 0xBB, "comma": 0xBC, "minus": 0xBD,
    "period": 0xBE, "slash": 0xBF, "backtick": 0xC0, "grave": 0xC0,
    "lbracket": 0xDB, "backslash": 0xDC, "rbracket": 0xDD, "quote": 0xDE,
}


def _number(value):
    value = float(value)
    if not math.isfinite(value) or abs(value) > 1_000_000:
        raise ValueError("坐标和尺寸必须是 -1000000 到 1000000 之间的有限数值")
    return value


def _color(value):
    channels = tuple(value)
    if len(channels) == 3:
        channels += (255,)
    if len(channels) != 4 or any(not isinstance(c, int) or not 0 <= c <= 255 for c in channels):
        raise ValueError("颜色使用 (R, G, B) 或 (R, G, B, A)，每项为 0~255 的整数")
    r, g, b, a = channels
    return r << 24 | g << 16 | b << 8 | a


def _positive(value, what="尺寸"):
    value = _number(value)
    if value <= 0:
        raise ValueError(f"{what}必须大于零")
    return value


def _virtual_key(key):
    """Accepts an int VK code or a name such as "q", "F5", "ctrl" is not valid here."""
    if isinstance(key, int):
        if not 0 < key < 0x100:
            raise ValueError("虚拟键码必须在 0~255 范围")
        return key
    if not isinstance(key, str):
        raise ValueError("按键必须是虚拟键码整数或名称字符串")
    name = key.strip().lower()
    if name in _VK_NAMES:
        return _VK_NAMES[name]
    if len(name) == 1 and (name.isalpha() or name.isdigit()):
        return ord(name.upper())
    if name.startswith("f") and name[1:].isdigit() and 1 <= int(name[1:]) <= 24:
        return 0x70 + int(name[1:]) - 1
    raise ValueError(f"无法识别的按键名称：{key!r}")


def _parse_hotkey(spec):
    """Parses "ctrl+alt+q" into (virtual_key, modifier_flags, display_name)."""
    if not isinstance(spec, str) or not spec.strip():
        raise ValueError("快捷键必须是非空字符串，例如 'ctrl+alt+q'")
    parts = [p.strip().lower() for p in spec.split("+") if p.strip()]
    if not parts:
        raise ValueError("快捷键必须包含一个按键，例如 'ctrl+alt+q'")
    modifiers, key = 0, None
    for part in parts:
        if part in _MODIFIERS:
            modifiers |= _MODIFIERS[part]
        elif key is None:
            key = _virtual_key(part)
        else:
            raise ValueError(f"快捷键只能包含一个主按键：{spec!r}")
    if key is None:
        raise ValueError(f"快捷键缺少主按键：{spec!r}")
    if not modifiers:
        raise ValueError("全局快捷键至少需要一个修饰键（ctrl/alt/shift/win）")
    # Keep the user's original ordering and capitalisation for display.
    display = "+".join(p.strip() for p in spec.split("+") if p.strip())
    return key, modifiers, display


class _Native:
    def __init__(self, path):
        if os.name != "nt" or struct.calcsize("P") != 8:
            raise RuntimeError("需要 Windows 和 64 位 Python")
        self.dll = C.CDLL(str(path), use_last_error=True)
        self.dll.DD_ApiVersion.restype = _U
        self.dll.DD_ApiVersion.argtypes = []
        if self.dll.DD_ApiVersion() != _API_VERSION:
            raise RuntimeError("dwm_draw.dll 接口版本不匹配，请重新构建")
        definitions = {
            "ApiVersion": (_U, []), "Create": (_ID, [C.c_wchar_p, C.c_wchar_p]),
            "Destroy": (_I, [_ID]), "Begin": (_I, [_ID]), "End": (_I, [_ID, _I]),
            "Overflow": (_I, [_ID]),
            "Clear": (_I, [_ID]), "Position": (_I, [_ID, _F, _F]),
            "Text": (_I, [_ID, _F, _F, C.c_char_p, _U, _I]),
            "TextEx": (_I, [_ID, _F, _F, C.c_char_p, _U, _I, _I, _F]),
            "FontAdd": (_I, [_ID, C.c_char_p, _F, _I]),
            "Rect": (_I, [_ID, _F, _F, _F, _F, _U, _F, _I]),
            "Line": (_I, [_ID, _F, _F, _F, _F, _U, _F]),
            "Circle": (_I, [_ID, _F, _F, _F, _U, _F, _I, _I]),
            "RoundRect": (_I, [_ID, _F, _F, _F, _F, _F, _U, _F, _I]),
            "Ellipse": (_I, [_ID, _F, _F, _F, _F, _U, _F, _I, _I]),
            "Polygon": (_I, [_ID, C.POINTER(_F), _I, _U, _F, _I, _I]),
            "Bezier": (_I, [_ID, _F, _F, _F, _F, _F, _F, _F, _F, _U, _F, _I]),
            "Arc": (_I, [_ID, _F, _F, _F, _F, _F, _U, _F, _I]),
            "Clip": (_I, [_ID, _F, _F, _F, _F, _I]), "Fps": (_I, [_ID, C.POINTER(_F)]),
            "Poll": (_I, [_ID, _I]), "Receiver": (_I, [_ID, C.c_void_p]),
            "HotkeyAdd": (_I, [_ID, _I, _I, _I]),
            "HotkeyRemove": (_I, [_ID, _I]), "HotkeyFired": (_I, [_ID, _I]),
            "KeyDown": (_I, [_ID, _I]),
            "PanelCreate": (_ID, [_ID]),
            "PanelDestroy": (_I, [_ID, _ID]), "PanelSet": (_I, [_ID, _ID, _I, _F, _F]),
            "PanelColor": (_I, [_ID, _ID, _U]),
            "PanelOffset": (_I, [_ID, _ID, C.POINTER(_F), C.POINTER(_F)]),
            "PanelBegin": (_I, [_ID, _ID]), "PanelEnd": (_I, [_ID, _ID]),
        }
        for name, (result, arguments) in definitions.items():
            function = getattr(self.dll, "DD_" + name)
            function.restype, function.argtypes = result, arguments
            setattr(self, name, function)

    @staticmethod
    def check(result):
        if not result:
            raise C.WinError(C.get_last_error() or 31)
        return result


class Hotkey:
    """A registered global shortcut. Poll `fired` after each `running()` call."""

    def __init__(self, draw, handle, display, identity):
        self._draw = draw
        self._handle = handle
        self.display = display
        # (virtual key, modifier flags): the canonical identity, used to reject duplicates
        # regardless of the spelling the caller used.
        self._identity = identity

    @property
    def fired(self):
        """True once per press since the previous check; reading it consumes the event."""
        if not self._handle:
            raise RuntimeError("快捷键已注销")
        result = self._draw._native.HotkeyFired(self._draw._handle, self._handle)
        if result < 0:
            raise C.WinError(C.get_last_error() or 31)
        return bool(result)

    def close(self):
        if self._handle:
            self._draw._call("HotkeyRemove", self._handle)
            self._handle = 0
        # Deregister from the owner so the list never accumulates dead shortcuts.
        try:
            self._draw._hotkeys.remove(self)
        except ValueError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __repr__(self):
        return f"<Hotkey {self.display}>"


class DwmDraw:
    """Use on its creation thread. A context manager clears and releases everything on exit."""
    def __init__(self, *, auto_start=True, dll_path=None, renderer_path=None,
                 mapping_name=None, mutex_name=None):
        self._thread = threading.get_ident()
        self._handle = 0
        self._in_frame = False
        self._panels = []
        self._hotkeys = []
        self._fonts = {}
        self._process = None
        self._window = None
        self.renderer_log = None
        if auto_start and (mapping_name is not None or mutex_name is not None):
            raise ValueError("自定义共享内存名称时使用 auto_start=False")
        # Every automatic renderer owns its channel. Existing windows and C++
        # producers cannot block startup or overwrite this object's frames.
        self._session = uuid.uuid4().hex if auto_start else None
        if self._session:
            mapping_name = "Local\\DwmDraw.Python." + self._session
            mutex_name = mapping_name + ".Lock"
        self._native = _Native(Path(dll_path) if dll_path else _ROOT / "bin" / "dwm_draw.dll")
        self._user32 = C.WinDLL("user32", use_last_error=True)
        for name, result, arguments in (
            ("FindWindowExW", C.c_void_p, [C.c_void_p, C.c_void_p, C.c_wchar_p, C.c_wchar_p]),
            ("GetPropW", C.c_void_p, [C.c_void_p, C.c_wchar_p]),
            ("IsWindow", _I, [C.c_void_p]), ("IsWindowVisible", _I, [C.c_void_p]),
            ("ShowWindow", _I, [C.c_void_p, _I]),
            ("PostMessageW", _I, [C.c_void_p, _U, C.c_size_t, C.c_ssize_t]),
            ("GetWindowThreadProcessId", _U, [C.c_void_p, C.POINTER(_U)]),
            ("SetProcessDpiAwarenessContext", _I, [C.c_void_p]),
        ):
            function = getattr(self._user32, name)
            function.restype, function.argtypes = result, arguments
        # An existing GUI framework may have set DPI awareness already.
        self._user32.SetProcessDpiAwarenessContext(C.c_void_p(-4))
        self._handle = self._native.check(self._native.Create(mapping_name, mutex_name))
        try:
            if auto_start:
                self._start_renderer(Path(renderer_path) if renderer_path else _ROOT / "bin" / "overlay_python.exe")
        except BaseException:
            self._native.Destroy(self._handle)
            self._handle = 0
            self._stop_renderer()
            raise

    def _open(self):
        if threading.get_ident() != self._thread:
            raise RuntimeError("绘图对象、滚轮和消息循环需要在创建对象的同一线程操作")
        if not self._handle:
            raise RuntimeError("绘图对象已经关闭")

    def _call(self, name, *arguments):
        self._open()
        return self._native.check(getattr(self._native, name)(self._handle, *arguments))

    def _renderer_windows(self):
        window = None
        visited = set()
        while True:
            window = self._user32.FindWindowExW(None, window, "DwmDraw.CaptureExcludedOverlay", None)
            if not window or window in visited:
                return
            visited.add(window)
            pid = _U()
            self._user32.GetWindowThreadProcessId(window, C.byref(pid))
            yield (window, pid.value, self._user32.GetPropW(window, "DwmDraw.ProtocolVersion"),
                   bool(self._user32.GetPropW(window, "DwmDraw.Listen")))

    def _find_renderer(self):
        if self._process is None:
            return None
        for window, pid, version, listener in self._renderer_windows():
            if version == _PROTOCOL_VERSION and listener and pid == self._process.pid:
                return window
        return None

    def _start_renderer(self, executable):
        if not executable.is_file():
            raise FileNotFoundError(f"找不到显示端：{executable}，请先运行 build.ps1")
        log = tempfile.NamedTemporaryFile(prefix="dwm-python-renderer-", suffix=".log", delete=False)
        self.renderer_log = Path(log.name)
        try:
            self._process = subprocess.Popen(
                [str(executable), "--listen", "--managed", "--parent", str(os.getpid()),
                 "--session", self._session], cwd=str(executable.parent), stdout=log,
                stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        finally:
            log.close()
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if self._process.poll() is not None:
                details = self.renderer_log.read_text(encoding="utf-8", errors="replace")
                raise RuntimeError(f"显示端启动失败：\n{details}\n日志：{self.renderer_log}")
            window = self._find_renderer()
            if window and self._user32.IsWindowVisible(window):
                pid = _U()
                self._user32.GetWindowThreadProcessId(window, C.byref(pid))
                if pid.value == self._process.pid:
                    self._window = window
                    self._call("Receiver", window)
                    return
            time.sleep(0.05)
        raise TimeoutError(f"等待显示端超时，日志：{self.renderer_log}")

    def _stop_renderer(self):
        process, self._process = self._process, None
        if process is None:
            return
        if process.poll() is None:
            if self._window:
                self._user32.PostMessageW(self._window, 0x0312, 1, 0)  # Existing quit command.
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=2)

    def begin(self):
        self._open()
        result = self._native.Begin(self._handle)
        if result < 0:
            raise C.WinError(C.get_last_error() or 31)
        self._in_frame = bool(result)
        return self._in_frame

    def end(self, *, cancel=False):
        self._call("End", int(cancel))
        self._in_frame = False

    @contextmanager
    def frame(self):
        ready = self.begin()
        try:
            yield ready
        except BaseException:
            if ready:
                self.end(cancel=True)
            raise
        else:
            if ready:
                self.end()

    @property
    def overflowed(self):
        """True when the last submitted frame dropped commands or points."""
        self._open()
        result = self._native.Overflow(self._handle)
        if result < 0:
            raise C.WinError(C.get_last_error() or 31)
        return bool(result)

    def set_position(self, x, y):
        self._call("Position", _number(x), _number(y))

    def font(self, family=None, size=None, ranges="auto"):
        """Returns a font handle for `text(..., font=...)`.

        `family` is a Windows family name ("Microsoft YaHei", "Consolas"), a full .ttf/.ttc
        path, or None for the renderer default. `size` is the pixel height; None keeps the
        renderer default (16px). `ranges` is "auto", "default", "chinese", "japanese",
        "korean" or "cyrillic"; "auto" picks Chinese coverage for CJK-capable families.
        """
        self._open()
        if ranges not in _RANGES:
            raise ValueError(f"ranges 必须是 {', '.join(sorted(set(_RANGES)))} 之一")
        if size is not None:
            size = _positive(size, "字号")
        key = (family, size, ranges)
        cached = self._fonts.get(key)
        if cached is not None:
            return cached
        raw = family.encode("utf-8") if family else None
        index = self._native.FontAdd(self._handle, raw, _F(size or 0.0), _RANGES[ranges])
        if index < 0:
            raise C.WinError(C.get_last_error() or 31)
        self._fonts[key] = index
        return index

    def text(self, x, y, text, color=WHITE, *, outlined=True, font=None, size=None, ranges="auto"):
        """Draws UTF-8 text.

        `font` may be a family name, a .ttf path, or a handle from `font()`. `size` overrides
        the pixel height for this call only.
        """
        if not isinstance(text, str) or "\0" in text:
            raise ValueError("文字必须是 Python str，且不包含空字符")
        if font is None and size is None:
            self._call("Text", _number(x), _number(y), text.encode("utf-8"), _color(color), int(outlined))
            return
        if isinstance(font, int):
            index = font
            if size is not None:
                size = _positive(size, "字号")
        else:
            index = self.font(font, size, ranges)
        self._call("TextEx", _number(x), _number(y), text.encode("utf-8"), _color(color),
                   int(outlined), index, _F(size or 0.0))

    def rect(self, x, y, width, height, color=WHITE, *, thickness=2, filled=False, corner=False):
        if width < 0 or height < 0 or thickness <= 0:
            raise ValueError("尺寸不能为负，线宽必须大于零")
        if filled and corner:
            raise ValueError("filled 和 corner 不能同时启用")
        self._call("Rect", *map(_number, (x, y, width, height)), _color(color), _number(thickness), 1 if filled else 2 if corner else 0)

    def round_rect(self, x, y, width, height, radius, color=WHITE, *, thickness=2):
        """Rounded rectangle outline. Radius is clamped to half of the shorter side."""
        if width < 0 or height < 0:
            raise ValueError("尺寸不能为负")
        self._call("RoundRect", *map(_number, (x, y, width, height, radius)),
                   _color(color), _number(thickness), 0)

    def filled_round_rect(self, x, y, width, height, radius, color=WHITE):
        if width < 0 or height < 0:
            raise ValueError("尺寸不能为负")
        self._call("RoundRect", *map(_number, (x, y, width, height, radius)),
                   _color(color), _F(1.0), 1)

    def ellipse(self, x, y, radius_x, radius_y, color=WHITE, *, thickness=2, segments=64):
        if radius_x <= 0 or radius_y <= 0 or thickness <= 0:
            raise ValueError("半径和线宽必须大于零")
        if not 3 <= segments <= 512:
            raise ValueError("segments 范围为 3~512")
        self._call("Ellipse", *map(_number, (x, y, radius_x, radius_y)),
                   _color(color), _number(thickness), segments, 0)

    def filled_ellipse(self, x, y, radius_x, radius_y, color=WHITE, *, segments=64):
        if radius_x <= 0 or radius_y <= 0:
            raise ValueError("半径必须大于零")
        if not 3 <= segments <= 512:
            raise ValueError("segments 范围为 3~512")
        self._call("Ellipse", *map(_number, (x, y, radius_x, radius_y)),
                   _color(color), _F(1.0), segments, 1)

    def line(self, x1, y1, x2, y2, color=WHITE, *, thickness=2):
        if thickness <= 0:
            raise ValueError("线宽必须大于零")
        self._call("Line", *map(_number, (x1, y1, x2, y2)), _color(color), _number(thickness))

    def circle(self, x, y, radius, color=WHITE, *, thickness=2, segments=64, filled=False):
        if radius <= 0 or thickness <= 0 or not 3 <= segments <= 512:
            raise ValueError("半径和线宽必须大于零，segments 范围为 3~512")
        self._call("Circle", *map(_number, (x, y, radius)), _color(color), _number(thickness), segments, int(filled))

    def triangle(self, x1, y1, x2, y2, x3, y3, color=WHITE, *, thickness=2, filled=False):
        self.polygon([(x1, y1), (x2, y2), (x3, y3)], color, thickness=thickness, filled=filled)

    def polygon(self, points, color=WHITE, *, thickness=2, filled=False, closed=True):
        """Draws a polygon or polyline.

        `points` is any iterable of (x, y) pairs. Filled polygons handle concave outlines.
        Set closed=False for an open polyline.
        """
        pairs = [(float(px), float(py)) for px, py in points]
        if len(pairs) < 2:
            raise ValueError("多边形至少需要 2 个点")
        if len(pairs) > 4096:
            raise ValueError("多边形最多 4096 个点")
        if thickness <= 0:
            raise ValueError("线宽必须大于零")
        flat = [0.0] * (len(pairs) * 2)
        for position, (px, py) in enumerate(pairs):
            flat[position * 2] = _number(px)
            flat[position * 2 + 1] = _number(py)
        buffer = (_F * len(flat))(*flat)
        self._call("Polygon", buffer, len(pairs), _color(color), _number(thickness),
                   int(bool(filled)), int(bool(closed)))

    def polyline(self, points, color=WHITE, *, thickness=2):
        self.polygon(points, color, thickness=thickness, filled=False, closed=False)

    def bezier(self, x0, y0, x1, y1, x2, y2, x3, y3, color=WHITE, *, thickness=2, segments=32):
        """Cubic bezier from (x0,y0) to (x3,y3) with control points (x1,y1) and (x2,y2)."""
        if thickness <= 0:
            raise ValueError("线宽必须大于零")
        if not 2 <= segments <= 256:
            raise ValueError("segments 范围为 2~256")
        self._call("Bezier", *map(_number, (x0, y0, x1, y1, x2, y2, x3, y3)),
                   _color(color), _number(thickness), segments)

    def arc(self, x, y, radius, start_degrees, end_degrees, color=WHITE, *, thickness=2, segments=32):
        """Arc centred on (x, y). 0 degrees points right, angles increase downward."""
        if radius <= 0 or thickness <= 0:
            raise ValueError("半径和线宽必须大于零")
        if not 2 <= segments <= 1024:
            raise ValueError("segments 范围为 2~1024")
        self._call("Arc", _number(x), _number(y), _number(radius),
                   _number(start_degrees), _number(end_degrees),
                   _color(color), _number(thickness), segments)

    @contextmanager
    def clip(self, x, y, width, height):
        if width < 0 or height < 0:
            raise ValueError("裁剪尺寸不能为负")
        self._call("Clip", *map(_number, (x, y, width, height)), 1)
        try:
            yield
        finally:
            self._call("Clip", 0, 0, 0, 0, 0)

    @property
    def fps(self):
        value = _F()
        self._call("Fps", C.byref(value))
        return value.value

    def hotkey(self, spec, *, repeat=False):
        """Registers a global shortcut such as "ctrl+alt+q" and returns a Hotkey.

        Shortcuts are process-wide, so they fire even when the overlay is not focused.
        Read `Hotkey.fired` after `running()` to consume a press.
        """
        self._open()
        key, modifiers, display = _parse_hotkey(spec)
        identity = (key, modifiers)
        for existing in self._hotkeys:
            # Compare the parsed combination so "Ctrl+Alt+Q" and "ctrl+alt+q" collide here
            # instead of being rejected later by the OS.
            if existing._handle and existing._identity == identity:
                raise ValueError(f"快捷键已注册：{existing.display}")
        handle = self._native.HotkeyAdd(self._handle, key, modifiers, int(bool(repeat)))
        if not handle:
            raise C.WinError(C.get_last_error() or 31)
        hotkey = Hotkey(self, handle, display, identity)
        self._hotkeys.append(hotkey)
        return hotkey

    def key_down(self, key):
        """Level-triggered state of a key ("a", "F5", "space") or a raw virtual-key code."""
        self._open()
        result = self._native.KeyDown(self._handle, _virtual_key(key))
        if result < 0:
            raise C.WinError(C.get_last_error() or 31)
        return bool(result)

    def running(self, *, stop_on_escape=True):
        self._open()
        if self._process and self._process.poll() is not None:
            raise RuntimeError(f"显示端已退出，请检查日志：{self.renderer_log}")
        if self._window and not self._user32.IsWindow(self._window):
            return False
        result = self._native.Poll(self._handle, int(stop_on_escape))
        if result < 0:
            raise C.WinError(C.get_last_error() or 31)
        return bool(result)

    def clear(self):
        self._open()
        result = self._native.Clear(self._handle)
        if result < 0:
            raise C.WinError(C.get_last_error() or 31)
        return bool(result)

    def set_visible(self, visible):
        self._open()
        if not self._window or not self._user32.IsWindow(self._window):
            raise RuntimeError("没有可控制的显示端窗口")
        if bool(self._user32.IsWindowVisible(self._window)) == bool(visible):
            return
        # Keep the renderer's internal visible flag in sync.
        if not self._user32.PostMessageW(self._window, 0x0312, 2, 0):
            raise C.WinError(C.get_last_error())
        deadline = time.monotonic() + 2
        while bool(self._user32.IsWindowVisible(self._window)) != bool(visible):
            if not self._user32.IsWindow(self._window) or time.monotonic() >= deadline:
                raise RuntimeError("显示端没有响应隐藏/显示请求")
            time.sleep(0.02)

    def show(self): self.set_visible(True)
    def hide(self): self.set_visible(False)

    def scroll_panel(self, *, x=50, y=50, width=420, height=280, content_height=1000,
                     step=48, background=(20, 24, 32, 230), mouse_wheel=True):
        return ScrollPanel(self, x, y, width, height, content_height, step, background, mouse_wheel)

    def close(self):
        if not self._handle:
            return
        self._open()
        try:
            if self._in_frame:
                self.end(cancel=True)
            # Hotkey.close() removes itself from _hotkeys, so iterate over a copy.
            for hotkey in list(self._hotkeys):
                hotkey.close()
            for panel in list(reversed(self._panels)):
                panel.close()
            for _ in range(20):
                if self.clear():
                    break
                time.sleep(0.01)
            self._call("Destroy")
            self._handle = 0
        finally:
            self._stop_renderer()

    def __enter__(self):
        self._open()
        return self

    def __exit__(self, *_):
        self.close()


class ScrollPanel:
    def __init__(self, draw, x, y, width, height, content_height, step, background, mouse_wheel):
        self.draw = draw
        self._id = draw._call("PanelCreate")
        try:
            self.set_position(x, y)
            self.set_size(width, height)
            self.set_content_height(content_height)
            self.set_scroll_step(step)
            self.set_background(background)
            if mouse_wheel:
                self.attach_mouse_wheel()
        except BaseException:
            draw._call("PanelDestroy", self._id)
            self._id = 0
            raise
        draw._panels.append(self)

    def _set(self, kind, a=0, b=0):
        if not self._id:
            raise RuntimeError("滚动面板已经关闭")
        self.draw._call("PanelSet", self._id, kind, _number(a), _number(b))

    def set_position(self, x, y): self._set(0, x, y)
    def set_size(self, width, height): self._set(1, width, height)
    def set_content_height(self, height): self._set(2, height)
    def set_scroll_step(self, pixels): self._set(3, pixels)
    def scroll_to(self, offset): self._set(4, offset)
    def scroll_by(self, pixels): self._set(5, pixels)
    def set_consume_wheel(self, consume): self._set(6, int(consume))
    def set_input_enabled(self, enabled): self._set(7, int(enabled))
    def attach_mouse_wheel(self): self._set(8)
    def detach_mouse_wheel(self): self._set(9)

    def set_background(self, color):
        self.draw._call("PanelColor", self._id, _color(color))

    def _offsets(self):
        current, maximum = _F(), _F()
        self.draw._call("PanelOffset", self._id, C.byref(current), C.byref(maximum))
        return current.value, maximum.value

    @property
    def scroll_offset(self): return self._offsets()[0]

    @property
    def max_scroll_offset(self): return self._offsets()[1]

    def close(self):
        if self._id:
            self.draw._call("PanelDestroy", self._id)
            self._id = 0
            self.draw._panels.remove(self)

    def __enter__(self):
        self.draw._call("PanelBegin", self._id)
        return self

    def __exit__(self, *_):
        self.draw._call("PanelEnd", self._id)
