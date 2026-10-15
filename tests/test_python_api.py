import ctypes as C
from pathlib import Path
import struct
import sys
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from dwm_draw import DwmDraw, _parse_hotkey


"""Data layout of DRAWARR, mirrored from src/DrawProtocol.h (protocol v4)."""
PROTOCOL_VERSION = 4
SHARED_SIZE = 181956
COMMAND_SIZE = 296
COMMAND_COUNT = 500
FONT_SIZE = 72
FONT_COUNT = 16
POINT_COUNT = 4096
# Offsets inside DRAWARR.
OFFSET_DRAW_COUNT = 20
OFFSET_POINT_COUNT = 24
OFFSET_OVERFLOW = 28
OFFSET_FONT_COUNT = 32
OFFSET_FONTS = 36
OFFSET_COMMANDS = OFFSET_FONTS + FONT_SIZE * FONT_COUNT  # 1188
OFFSET_POINTS = OFFSET_COMMANDS + COMMAND_SIZE * COMMAND_COUNT  # 149188
# Command type ids, matching enum DRAWTYPE.
TEXT, TEXT2, CIRCLE_FILL, CIRCLE, RECT, RECT_EX, FILLED_RECT, LINE = range(8)
FPS, CLIP_PUSH, CLIP_POP, ROUND_RECT, ELLIPSE, POINTS, ARC = range(8, 15)
# Field offsets inside DrawTextStr (From starts at byte 4 of the command).
OFF_TEXT_OUTLINED = 28
OFF_TEXT_SIZE = 32
OFF_TEXT_FONT = 36
OFF_TEXT_STRING = 40
# Field offsets inside DrawPointsStr.
OFF_POINTS_FIRST = 24
OFF_POINTS_COUNT = 28
OFF_POINTS_FILLED = 32
OFF_POINTS_CLOSED = 36
# DrawRoundRectStr / DrawEllipseStr place the trailing flag last:
# 5 geometry floats, 4 colour floats, thickness, then m_Filled.
OFF_SHAPE_FILLED = 44


class Reader:
    def __init__(self, name):
        self.api = C.WinDLL("kernel32", use_last_error=True)
        self.api.OpenFileMappingW.argtypes = [C.c_uint32, C.c_int, C.c_wchar_p]
        self.api.OpenFileMappingW.restype = C.c_void_p
        self.api.MapViewOfFile.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_size_t]
        self.api.MapViewOfFile.restype = C.c_void_p
        self.api.UnmapViewOfFile.argtypes = [C.c_void_p]
        self.api.CloseHandle.argtypes = [C.c_void_p]
        self.handle = self.api.OpenFileMappingW(4, 0, name)
        self.address = self.api.MapViewOfFile(self.handle, 4, 0, 0, SHARED_SIZE)
        if not self.address:
            raise C.WinError(C.get_last_error())

    def header(self):
        data = C.string_at(self.address, OFFSET_FONTS)
        magic, version, size, number, fps, count, points, overflow, fonts = struct.unpack_from("<iIIIiiiii", data)
        assert version == PROTOCOL_VERSION and size == SHARED_SIZE
        return SimpleNamespace(magic=magic, version=version, size=size, frame=number, fps=fps,
                               count=count, points=points, overflow=overflow, fonts=fonts)

    def commands(self):
        state = self.header()
        data = C.string_at(self.address + OFFSET_COMMANDS, COMMAND_SIZE * state.count)
        return [data[i * COMMAND_SIZE:(i + 1) * COMMAND_SIZE] for i in range(state.count)]

    def fonts(self):
        """Decoded font table entries, as (family, size, ranges)."""
        state = self.header()
        table = []
        for index in range(state.fonts):
            raw = C.string_at(self.address + OFFSET_FONTS + index * FONT_SIZE, FONT_SIZE)
            family = raw[:64].split(b"\0", 1)[0].decode("utf-8")
            size, ranges = struct.unpack_from("<fi", raw, 64)
            table.append((family, size, ranges))
        return table

    def points(self):
        """Decoded vertex pool as a list of (x, y) tuples."""
        state = self.header()
        raw = C.string_at(self.address + OFFSET_POINTS, 8 * state.points)
        return [struct.unpack_from("<ff", raw, i * 8) for i in range(state.points)]

    def close(self):
        self.api.UnmapViewOfFile(self.address)
        self.api.CloseHandle(self.handle)


class PythonApiTests(unittest.TestCase):
    def setUp(self):
        name = f"Local\\DwmDraw.PythonTest.{time.time_ns()}"
        self.draw = DwmDraw(auto_start=False, mapping_name=name, mutex_name=name + ".Lock")
        self.reader = Reader(name)

    def tearDown(self):
        self.draw.close()
        self.reader.close()

    def test_unicode_rgba_and_position_across_dll(self):
        self.draw.set_position(100, 200)
        with self.draw.frame() as ready:
            self.assertTrue(ready)
            self.draw.text(12, 24, "你好 Python", (255, 128, 0, 64), outlined=False)
            self.draw.rect(0, 0, 80, 60, (0, 255, 0), thickness=3)
            self.draw.circle(40, 40, 10, filled=True)
        commands = self.reader.commands()
        self.assertEqual(len(commands), 3)
        kind, x, y, r, g, b, a = struct.unpack_from("<iff4f", commands[0])
        self.assertEqual((kind, x, y), (TEXT2, 112, 224))
        self.assertAlmostEqual(g, 128 / 255, places=6)
        self.assertAlmostEqual(a, 64 / 255, places=6)
        self.assertEqual(commands[0][OFF_TEXT_OUTLINED], 0)
        self.assertEqual(commands[0][OFF_TEXT_STRING:].split(b"\0", 1)[0].decode("utf-8"), "你好 Python")
        self.assertEqual(struct.unpack_from("<i", commands[2])[0], CIRCLE_FILL)

    def test_scroll_panel_bounds_and_clip_commands(self):
        panel = self.draw.scroll_panel(x=300, y=150, width=400, height=300,
                                      content_height=1000, mouse_wheel=False)
        panel.scroll_to(80)
        self.assertEqual(panel.scroll_offset, 80)
        self.assertEqual(panel.max_scroll_offset, 700)
        with self.draw.frame() as ready:
            self.assertTrue(ready)
            with panel:
                self.draw.text(12, 24, "面板内容")
            self.draw.line(0, 0, 1, 1)
        commands = self.reader.commands()
        self.assertEqual([struct.unpack_from("<i", c)[0] for c in commands],
                         [CLIP_PUSH, FILLED_RECT, TEXT2, CLIP_POP, LINE])
        self.assertEqual(struct.unpack_from("<ff", commands[2], 4), (312, 94))
        self.assertEqual(struct.unpack_from("<ff", commands[4], 4), (0, 0))
        panel.scroll_by(9999)
        self.assertEqual(panel.scroll_offset, 700)
        panel.set_content_height(320)
        self.assertEqual(panel.scroll_offset, 20)

    def test_frame_exception_cancels_and_unlocks(self):
        with self.assertRaisesRegex(ValueError, "test error"):
            with self.draw.frame():
                self.draw.text(0, 0, "不能留下半帧")
                raise ValueError("test error")
        self.assertEqual(self.reader.commands(), [])
        with self.draw.frame():
            self.draw.text(0, 0, "恢复")
        self.assertEqual(len(self.reader.commands()), 1)

    def test_thread_and_handle_validation(self):
        errors = []
        def wrong_thread():
            try:
                self.draw.begin()
            except RuntimeError as error:
                errors.append(str(error))
        thread = threading.Thread(target=wrong_thread)
        thread.start(); thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(errors), 1)
        self.assertEqual(self.draw._native.Destroy(1 << 48), 0)
        self.assertEqual(C.get_last_error(), 6)
        with self.assertRaises(OSError):
            self.draw.text(0, 0, "没有开始帧")

    def test_clear_and_invalid_values(self):
        with self.draw.frame():
            self.draw.rect(1, 2, 3, 4)
        self.assertTrue(self.draw.clear())
        self.assertEqual(self.reader.commands(), [])
        with self.assertRaises(ValueError):
            self.draw.set_position(float("nan"), 0)
        with self.draw.frame():
            with self.assertRaises(ValueError):
                self.draw.text(0, 0, "颜色错误", (999, 0, 0))
        self.draw.close()
        with self.assertRaises(RuntimeError):
            self.draw.begin()

    def test_renderer_search_checks_all_windows(self):
        # A legacy renderer reports the old protocol version and must never be adopted.
        self.draw._renderer_windows = lambda: iter(
            [(101, 1, None, False), (202, 2, PROTOCOL_VERSION - 1, True), (303, 2, PROTOCOL_VERSION, True)])
        self.assertIsNone(self.draw._find_renderer())
        self.draw._process = SimpleNamespace(pid=2)
        try:
            # Only the listener at the current protocol version owned by our process matches.
            self.assertEqual(self.draw._find_renderer(), 303)
        finally:
            self.draw._process = None

    def test_automatic_sessions_do_not_overwrite_or_clear_each_other(self):
        with patch.object(DwmDraw, "_start_renderer"):
            with DwmDraw() as first:
                reader = Reader("Local\\DwmDraw.Python." + first._session)
                try:
                    with first.frame():
                        first.text(10, 20, "第一份内容")
                    expected = reader.commands()
                    with DwmDraw() as second:
                        self.assertNotEqual(first._session, second._session)
                        with second.frame():
                            second.text(30, 40, "第二份内容")
                        self.assertEqual(reader.commands(), expected)
                    self.assertEqual(reader.commands(), expected)
                finally:
                    reader.close()

    def test_receiver_rejects_invalid_window(self):
        with self.assertRaises(OSError):
            self.draw._call("Receiver", None)
        self.assertEqual(C.get_last_error(), 1400)

    def test_font_family_and_size_are_negotiated(self):
        handle = self.draw.font("Consolas", 24)
        self.assertGreaterEqual(handle, 1)
        # The same request must reuse the handle rather than growing the table.
        self.assertEqual(self.draw.font("Consolas", 24), handle)
        self.assertNotEqual(self.draw.font("Consolas", 12), handle)
        with self.draw.frame():
            self.draw.text(10, 20, "小号", font="SimHei", size=12)
            self.draw.text(10, 60, "大号", font=handle, size=32, color=(255, 0, 0))
        state = self.reader.header()
        self.assertGreaterEqual(state.fonts, 3)
        table = self.reader.fonts()
        self.assertEqual(table[0], ("", 0.0, 0))  # index 0 is the renderer default
        families = {entry[0] for entry in table}
        self.assertIn("SimHei", families)
        self.assertIn("Consolas", families)
        commands = self.reader.commands()
        self.assertEqual([struct.unpack_from("<i", c)[0] for c in commands], [TEXT2, TEXT2])
        # The first call carries an explicit size and its own font index.
        first_size, first_font = struct.unpack_from("<fi", commands[0], OFF_TEXT_SIZE)
        self.assertEqual(first_size, 12)
        self.assertGreaterEqual(first_font, 1)
        self.assertEqual(table[first_font][0], "SimHei")
        second_size, second_font = struct.unpack_from("<fi", commands[1], OFF_TEXT_SIZE)
        self.assertEqual(second_size, 32)
        # Wire font indices address the frame's own table, which is numbered independently
        # of the session handles returned by font(); what matters is the resolved family.
        self.assertNotEqual(second_font, first_font)
        self.assertEqual(table[second_font][0], "Consolas")
        self.assertEqual(table[second_font][1], 32.0)

    def test_text_without_font_keeps_legacy_command(self):
        with self.draw.frame():
            self.draw.text(1, 2, "默认字体")
        command = self.reader.commands()[0]
        self.assertEqual(struct.unpack_from("<i", command)[0], TEXT2)
        size, font = struct.unpack_from("<fi", command, OFF_TEXT_SIZE)
        self.assertEqual((size, font), (0.0, 0))  # 0/0 means "renderer default"

    def test_invalid_font_and_size_arguments(self):
        with self.assertRaises(ValueError):
            self.draw.font("Arial", 0)
        with self.assertRaises(ValueError):
            self.draw.font("Arial", -5)
        with self.assertRaises(ValueError):
            self.draw.font("Arial", 12, ranges="klingon")
        with self.assertRaises(ValueError):
            with self.draw.frame():
                self.draw.text(0, 0, "bad", font="Arial", size=float("nan"))

    def test_new_geometry_primitives_reach_the_protocol(self):
        with self.draw.frame():
            self.draw.round_rect(1, 2, 100, 40, 8, (10, 20, 30), thickness=2)
            self.draw.filled_round_rect(1, 60, 100, 40, 8, (10, 20, 30))
            self.draw.ellipse(50, 150, 30, 15, (1, 2, 3), thickness=2)
            self.draw.filled_ellipse(50, 200, 30, 15, (1, 2, 3))
            self.draw.triangle(0, 0, 10, 0, 5, 10, filled=True)
            self.draw.polygon([(0, 0), (10, 0), (10, 10), (0, 10)], (9, 9, 9))
            self.draw.polyline([(0, 0), (5, 5), (10, 0)])
            self.draw.bezier(0, 0, 10, 10, 20, -10, 30, 0, segments=8)
            self.draw.arc(100, 100, 20, 0, 180, segments=16)
        commands = self.reader.commands()
        kinds = [struct.unpack_from("<i", c)[0] for c in commands]
        self.assertEqual(kinds, [ROUND_RECT, ROUND_RECT, ELLIPSE, ELLIPSE,
                                 POINTS, POINTS, POINTS, POINTS, POINTS])
        # filled flag lives after the 5 geometry floats + 4 colour floats + thickness
        self.assertEqual(struct.unpack_from("<i", commands[0], OFF_SHAPE_FILLED)[0], 0)
        self.assertEqual(struct.unpack_from("<i", commands[1], OFF_SHAPE_FILLED)[0], 1)
        # ellipse repeats the same layout, so the filled flags are at the same offset
        self.assertEqual(struct.unpack_from("<i", commands[2], OFF_SHAPE_FILLED)[0], 0)
        self.assertEqual(struct.unpack_from("<i", commands[3], OFF_SHAPE_FILLED)[0], 1)
        # triangle and polygon are filled; polyline/bezier/arc are strokes
        filled = [struct.unpack_from("<i", c, OFF_POINTS_FILLED)[0] for c in commands[4:]]
        self.assertEqual(filled, [1, 0, 0, 0, 0])
        # the closed quad is flagged closed, the polyline is not
        first, count = struct.unpack_from("<ii", commands[5], OFF_POINTS_FIRST)
        self.assertEqual(count, 4)
        self.assertEqual(struct.unpack_from("<i", commands[5], OFF_POINTS_CLOSED)[0], 1)
        self.assertEqual(self.reader.points()[first:first + 4],
                         [(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)])
        # the bezier emitted segments+1 flattened vertices
        _, bezier_count = struct.unpack_from("<ii", commands[7], OFF_POINTS_FIRST)
        self.assertEqual(bezier_count, 9)

    def test_polygon_validation(self):
        with self.assertRaises(ValueError):
            with self.draw.frame():
                self.draw.polygon([(0, 0)])
        with self.assertRaises(ValueError):
            with self.draw.frame():
                self.draw.polygon([(i, 0) for i in range(4097)])
        with self.assertRaises(ValueError):
            with self.draw.frame():
                self.draw.polygon([(0, 0), (1, 1)], thickness=0)
        with self.assertRaises(ValueError):
            with self.draw.frame():
                self.draw.bezier(0, 0, 1, 1, 2, 2, 3, 3, segments=999)

    def test_point_pool_resets_between_frames(self):
        with self.draw.frame():
            self.draw.polygon([(0, 0), (1, 1), (2, 2)])
        self.assertEqual(self.reader.header().points, 3)
        with self.draw.frame():
            self.draw.polyline([(5, 5), (6, 6)])
        # The pool restarts, so this frame holds only its own two vertices.
        state = self.reader.header()
        self.assertEqual(state.points, 2)
        self.assertEqual(self.reader.points(), [(5.0, 5.0), (6.0, 6.0)])

    def test_overflow_flag_reports_dropped_commands(self):
        with self.draw.frame():
            for _ in range(500):
                self.draw.line(0, 0, 1, 1)
        self.assertFalse(self.draw.overflowed)
        with self.draw.frame():
            # One past MAX_DRAW, so the extra command is dropped and flagged.
            for _ in range(501):
                self.draw.line(0, 0, 1, 1)
        self.assertTrue(self.draw.overflowed)
        self.assertEqual(len(self.reader.commands()), 500)
        self.assertEqual(self.reader.header().overflow, 1)
        with self.draw.frame():
            self.draw.line(0, 0, 1, 1)
        self.assertFalse(self.draw.overflowed)

    def test_hotkey_spec_parsing(self):
        self.assertEqual(_parse_hotkey("ctrl+alt+q"), (ord("Q"), 0x0003, "ctrl+alt+q"))
        self.assertEqual(_parse_hotkey("Ctrl + Shift + F5"), (0x74, 0x0006, "Ctrl+Shift+F5"))
        self.assertEqual(_parse_hotkey("win+space"), (0x20, 0x0008, "win+space"))
        for bad in ("q", "", "ctrl+", "ctrl+alt+q+r", "ctrl+nope", "ctrl+alt+nope"):
            with self.assertRaises(ValueError):
                _parse_hotkey(bad)

    def test_hotkey_registration_and_cleanup(self):
        hotkey = self.draw.hotkey("ctrl+alt+f9")
        self.assertEqual(hotkey.display, "ctrl+alt+f9")
        self.assertIn(hotkey, self.draw._hotkeys)
        # Not pressed, so the level-triggered query is false.
        self.assertFalse(hotkey.fired)
        self.assertFalse(self.draw.key_down("ctrl"))
        self.assertFalse(self.draw.key_down("f9"))
        hotkey.close()
        self.assertEqual(self.draw._hotkeys, [])
        with self.assertRaises(RuntimeError):
            hotkey.fired
        # Closing the whole drawing object must not raise with no hotkeys left.
        self.draw.close()

    def test_hotkey_rejects_duplicates_and_invalid_names(self):
        hotkey = self.draw.hotkey("ctrl+alt+f8")
        with self.assertRaises(ValueError):
            self.draw.hotkey("ctrl+alt+f8")
        with self.assertRaises(ValueError):
            self.draw.hotkey("f8")  # no modifier
        hotkey.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
