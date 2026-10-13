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
from dwm_draw import DwmDraw


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
        self.address = self.api.MapViewOfFile(self.handle, 4, 0, 0, 144024)
        if not self.address:
            raise C.WinError(C.get_last_error())

    def commands(self):
        data = C.string_at(self.address, 144024)
        magic, version, size, number, fps, count = struct.unpack_from("<iIIIii", data)
        assert version == 3 and size == 144024
        return [data[24 + i * 288:24 + (i + 1) * 288] for i in range(count)]

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
        self.assertEqual((kind, x, y), (1, 112, 224))
        self.assertAlmostEqual(g, 128 / 255, places=6)
        self.assertAlmostEqual(a, 64 / 255, places=6)
        self.assertEqual(commands[0][28], 0)
        self.assertEqual(commands[0][29:].split(b"\0", 1)[0].decode("utf-8"), "你好 Python")
        self.assertEqual(struct.unpack_from("<i", commands[2])[0], 2)

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
        self.assertEqual([struct.unpack_from("<i", c)[0] for c in commands], [9, 6, 1, 10, 7])
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
        self.draw._renderer_windows = lambda: iter([(101, 1, None, False), (202, 2, 3, True)])
        self.assertIsNone(self.draw._find_renderer())
        self.draw._process = SimpleNamespace(pid=2)
        try:
            self.assertEqual(self.draw._find_renderer(), 202)
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
