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

颜色接受 `(R, G, B)` 或 `(R, G, B, A)`，整数范围 0~255。省略 A 表示完全不透明。中文直接使用 Python 字符串，内部自动转 UTF-8。文字大小当前为 16 像素，最多 255 个 UTF-8 字节；一帧最多 500 条命令，裁剪和面板背景也计入命令数。

| Python 接口 | 用途 |
| --- | --- |
| `draw.set_position(x, y)` | 设置所有后续图形共同使用的原点，主屏左上为 `(0, 0)` |
| `draw.text(x, y, text, color, outlined=True)` | 中文或英文文字 |
| `draw.rect(x, y, width, height, color, thickness=2)` | 矩形边框 |
| `draw.rect(..., filled=True)` | 填充矩形 |
| `draw.rect(..., corner=True)` | 矩形四角边框 |
| `draw.line(x1, y1, x2, y2, color, thickness=2)` | 线段 |
| `draw.circle(x, y, radius, color, thickness=2, segments=64)` | 圆形边框 |
| `draw.circle(..., filled=True)` | 填充圆形 |
| `with draw.clip(x, y, width, height):` | 限制区域内的图形显示 |
| `draw.clear()` | 清除上一帧；忙时返回 False |
| `draw.fps` | 获取显示端帧率估计 |
| `draw.hide()` / `draw.show()` | 隐藏或显示窗口 |
| `draw.running()` | 处理滚轮消息并检查 Esc / 窗口退出；`stop_on_escape=False` 可禁用 Esc 检查 |
| `draw.close()` | 释放面板和底层资源；`with DwmDraw()` 退出时自动调用 |

所有绘图操作放在成功的 `with draw.frame() as ready:` 内；`ready=False` 表示通道暂忙，跳过本帧。异常离开帧时取消该帧并释放锁。每帧替换上一帧内容；`with panel:` 同样放在帧内。

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

Python 接口测试使用独立共享内存，不改动正在显示的内容。`tests/test_python_gui.py` 在独立测试桌面验证自动启动、多个显示端独立运行、滚轮初始化、实际接收帧和退出，不切换用户桌面；`--legacy` 额外验证旧窗口的控制消息被拒绝时仍可正常启动绘制，`--orphan` 验证 Python 意外结束后显示端退出。它需要创建桌面的权限，受限沙箱可能返回 Win32 错误 5。显示端启动错误会抛出 Python 异常，并附带临时日志路径。
