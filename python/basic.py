"""Smallest runnable drawing example. Run: python python/basic.py"""
import time
from dwm_draw import DwmDraw


def main():
    with DwmDraw() as draw:
        draw.set_position(200, 100)  # All drawing coordinates below are relative to this origin.
        print("Python 绘图已启动，Esc / Ctrl+C 退出。")
        while draw.running():
            with draw.frame() as ready:
                if ready:
                    draw.text(0, 0, "你好，这是 Python 绘制的文字", (255, 255, 255))
                    draw.rect(0, 50, 240, 140, (0, 255, 100), thickness=2)
                    draw.line(0, 220, 300, 220, (255, 220, 0), thickness=3)
                    draw.circle(330, 120, 45, (80, 160, 255), filled=True)
            time.sleep(1 / 60)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
