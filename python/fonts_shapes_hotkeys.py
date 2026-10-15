"""字体字号、几何图形和全局快捷键示例。运行：python python/fonts_shapes_hotkeys.py"""
import time
from dwm_draw import DwmDraw, _PROTOCOL_VERSION


def main():
    with DwmDraw() as draw:
        print(f"绘图协议 v{_PROTOCOL_VERSION}；Ctrl+Alt+Q 退出，Ctrl+Alt+1 切换文字颜色。")
        # 字体句柄只创建一次即可复用；size=None 时使用渲染端默认字号（16px）。
        title = draw.font("Microsoft YaHei", 28, ranges="chinese")
        body = draw.font("Microsoft YaHei", 16, ranges="chinese")
        mono = draw.font("Consolas", 14, ranges="default")
        # 全局快捷键：窗口未获得焦点时同样生效。
        quit_key = draw.hotkey("ctrl+alt+q")
        accent_key = draw.hotkey("ctrl+alt+1")
        palette = [(255, 220, 100), (120, 220, 255), (255, 140, 180)]
        accent = 0
        printed = False
        while draw.running(stop_on_escape=False):
            # 每帧读取一次，读取即消费本次按下事件。
            if quit_key.fired:
                break
            if accent_key.fired:
                accent = (accent + 1) % len(palette)
                printed = False
            if draw.key_down("shift") and not printed:
                print("Shift 按住中（key_down 轮询状态）")
                printed = True

            with draw.frame() as ready:
                if ready:
                    draw.set_position(240, 120)
                    draw.text(0, 0, "字体与字号可选", color=palette[accent], font=title)
                    draw.text(0, 44, "这一行是 16px 微软雅黑；下面一行是等宽字体。", font=body)
                    draw.text(0, 72, "consolas 14px: x = 3.14159", color=(160, 255, 180), font=mono)
                    # 也可以直接按字号临时覆盖，无需预先创建句柄。
                    draw.text(0, 100, "同一字体族，18px 覆盖", color=(255, 255, 255),
                              font="Microsoft YaHei", size=18)

                    # 几何图形：圆角矩形、椭圆、三角形、多边形、贝塞尔、圆弧。
                    draw.filled_round_rect(0, 140, 200, 60, 14, (40, 90, 160, 200))
                    draw.round_rect(0, 140, 200, 60, 14, (140, 200, 255), thickness=2)
                    draw.filled_ellipse(110, 260, 100, 45, (60, 140, 90, 200))
                    draw.ellipse(110, 260, 100, 45, (150, 255, 180), thickness=2)
                    # 凹多边形（五角星）会自动三角化，不会画成错误的凸包。
                    draw.polygon([(20, 330), (48, 400), (0, 400), (62, 400),
                                  (14, 400), (42, 330), (35, 415), (20, 355),
                                  (55, 355), (40, 415)], (255, 200, 60), filled=True)
                    draw.bezier(240, 330, 320, 280, 360, 430, 440, 360, (120, 200, 255),
                                thickness=3, segments=32)
                    draw.arc(330, 250, 60, 200, 340, (255, 120, 120), thickness=3)
                    draw.triangle(250, 400, 330, 400, 290, 470, (200, 120, 255),
                                  thickness=2, filled=True)
                    draw.polyline([(240, 150), (280, 180), (320, 130), (360, 190), (400, 140)],
                                  (255, 255, 255), thickness=2)
            time.sleep(1 / 60)
        print("已退出。")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
