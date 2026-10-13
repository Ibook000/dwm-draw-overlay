#include "CDwmMsg.h"
#include "CIMGuiDraw.h"
#include <dwmapi.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <string>
#include <cstdlib>

using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t ClassName[] = L"DwmDraw.CaptureExcludedOverlay";
constexpr int QuitKey = 1, HideKey = 2;
struct ParentProcess {
    HANDLE handle = nullptr;
    ~ParentProcess() { if (handle) CloseHandle(handle); }
};

void WinCheck(BOOL ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + " failed; Win32=" + std::to_string(GetLastError()));
}
void HrCheck(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char code[32]{}; sprintf_s(code, "0x%08lX", static_cast<unsigned long>(hr));
        throw std::runtime_error(std::string(operation) + " failed; HRESULT=" + code);
    }
}
void Affinity(HWND window, DWORD value) {
    WinCheck(SetWindowDisplayAffinity(window, value), "SetWindowDisplayAffinity");
    DWORD actual = WDA_NONE;
    WinCheck(GetWindowDisplayAffinity(window, &actual), "GetWindowDisplayAffinity");
    if (actual != value) throw std::runtime_error("Display affinity readback mismatch");
}

class Overlay {
public:
    explicit Overlay(const wchar_t* mapping, const wchar_t* mutex) : renderer(mapping, mutex) {}
    ~Overlay() {
        renderer.Shutdown();
        if (context) context->ClearState();
        target.Reset(); staging.Reset(); chain.Reset(); context.Reset(); device.Reset();
        if (oldBitmap && oldBitmap != HGDI_ERROR) SelectObject(memoryDC, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (memoryDC) DeleteDC(memoryDC);
        if (window) DestroyWindow(window);
    }
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void Create(int x, int y, int w, int h) {
        BOOL composition = FALSE;
        HrCheck(DwmIsCompositionEnabled(&composition), "DwmIsCompositionEnabled");
        if (!composition) throw std::runtime_error("Desktop composition is disabled");
        width = w; height = h; position = {x, y};
        // LAYERED + TRANSPARENT lets mouse input pass to other processes as well.
        window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, ClassName, L"Capture-excluded overlay",
            WS_POPUP, x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        WinCheck(window != nullptr, "CreateWindowExW");
        // Do this while hidden. Failure must never leave an unprotected visible overlay.
        Affinity(window, WDA_EXCLUDEFROMCAPTURE);

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = w; desc.BufferDesc.Height = h;
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &desc, &chain, &device, nullptr, &context);
        if (FAILED(hr)) {
            chain.Reset(); context.Reset(); device.Reset();
            HrCheck(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &desc, &chain, &device, nullptr, &context), "D3D11 WARP fallback");
        }
        ComPtr<ID3D11Texture2D> back;
        HrCheck(chain->GetBuffer(0, IID_PPV_ARGS(&back)), "GetBuffer");
        HrCheck(device->CreateRenderTargetView(back.Get(), nullptr, &target), "CreateRenderTargetView");
        D3D11_TEXTURE2D_DESC texture{}; back->GetDesc(&texture);
        texture.Usage = D3D11_USAGE_STAGING; texture.BindFlags = 0;
        texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ; texture.MiscFlags = 0;
        HrCheck(device->CreateTexture2D(&texture, nullptr, &staging), "Create staging texture");

        memoryDC = CreateCompatibleDC(nullptr);
        WinCheck(memoryDC != nullptr, "CreateCompatibleDC");
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(memoryDC, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        WinCheck(bitmap != nullptr, "CreateDIBSection");
        oldBitmap = SelectObject(memoryDC, bitmap);
        WinCheck(oldBitmap && oldBitmap != HGDI_ERROR, "SelectObject");
    }

    bool Render() {
        const float clear[4] = {0, 0, 0, 0};
        context->ClearRenderTargetView(target.Get(), clear);
        // A disconnected producer is allowed: the overlay stays transparent.
        const bool rendered = renderer.ImGuiDx11Draw(chain.Get());
        ComPtr<ID3D11Texture2D> back;
        HrCheck(chain->GetBuffer(0, IID_PPV_ARGS(&back)), "GetBuffer");
        context->CopyResource(staging.Get(), back.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HrCheck(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map staging texture");
        for (int row = 0; row < height; ++row)
            memcpy(static_cast<unsigned char*>(pixels) + static_cast<size_t>(row) * width * 4,
                static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
                static_cast<size_t>(width) * 4);
        context->Unmap(staging.Get(), 0);
        // The DX11 RGB and alpha blend equations produce premultiplied BGRA.
        // No swap-chain Present: the layered window is the only visible surface.
        SIZE size{width, height}; POINT source{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        WinCheck(UpdateLayeredWindow(window, nullptr, &position, &size, memoryDC,
            &source, 0, &blend, ULW_ALPHA), "UpdateLayeredWindow");
        return rendered;
    }

    const unsigned char* Pixel(int x, int y) const {
        return static_cast<const unsigned char*>(pixels) + (static_cast<size_t>(y) * width + x) * 4;
    }
    HWND window = nullptr;
private:
    CIMGuiDraw renderer;
    int width = 0, height = 0;
    POINT position{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11Texture2D> staging;
    HDC memoryDC = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* pixels = nullptr;
};

struct Backdrop {
    HWND window = nullptr;
    HBRUSH brush = CreateSolidBrush(RGB(0, 0, 255));
    ~Backdrop() { if (window) DestroyWindow(window); if (brush) DeleteObject(brush); }
};

COLORREF CapturePixel(int x, int y) {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 1, 1);
    if (!screen || !dc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        if (screen) ReleaseDC(nullptr, screen);
        throw std::runtime_error("Cannot allocate GDI capture resources");
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    const BOOL copied = BitBlt(dc, 0, 0, 1, 1, screen, x, y, SRCCOPY | CAPTUREBLT);
    const COLORREF color = copied ? GetPixel(dc, 0, 0) : CLR_INVALID;
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
    if (color == CLR_INVALID) throw std::runtime_error("GDI screen capture failed");
    return color;
}

int SelfTest() {
    // Isolated protocol names avoid changing a running producer's frame.
    const auto mapping = L"Local\\DwmDraw.OverlayTest." + std::to_wstring(GetCurrentProcessId());
    const auto mutex = mapping + L".Lock";
    CDwmMsg sender(mapping.c_str(), mutex.c_str());
    if (!sender.InitMsg() || !sender.ExeStartDraw()) throw std::runtime_error("Test producer init failed");
    sender.DrawFilledRect(20, 20, 80, 80, ImVec4(1, 0, 0, 1));
    sender.DrawFilledRect(120, 20, 80, 80, ImVec4(0, 1, 0, 0.5f));
    sender.ExeEndDraw();
    Backdrop background;
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DwmDraw.OverlayTest.Backdrop"; wc.hbrBackground = background.brush;
    WinCheck(RegisterClassW(&wc), "Register backdrop class");
    background.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Capture test background", WS_POPUP, 80, 80, 320, 240,
        nullptr, nullptr, wc.hInstance, nullptr);
    WinCheck(background.window != nullptr, "Create backdrop");
    ShowWindow(background.window, SW_SHOWNOACTIVATE); UpdateWindow(background.window);

    Overlay overlay(mapping.c_str(), mutex.c_str());
    overlay.Create(80, 80, 320, 240);
    if (!overlay.Render()) throw std::runtime_error("Test renderer failed");
    const auto* red = overlay.Pixel(30, 30);
    const auto* half = overlay.Pixel(130, 30);
    const auto* empty = overlay.Pixel(300, 200);
    if (red[2] < 250 || red[3] < 250 || half[1] < 125 || half[1] > 130 ||
        half[3] < 125 || half[3] > 130 || empty[3] != 0)
        throw std::runtime_error("Premultiplied opaque/half-alpha/transparent pixel test failed");
    puts("PASS: DX11 opaque, half-alpha and transparent BGRA pixels.");
    ShowWindow(overlay.window, SW_SHOWNOACTIVATE);
    if (WindowFromPoint(POINT{110, 110}) != background.window)
        throw std::runtime_error("Layered window mouse pass-through test failed");
    puts("PASS: hit testing reaches the window underneath the opaque overlay pixel.");

    const DWORD modes[] = {WDA_NONE, WDA_EXCLUDEFROMCAPTURE, WDA_NONE, WDA_EXCLUDEFROMCAPTURE};
    for (DWORD mode : modes) {
        Affinity(overlay.window, mode);
        HrCheck(DwmFlush(), "DwmFlush");
        Sleep(150);
        const COLORREF actual = CapturePixel(110, 110);
        const COLORREF expected = mode == WDA_NONE ? RGB(255, 0, 0) : RGB(0, 0, 255);
        printf("GDI CAPTUREBLT affinity=0x%02lX pixel=#%02X%02X%02X\n", mode,
            GetRValue(actual), GetGValue(actual), GetBValue(actual));
        if (actual != expected) throw std::runtime_error("Capture did not match known red-overlay/blue-background pixels");
    }
    puts("PASS: affinity readback and GDI capture exclusion with visible-capture controls.");
    puts("This tests GDI CAPTUREBLT only; validate your recording/sharing software separately.");
    if (!sender.ExeStartDraw()) throw std::runtime_error("Clip test producer busy");
    sender.PushClipRect(20, 20, 40, 40);
    sender.DrawFilledRect(0, 0, 100, 100, ImVec4(1, 0, 0, 1));
    sender.PopClipRect();
    sender.DrawFilledRect(100, 100, 10, 10, ImVec4(0, 0, 1, 1));
    sender.ExeEndDraw();
    if (!overlay.Render() || overlay.Pixel(30, 30)[2] < 250 || overlay.Pixel(10, 10)[3] != 0 || overlay.Pixel(105, 105)[0] < 250)
        throw std::runtime_error("Panel clipping / clip restoration pixel test failed");
    if (!sender.ExeStartDraw()) throw std::runtime_error("Unbalanced clip test producer busy");
    sender.PopClipRect(); // Underflow must be ignored.
    sender.PushClipRect(20, 20, 40, 40); // Missing pop must be balanced at frame end.
    sender.DrawFilledRect(0, 0, 100, 100, ImVec4(1, 0, 0, 1));
    sender.ExeEndDraw();
    if (!overlay.Render()) throw std::runtime_error("Unbalanced clip render failed");
    if (!sender.ExeStartDraw()) throw std::runtime_error("Next-frame clip test producer busy");
    sender.DrawFilledRect(0, 0, 100, 100, ImVec4(0, 0, 1, 1)); sender.ExeEndDraw();
    if (!overlay.Render() || overlay.Pixel(10, 10)[0] < 250)
        throw std::runtime_error("Clip state leaked into the next frame");
    puts("PASS: panel clip pixels, pop restoration, clip underflow and next-frame isolation.");
    return 0;
}

void DemoFrame(CDwmMsg& sender, unsigned frame) {
    if (!sender.ExeStartDraw()) return;
    sender.DrawFilledRect(20, 20, 550, 70, ImVec4(0.04f, 0.07f, 0.12f, 0.85f));
    sender.DrawNewTextStr(40, 36, ImVec4(1, 1, 1, 1), false, "本机可见 / 已设置捕获排除");
    sender.DrawNewTextStr(40, 62, ImVec4(0.6f, 0.8f, 1, 1), false, "Ctrl+Alt+H: show/hide    Ctrl+Alt+Q: quit");
    const float offset = 20 * std::sin(frame * 0.03f);
    sender.DrawRect(100 + offset, 110, 220, 130, ImVec4(0.2f, 1, 0.4f, 1), 2);
    sender.DrawCircle(430, 175, 45, ImVec4(0.4f, 0.7f, 1, 1), 64, 2);
    sender.DrawLine(100, 270, 480, 270, ImVec4(1, 0.8f, 0.2f, 1), 2);
    sender.ExeEndDraw();
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool listen = false, test = false, managed = false;
        DWORD parentId = 0;
        std::wstring session;
        for (int i = 1; i < argc; ++i) {
            if (!strcmp(argv[i], "--listen")) listen = true;
            else if (!strcmp(argv[i], "--self-test")) test = true;
            else if (!strcmp(argv[i], "--managed")) managed = true;
            else if (!strcmp(argv[i], "--session") && i + 1 < argc) {
                const std::string value = argv[++i];
                if (value.size() != 32 || value.find_first_not_of("0123456789abcdef") != std::string::npos)
                    throw std::runtime_error("Invalid session: expected 32 lowercase hex characters");
                session.assign(value.begin(), value.end());
            }
            else if (!strcmp(argv[i], "--parent") && i + 1 < argc) {
                char* end = nullptr;
                const unsigned long long value = std::strtoull(argv[++i], &end, 10);
                if (!*argv[i] || *end || !value || value > MAXDWORD) throw std::runtime_error("Invalid parent PID");
                parentId = static_cast<DWORD>(value);
            }
            else {
                puts("Usage: overlay_python [--listen [--managed --parent PID [--session HEX32]] | --self-test]\nDefault: standalone demo. --managed: Python owns the window lifecycle and no global hotkeys are registered.");
                return !strcmp(argv[i], "--help") ? 0 : 2;
            }
        }
        if (listen && test) throw std::runtime_error("Choose either --listen or --self-test");
        if (managed && (!listen || !parentId)) throw std::runtime_error("--managed requires --listen and --parent PID");
        if (parentId && !managed) throw std::runtime_error("--parent requires --managed");
        if (!session.empty() && !managed) throw std::runtime_error("--session requires --managed");
        ParentProcess parent;
        if (parentId) {
            parent.handle = OpenProcess(SYNCHRONIZE, FALSE, parentId);
            WinCheck(parent.handle != nullptr, "Open Python parent process");
        }
        WinCheck(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2), "SetProcessDpiAwarenessContext");
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = ClassName;
        WinCheck(RegisterClassW(&wc), "RegisterClassW");
        if (test) return SelfTest();

        const auto mapping = !session.empty() ? L"Local\\DwmDraw.Python." + session :
            listen ? std::wstring(MAPNAME) : L"Local\\DwmDraw.OverlayDemo." + std::to_wstring(GetCurrentProcessId());
        const auto mutex = listen && session.empty() ? std::wstring(MUTEXNAME) : mapping + L".Lock";
        CDwmMsg sender(mapping.c_str(), mutex.c_str());
        if (!listen && !sender.InitMsg()) throw std::runtime_error("Demo producer init failed");
        Overlay overlay(mapping.c_str(), mutex.c_str());
        overlay.Create(listen ? 0 : 24, listen ? 0 : 24,
            listen ? GetSystemMetrics(SM_CXSCREEN) : 640, listen ? GetSystemMetrics(SM_CYSCREEN) : 360);
        WinCheck(SetPropW(overlay.window, L"DwmDraw.ProtocolVersion", reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(DRAW_VERSION))), "Set protocol property");
        if (listen) WinCheck(SetPropW(overlay.window, L"DwmDraw.Listen", reinterpret_cast<HANDLE>(1)), "Set listener property");
        if (!managed) {
            WinCheck(RegisterHotKey(overlay.window, QuitKey, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q'), "Register quit hotkey");
            WinCheck(RegisterHotKey(overlay.window, HideKey, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'H'), "Register hide hotkey");
        }
        if (!listen) DemoFrame(sender, 0);
        overlay.Render();
        ShowWindow(overlay.window, SW_SHOWNOACTIVATE);
        puts("Overlay running. WDA_EXCLUDEFROMCAPTURE=0x11 verified. Mouse clicks pass through.");
        puts(managed ? "Window lifecycle is managed by Python." : "Ctrl+Alt+H hides/shows; Ctrl+Alt+Q quits. Capture behavior depends on the capturing application.");
        unsigned frame = 0;
        bool running = true, visible = true;
        while (running) {
            if (parent.handle && WaitForSingleObject(parent.handle, 0) == WAIT_OBJECT_0) break;
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT || (msg.message == WM_HOTKEY && msg.wParam == QuitKey)) running = false;
                if (msg.message == WM_HOTKEY && msg.wParam == HideKey) {
                    visible = !visible;
                    ShowWindow(overlay.window, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
                }
                if (msg.message == WM_DISPLAYCHANGE && listen)
                    throw std::runtime_error("Display configuration changed; restart overlay to update monitor bounds");
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (!running) break;
            if (visible) {
                if (!listen) DemoFrame(sender, frame++);
                overlay.Render();
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 33, QS_ALLINPUT);
        }
        if (!managed) { UnregisterHotKey(overlay.window, HideKey); UnregisterHotKey(overlay.window, QuitKey); }
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "Overlay error: %s\n", error.what());
        return 1;
    }
}
