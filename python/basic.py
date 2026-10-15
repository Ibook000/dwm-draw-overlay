"""Smallest runnable drawing example. Run: python python/basic.py"""
import time
from dwm_draw import DwmDraw


def main():
    with DwmDraw() as draw:
        draw.set_position(200, 100)  # All drawing coordinates below are relative to this origin.
        # 字体句柄可以复用；这里按需选择字体族和字号。
        heading = draw.font("Microsoft YaHei", 22, ranges="chinese")
        print("Python 绘图已启动，Esc / Ctrl+C 退出。")
        while draw.running():
            with draw.frame() as ready:
                if ready:
                    draw.text(0, 0, "你好，这是 Python 绘制的文字", (255, 255, 255), font=heading)
                    draw.rect(0, 50, 240, 140, (0, 255, 100), thickness=2)
                    # 圆角矩形和椭圆是新增的几何图形。
                    draw.filled_round_rect(260, 50, 180, 60, 12, (40, 90, 160, 200))
                    draw.round_rect(260, 50, 180, 60, 12, (140, 200, 255), thickness=2)
                    draw.filled_ellipse(350, 180, 90, 45, (90, 200, 120, 180))
                    draw.line(0, 220, 300, 220, (255, 220, 0), thickness=3)
                    draw.circle(330, 120, 45, (80, 160, 255), filled=True)
                    draw.text(300, 200, "Hello, this is text drawn by Python", (255, 255, 255))
            time.sleep(1 / 60)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
