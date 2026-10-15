# Python 绘图接口

需要 Windows 和 **64 位 Python 3.10+**。仅使用标准库 `ctypes`，没有 pip 依赖。底层已编译为 `bin/dwm_draw.dll`，显示端为 `bin/overlay_python.exe`。

在 `F:\DWM` 直接运行：

```powershell
python .\python\basic.py
```

滚动面板示例：

```powershell
python .\python\example.py --x 300 --y 150
```

字体、几何图形和快捷键综合示例：

```powershell
python .\python\fonts_shapes_hotkeys.py
```

显示端自动以隐藏控制台的方式启动，桌面上显示绘图内容。Esc / Ctrl+C 清空内容并退出；Python 只关闭它自己启动的显示端。Python 进程被强制结束时，它启动的显示端也会自动退出。默认由 Python 管理窗口，不占用全局 Ctrl+Alt 快捷键。`--seconds 3` 可以让滚动示例在三秒后自动退出。

## 最小调用

每个 `DwmDraw()` 自动创建自己的显示端和独立绘图通道。已有旧窗口、管理员窗口或 C++ 发送程序都不会阻止 Python 启动，也不会覆盖它的绘图内容。关闭一个对象只清除该对象的内容；`hide()` / `show()` 也只控制它自己的窗口。已有旧窗口会保留，可以在需要时单独退出。

```python
import time
from dwm_draw import DwmDraw

with DwmDraw() as draw:
    draw.set_position(200, 100)
    while draw.running():
        with draw.frame() as ready:
            if ready:
                draw.text(0, 0, "你好，世界", (255, 255, 255))
                draw.rect(0, 50, 200, 100, (0, 255, 0), thickness=2)
        time.sleep(1 / 60)
```

把 `python/dwm_draw.py` 放在自己的脚本旁边即可导入。默认按当前工程目录找到 DLL 和显示端；移到其他项目时可以显式指定：

```python
draw = DwmDraw(dll_path=r"F:\DWM\bin\dwm_draw.dll",
               renderer_path=r"F:\DWM\bin\overlay_python.exe")
```

## 绘图接口

颜色接受 `(R, G, B)` 或 `(R, G, B, A)`，整数范围 0~255。省略 A 表示完全不透明。中文直接使用 Python 字符串，内部自动转 UTF-8。默认字号 16 像素，可通过 `font=` / `size=` 指定；文字最多 255 个 UTF-8 字节；一帧最多 500 条命令、多边形共 4096 个顶点，裁剪和面板背景也计入命令数。

| Python 接口 | 用途 |
| --- | --- |
| `draw.set_position(x, y)` | 设置所有后续图形共同使用的原点，主屏左上为 `(0, 0)` |
| `draw.text(x, y, text, color, outlined=True)` | 中文或英文文字，使用默认字体（16 像素） |
| `draw.text(..., font="Consolas", size=20)` | 指定字体族和字号，见下文「字体与字号」 |
| `draw.font(family, size, ranges="auto")` | 预先创建字体句柄，循环中复用 |
| `draw.rect(x, y, width, height, color, thickness=2)` | 矩形边框 |
| `draw.rect(..., filled=True)` | 填充矩形 |
| `draw.rect(..., corner=True)` | 矩形四角边框 |
| `draw.round_rect(x, y, w, h, radius, color, thickness=2)` | 圆角矩形边框 |
| `draw.filled_round_rect(x, y, w, h, radius, color)` | 填充圆角矩形 |
| `draw.ellipse(x, y, rx, ry, color, thickness=2, segments=64)` | 椭圆边框 |
| `draw.filled_ellipse(x, y, rx, ry, color, segments=64)` | 填充椭圆 |
| `draw.line(x1, y1, x2, y2, color, thickness=2)` | 线段 |
| `draw.circle(x, y, radius, color, thickness=2, segments=64)` | 圆形边框 |
| `draw.circle(..., filled=True)` | 填充圆形 |
| `draw.triangle(x1, y1, x2, y2, x3, y3, color, filled=False)` | 三角形 |
| `draw.polygon(points, color, filled=False, closed=True)` | 多边形 / 折线，`points` 为 `(x, y)` 序列 |
| `draw.polyline(points, color, thickness=2)` | 开放折线 |
| `draw.bezier(x0, y0, x1, y1, x2, y2, x3, y3, color, thickness=2, segments=32)` | 三次贝塞尔曲线 |
| `draw.arc(x, y, radius, start, end, color, thickness=2, segments=32)` | 圆弧；0 度指向右侧，角度向下递增 |
| `draw.hotkey("ctrl+alt+q")` | 注册全局快捷键，返回 `Hotkey` |
| `draw.key_down("shift")` | 轮询按键当前是否按下 |
| `draw.overflowed` | 上一帧是否有指令 / 顶点被丢弃 |
| `with draw.clip(x, y, width, height):` | 限制区域内的图形显示 |
| `draw.clear()` | 清除上一帧；忙时返回 False |
| `draw.fps` | 获取显示端帧率估计 |
| `draw.hide()` / `draw.show()` | 隐藏或显示窗口 |
| `draw.running()` | 处理滚轮和快捷键消息并检查 Esc / 窗口退出；`stop_on_escape=False` 可禁用 Esc 检查 |
| `draw.close()` | 释放快捷键、面板和底层资源；`with DwmDraw()` 退出时自动调用 |

所有绘图操作放在成功的 `with draw.frame() as ready:` 内；`ready=False` 表示通道暂忙，跳过本帧。异常离开帧时取消该帧并释放锁。每帧替换上一帧内容；`with panel:` 同样放在帧内。

## 字体与字号

`font=` 可以是 Windows 字体族名（`"Microsoft YaHei"`、`"Consolas"`、`"SimHei"`）、`.ttf` / `.ttc` 的完整路径，或 `draw.font()` 返回的句柄；`None` 表示渲染端默认字体。`size=` 为该次调用的像素高，省略时使用字体句柄记录的字号（默认 16px）。

```python
with DwmDraw() as draw:
    # 句柄只创建一次，之后每帧复用；相同 (字体族, 字号, 覆盖范围) 会复用同一个句柄。
    title = draw.font("Microsoft YaHei", 28, ranges="chinese")
    body = draw.font("Microsoft YaHei", 16, ranges="chinese")
    mono = draw.font("Consolas", 14, ranges="default")
    while draw.running():
        with draw.frame() as ready:
            if ready:
                draw.text(0, 0, "大标题", (255, 220, 100), font=title)
                draw.text(0, 40, "正文内容", font=body)
                draw.text(0, 70, "monospace: x = 1", font=mono)
                # 也可以直接传字体族名并临时覆盖字号。
                draw.text(0, 100, "临时 18px", font="Microsoft YaHei", size=18)
        time.sleep(1 / 60)
```

`ranges` 可选 `"auto"`（CJK 字体族自动选择中文覆盖）、`"default"`、`"chinese"`、`"japanese"`、`"korean"`、`"cyrillic"`。字体族通过注册表解析为真实字体文件（支持系统与用户目录安装）；无法解析时会回退到可用的中文字体或西文字体，而不是不显示文字。默认字体优先使用微软雅黑，因此缺少 SimHei 不再导致中文变方块。

新增字体组合时渲染端会重建字体图集，因此建议在循环外调用 `draw.font()` 固定字形，避免中途频繁引入新字号。

## 全局快捷键

快捷键注册在 Python 进程中，因此显示端窗口未获得焦点时同样生效；Python 启动的显示端不注册任何热键，不会冲突。语法为 `修饰键+主按键`，修饰键至少一个：`ctrl` / `alt` / `shift` / `win`（`control`、`super`、`meta` 亦可）。主按键可用单个字母数字、`F1`~`F24`，或 `space`、`enter`、`tab`、`esc`、`left`、`delete` 等具名按键。

```python
with DwmDraw() as draw:
    quit_key = draw.hotkey("ctrl+alt+q")
    inc_key = draw.hotkey("ctrl+alt+up")
    while draw.running(stop_on_escape=False):
        if quit_key.fired:          # 读取即消费，每次按下只返回一次 True
            break
        if inc_key.fired:
            print("加一")
        if draw.key_down("shift"):  # 轮询按键状态
            print("Shift 按住中")
        with draw.frame() as ready:
            if ready:
                draw.text(10, 10, "按 Ctrl+Alt+Q 退出")
        time.sleep(1 / 60)
```

`draw.hotkey()` 返回 `Hotkey`，其 `display` 为规范化后的名称（`"ctrl+alt+q"`）。重复注册同一组合或不带修饰键会抛出 `ValueError`；组合被其他程序占用时抛出 `OSError`（Win32 错误 1409）。用 `hotkey.close()` 或退出 `with DwmDraw()` 自动注销。`draw.key_down()` 接受按键名或虚拟键码整数。

宏按键（如 `ctrl+alt+1`）可能被某些键盘布局或输入法拦截，请以实际环境为准。受限沙箱可能禁止注册全局热键并返回 Win32 错误 5。

## 鼠标滚轮面板

```python
with DwmDraw() as draw:
    panel = draw.scroll_panel(x=300, y=150, width=460, height=320,
                              content_height=1200, step=48)
    while draw.running():
        with draw.frame() as ready:
            if ready:
                with panel:
                    draw.text(12, 12, "第一行")
                    draw.text(12, 1000, "向下滚动可以看到我")
        time.sleep(1 / 60)
```

面板内使用内容坐标，自动减去滚动偏移并裁剪。鼠标移入面板后用滚轮滚动；面板外继续传递输入，显示端隐藏时暂停面板滚轮处理。以下接口均可直接调用：

```python
panel.set_position(500, 200)
panel.set_size(400, 300)
panel.set_content_height(2000)
panel.set_scroll_step(60)
panel.scroll_to(200)
panel.scroll_by(50)
panel.set_background((20, 24, 32, 200))
panel.set_consume_wheel(False)  # 同时让下方程序收到滚轮
panel.set_input_enabled(False)
panel.detach_mouse_wheel()
panel.attach_mouse_wheel()
print(panel.scroll_offset, panel.max_scroll_offset)
```

`mouse_wheel=False` 可以仅由代码控制偏移。所有对象在创建它们的同一个线程使用；绘图循环应调用 `draw.running()` 处理鼠标消息。多个默认 Python 对象使用独立通道；显式使用 `auto_start=False` 连接同一共享通道时，仍应只运行一个发送程序。只覆盖主屏，坐标为物理像素。

## 构建和验证

```powershell
.\build.ps1 -KeepBuild
python .\tests\test_python_api.py
```

Python 接口测试使用独立共享内存，不改动正在显示的内容。`tests/test_python_gui.py` 在独立测试桌面验证自动启动、多个显示端独立运行、滚轮初始化、字体 / 字号、新几何图形、快捷键注册、实际接收帧和退出，不切换用户桌面；`--legacy` 额外验证旧窗口的控制消息被拒绝时仍可正常启动绘制，`--orphan` 验证 Python 意外结束后显示端退出。`tests/smoke_example.py` 会实际绘制字体、图形和快捷键示例若干帧并校验帧率与溢出标志。它需要创建桌面的权限，受限沙箱可能返回 Win32 错误 5。显示端启动错误会抛出 Python 异常，并附带临时日志路径。
