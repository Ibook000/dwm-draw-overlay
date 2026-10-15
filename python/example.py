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
        # 面板标题用较大字号，行内容用默认字号。
        title = draw.font("Microsoft YaHei", 18, ranges="chinese")
        # Ctrl+Alt+H 可切换显示端窗口的显示状态，这里演示读取快捷键状态。
        toggle = draw.hotkey("ctrl+alt+h")
        hidden = False
        print("鼠标放到面板内，用滚轮上下查看；Esc / Ctrl+C 退出。Ctrl+Alt+H 隐藏/显示。")
        started = time.monotonic()
        while draw.running():
            if args.seconds > 0 and time.monotonic() - started >= args.seconds:
                break
            if toggle.fired:
                hidden = not hidden
                draw.hide() if hidden else draw.show()
            with draw.frame() as ready:
                if ready:
                    draw.text(args.x, args.y - 30, "Python 中文绘图 / 鼠标滚轮", (255, 220, 100), font=title)
                    with panel:
                        for row in range(40):
                            draw.text(12, 12 + row * 26, f"第 {row + 1:02d} 行：这是 Python 绘制的文字",
                                      (170, 210, 255) if row % 2 else (255, 255, 255))
                    draw.rect(args.x + 520, args.y, 150, 90, (0, 255, 100), thickness=2)
                    draw.line(args.x + 520, args.y + 110, args.x + 670, args.y + 110, (255, 220, 0))
                    draw.circle(args.x + 595, args.y + 180, 40, (80, 160, 255), filled=True)
                    draw.filled_round_rect(args.x + 520, args.y + 240, 150, 50, 10, (60, 120, 200, 210))
            time.sleep(1 / 60)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
