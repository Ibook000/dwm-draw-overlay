# Windows 11 捕获排除悬浮窗口

利用 `WDA_EXCLUDEFROMCAPTURE` 实现的屏幕捕获排除透明悬浮窗：DX11 + ImGui 绘图，支持 UTF-8 中文文字、几何图形、鼠标滚轮面板，提供 Python 与 C++ 两种接口。

## 获取 vendor/imgui

`vendor/imgui/` 是 Dear ImGui **v1.70** 的精简子集（核心源文件 + DX11 backend，扁平目录布局），CMake 直接引用其中的 `imgui.cpp`、`imgui_draw.cpp`、`imgui_widgets.cpp`、`imgui_impl_dx11.cpp`。为保证 clone 后开箱即构建，该目录已随仓库分发；若需升级，请替换为对应版本 ImGui 并适配目录结构。

## Python 使用

Python 接口已封装完成：直接运行 `python .\python\example.py`。每个绘图对象自动启动自己的 `bin/overlay_python.exe` 并使用独立绘图通道，旧窗口无需先退出。可绘制中文、设置位置并使用鼠标滚轮面板；Esc / Ctrl+C 清空退出。需要 64 位 Python，无第三方 Python 依赖。简单绘图运行 `python .\python\basic.py`。完整接口见 [Python 使用说明](python/README.md)。

底层 DLL 为 `bin/dwm_draw.dll`，Python 封装为 `python/dwm_draw.py`。新版显示端默认构建输出名称为 `overlay_python.exe`。

最新版显示端为 `bin/overlay_python.exe`，通信协议为 v3。手动运行 C++ 示例时，先按 Ctrl+Alt+Q 退出旧监听端，再启动新版；发送端也需使用新编译的程序，同一通道只运行一个发送端。Python 默认自动管理独立通道，可与旧窗口及 C++ 示例同时运行。

独立透明置顶窗口由 DWM 正常合成，通过 `WDA_EXCLUDEFROMCAPTURE` 从支持该标志的截图、录屏或共享接口中排除。无需管理员权限、注入 DLL 或修改系统进程。

## 运行

```powershell
.\bin\overlay_python.exe
```

- `Ctrl+Alt+H`：隐藏 / 显示。
- `Ctrl+Alt+Q`：退出。
- 窗口允许鼠标点击穿透、不抢焦点。

程序在窗口显示前设置捕获排除并读回 `0x11`；设置或热键注册失败时报告错误并退出。

## 构建与验证

需要 CMake、VS 2019 Build Tools 的 v142 工具集和 Windows SDK 10.0.19041.0。

```powershell
.\build.ps1 -OverlayTest
.\tests\overlay_lifecycle.ps1
```

输出为 `bin/overlay_python.exe`、`bin/draw_example.exe` 和 `bin/scroll_example.exe`。构建时验证绘图 API、滚轮方向、区域命中、滚动边界和面板坐标恢复。构建成功后自动删除中间文件；加 `-KeepBuild` 可保留 `.build/` 供增量构建。编译失败时保留中间文件供排查。

`--self-test` 内置检查不透明 / 半透明 / 全透明像素、点击穿透、affinity 读回，以及 GDI `BitBlt(SRCCOPY | CAPTUREBLT)` 的捕获对照：关闭排除时截到红色悬浮块，开启排除时截到下面的蓝色窗口。生命周期脚本检查演示与监听模式的启动、隐藏 / 显示保留排除设置和退出清理，日志写入系统临时目录。

本机上述检查已通过。测试需要交互桌面；受限沙箱可能禁止全局热键注册并返回 Win32 错误 5。OBS、Teams、Zoom 等软件尚未实测，请用实际捕获模式验证；该 API 不保证所有捕获方式都排除内容。[微软文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)

## 许可

本项目代码未设许可证前仅供参考。`vendor/imgui/` 遵循 [Dear ImGui 的 MIT 许可](https://github.com/ocornut/imgui/blob/master/LICENSE.txt)。

## 代码

- `src/overlay_demo.cpp`：窗口、捕获排除、热键、DX11 帧提交及自测。
- `src/` 其余文件：绘图命令协议、发送端和渲染器。
- `src/` 其余文件：绘图命令协议、发送端和渲染器。
- `vendor/imgui/`：渲染所需的 Dear ImGui 源码依赖（v1.70 精简子集，随仓库分发，见上文）。
- `tests/overlay_lifecycle.ps1`：窗口生命周期检查。
- `src/DwmDraw.h`：便于直接调用的 C++ 绘图封装。
- `examples/draw_functions.cpp`：中文文字、矩形、线条、圆形和半透明填充的调用示例。

默认演示窗口为 640 × 360，位于主显示器 `(24, 24)`。`--listen` 接收 `src/DrawProtocol.h` 定义的 v3 共享内存协议，坐标为主显示器局部物理像素；自带演示使用独立映射名。外部 C++ 发送端可使用 `CDwmMsg` 的 `InitMsg → ExeStartDraw → Draw… → ExeEndDraw`，颜色范围为 0 到 1。

绘制采用 DX11 + ImGui，帧经 GPU → CPU 读回后通过 `UpdateLayeredWindow` 提交预乘 BGRA 内容，约每 33 ms 加绘制耗时更新一次。高分辨率监听模式会增加读回开销。监听端保留最后一帧，发送端应提交空帧清除。仅覆盖主显示器；监听时显示配置变化会退出并提示重启。

## 调用绘图函数

先启动显示端，并保持运行：

```powershell
.\bin\overlay_python.exe --listen
```

在第二个 PowerShell 窗口运行调用示例：

```powershell
.\bin\draw_example.exe
```

此时主屏左上区域会显示中文文字及示例图形。调用示例按 Esc 或 Ctrl+C 清空并退出，显示端按 Ctrl+Alt+Q 退出。若之前已打开默认演示模式，请先退出，再以 `--listen` 启动；默认演示使用自己的映射，不接收外部调用。

修改 `examples/draw_functions.cpp` 中的以下调用，然后执行 `.\build.ps1`：

```cpp
#include "DwmDraw.h"

DwmDraw draw;
// 初始化一次；失败时退出或稍后重试。
if (!draw.init()) return 1;

// 在自己的绘制循环中重复提交完整的一帧。
if (draw.begin()) {
    draw.text(50, 50, u8"你好，世界", {255, 255, 255, 255});
    draw.rect(50, 120, 240, 140, {0, 255, 0, 255}, 2);
    draw.line(50, 290, 350, 290, {255, 220, 0, 255}, 3);
    draw.circle(400, 190, 50, {80, 160, 255, 255}, 2);
    draw.end();
}
```

新封装 `DwmDraw` 使用 **RGBA 0 到 255**，透明度 `a=255` 为不透明，`a=0` 为透明；内部自动转换为底层 `CDwmMsg` 的 0 到 1 范围。坐标以主显示器左上角为 `(0, 0)`，向右 / 向下增加，单位为物理像素。

| 调用 | 功能 |
| --- | --- |
| `text(x, y, utf8, color, outlined)` | UTF-8 文字，默认白色并带阴影；当前字体大小为 16 像素 |
| `rect(x, y, w, h, color, thickness)` | 矩形边框 |
| `filledRect(x, y, w, h, color)` | 填充矩形 |
| `cornerRect(x, y, w, h, color, thickness)` | 矩形四角线框 |
| `line(x1, y1, x2, y2, color, thickness)` | 线段 |
| `circle(x, y, radius, color, thickness, segments)` | 圆形边框 |
| `filledCircle(x, y, radius, color, segments)` | 填充圆形 |
| `clear()` | 提交空帧清除内容；通道忙时返回 false |

一帧中所有图形放在成功的 `begin()` 和 `end()` 之间；每帧内容替换上一帧。每个对象由同一线程操作，一帧最多 500 条指令，文字最多 255 个 UTF-8 字节。中文源码保存为 UTF-8，使用 C++17 和 MSVC 的 `/utf-8` 编译选项。

集成到自己的 C++ 工程时，加入 `src/CDwmMsg.cpp`，并把 `src`、`vendor/imgui` 加入头文件搜索目录；发送端无需编译完整 ImGui 渲染源文件。项目内已有的 `draw_example` CMake 目标给出了完整构建配置。

## 设置位置和鼠标滚动

`DwmDraw::setPosition(x, y)` 设置后续图形共同使用的原点。例如 `draw.setPosition(300, 200)` 后，`draw.text(10, 20, ...)` 在屏幕 `(310, 220)` 显示；`draw.setPosition(0, 0)` 恢复绝对屏幕坐标。

鼠标滚动已封装在 `src/DwmScrollPanel.h` 中。启动新版显示端后，在另一个终端运行：

```powershell
.\bin\scroll_example.exe --x 300 --y 150 --width 460 --height 320 --step 48
```

鼠标放在面板内，滚轮上下查看 40 行内容。`x/y` 设置面板左上角，`width/height` 设置可见区域，`step` 设置每格滚轮移动的像素数。超出面板的文字和图形会裁剪；滚动到顶部或底部会停止。面板内默认消费滚轮事件，避免同时滚动下方程序，面板外继续传递输入。示例在显示端隐藏或退出时停止消费滚轮。

```cpp
#include "DwmScrollPanel.h"

// 初始化一次，在与绘图循环相同的线程操作。
DwmDraw draw;
if (!draw.init()) return 1;
DwmScrollPanel panel;
panel.setPosition(300, 150);   // 屏幕位置
panel.setSize(460, 320);       // 可见区域
panel.setContentHeight(1200);  // 内容总高度
panel.setScrollStep(48);       // 每格滚轮移动 48 像素
if (!panel.attachMouseWheel()) return 1;

// 每帧调用；pollEvents 处理鼠标滚轮，WM_QUIT 时返回 false。
if (!DwmScrollPanel::pollEvents()) return 0;
if (draw.begin()) {
    if (panel.begin(draw)) {
        draw.text(12, 12, u8"第一行");
        draw.text(12, 38, u8"第二行");
        draw.text(12, 1000, u8"滚动到下面能看到这一行");
        panel.end(draw);
    }
    draw.end();
}
```

`panel.begin/end` 之间的坐标以面板内容左上角为原点，并自动减去滚动偏移、裁剪到可见区域；退出面板后恢复之前的绘图原点。每帧仍需由外层 `draw.begin/end` 提交。完整循环、退出清空和隐藏处理见 `examples/scroll_panel.cpp`。

| 接口 | 功能 |
| --- | --- |
| `setPosition(x, y)` | 设置面板的屏幕位置 |
| `setSize(width, height)` | 设置可见区域 |
| `setContentHeight(height)` | 设置内容总高度；内容变短时自动限制偏移 |
| `setScrollStep(pixels)` | 每格滚轮的滚动距离 |
| `scrollTo(offset)` / `scrollBy(pixels)` | 通过代码滚动到指定偏移或移动指定距离 |
| `scrollOffset()` / `maxScrollOffset()` | 获取当前 / 最大滚动偏移 |
| `setBackground(color)` | 设置面板背景 RGBA |
| `setConsumeWheel(false)` | 面板滚动时也将滚轮传给下方程序 |
| `setInputEnabled(false)` | 暂停处理面板滚轮，隐藏内容时使用 |
| `attachMouseWheel()` / `detachMouseWheel()` | 安装 / 移除滚轮监听；析构自动移除 |
| `handleWheel(delta, screenX, screenY)` | 从已有窗口的滚轮消息手动输入；`delta=120` 为一格向上 |

滚轮监听和绘制循环需要在同一线程，循环中调用 `pollEvents()`，或使用你自己已有的消息循环；这是 [Windows 低级鼠标回调的要求](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)。可以用 `scroll_example.exe --check-input` 检查滚轮监听是否成功。集成时额外编译 `src/DwmScrollPanel.cpp` 并链接 `user32.lib`，在高 DPI 屏幕上使用物理像素坐标；完整示例已启用每显示器 DPI 感知。
