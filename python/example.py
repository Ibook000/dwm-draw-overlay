"""Run: python python/example.py. The renderer starts automatically."""
import argparse
import time
from dwm_draw import DwmDraw


def main():
    parser = argparse.ArgumentParser(description="Python 中文绘图和滚轮面板示例")
    parser.add_argument("--x", type=float, default=300, help="面板屏幕横坐标")
    parser.add_argument("--y", type=float, default=150, help="面板屏幕纵坐标")
    parser.add_argument("--seconds", type=float, default=0, help="自动退出秒数；0 为持续运行")
    args = parser.parse_args()
    with DwmDraw() as draw:
        panel = draw.scroll_panel(x=args.x, y=args.y, width=480, height=320,
                                  content_height=40 * 26 + 24, step=48)
        print("鼠标放到面板内，用滚轮上下查看；Esc / Ctrl+C 退出。")
        started = time.monotonic()
        while draw.running():
            if args.seconds > 0 and time.monotonic() - started >= args.seconds:
                break
            with draw.frame() as ready:
                if ready:
                    draw.text(args.x, args.y - 30, "Python 中文绘图 / 鼠标滚轮", (255, 220, 100))
                    with panel:
                        for row in range(40):
                            draw.text(12, 12 + row * 26, f"第 {row + 1:02d} 行：这是 Python 绘制的文字",
                                      (170, 210, 255) if row % 2 else (255, 255, 255))
                    draw.rect(args.x + 520, args.y, 150, 90, (0, 255, 100), thickness=2)
                    draw.line(args.x + 520, args.y + 110, args.x + 670, args.y + 110, (255, 220, 0))
                    draw.circle(args.x + 595, args.y + 180, 40, (80, 160, 255), filled=True)
            time.sleep(1 / 60)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
