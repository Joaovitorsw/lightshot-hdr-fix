#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>

using Microsoft::WRL::ComPtr;
using Capture = bool(__stdcall*)(HDC, RECT);
using Report = unsigned(__stdcall*)(char*, unsigned);
constexpr int width = 640, height = 160;

void Check(HRESULT result) { if (FAILED(result)) throw result; }

struct Image {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    std::uint32_t* pixels = nullptr;
    Image() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
            reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!dc || !bitmap) throw E_OUTOFMEMORY;
        old = SelectObject(dc, bitmap);
    }
    ~Image() { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); }
    int sample(int patch) const { return pixels[height / 2 * width + patch * (width / 4) + width / 8] & 255; }
    void save(const std::wstring& path) const {
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + width * height * 4;
        info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
        info.biPlanes = 1; info.biBitCount = 32;
        HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) throw HRESULT_FROM_WIN32(GetLastError());
        DWORD written;
        const bool success = WriteFile(output, &file, sizeof(file), &written, nullptr) &&
            WriteFile(output, &info, sizeof(info), &written, nullptr) &&
            WriteFile(output, pixels, width * height * 4, &written, nullptr);
        CloseHandle(output);
        if (!success) throw E_FAIL;
    }
};

struct Window {
    HWND handle = nullptr;
    ~Window() { if (handle) DestroyWindow(handle); }
};

int wmain(int argc, wchar_t** argv) {
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr, "usage: hdr_calibration.exe absolute-DLL-path SDR-white-scale output-prefix [--sdr]\n");
        return 2;
    }
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Window window;
    HMODULE module = nullptr;
    try {
        const float white = static_cast<float>(_wtof(argv[2]));
        if (!(white > 0 && white < 100)) throw E_INVALIDARG;
        const bool sdr = argc == 5 && std::wstring(argv[4]) == L"--sdr";
        POINT position{100, 100};
        if (sdr) {
            RECT secondary{};
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM value) -> BOOL {
                MONITORINFO info{}; info.cbSize = sizeof(info);
                if (GetMonitorInfoW(handle, &info) && !(info.dwFlags & MONITORINFOF_PRIMARY))
                    *reinterpret_cast<RECT*>(value) = info.rcMonitor;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&secondary));
            if (secondary.right == secondary.left) throw E_INVALIDARG;
            position = {secondary.left + 100, secondary.top + 100};
        }
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = instance;
        cls.lpszClassName = L"LightshotHdrCalibration";
        if (!RegisterClassW(&cls)) throw HRESULT_FROM_WIN32(GetLastError());
        window.handle = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            cls.lpszClassName, L"Lightshot HDR calibration", WS_POPUP,
            position.x, position.y, width, height, nullptr, nullptr, instance, nullptr);
        if (!window.handle) throw HRESULT_FROM_WIN32(GetLastError());
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &device, nullptr, &context));
        ComPtr<IDXGIDevice> dxgi;
        Check(device.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter;
        Check(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIFactory2> factory;
        Check(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width; description.Height = height;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> chain;
        Check(factory->CreateSwapChainForHwnd(device.Get(), window.handle, &description,
            nullptr, nullptr, &chain));
        ComPtr<IDXGISwapChain3> colorChain;
        Check(chain.As(&colorChain));
        Check(colorChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709));
        const std::array<float, 4> levels{0.18f, 1.0f, 2.0f, 4.0f};
        std::vector<std::uint16_t> content(width * height * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto value = DirectX::PackedVector::XMConvertFloatToHalf(levels[x / (width / 4)] * white);
                const auto offset = (y * width + x) * 4;
                content[offset] = value; content[offset + 1] = value;
                content[offset + 2] = value; content[offset + 3] = 0x3c00;
            }
        }
        ComPtr<ID3D11Texture2D> buffer;
        Check(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)));
        context->UpdateSubresource(buffer.Get(), 0, nullptr, content.data(), width * 8, 0);
        SetWindowPos(window.handle, HWND_TOPMOST, position.x, position.y, width, height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(window.handle, SW_SHOWNOACTIVATE);
        ShowWindow(window.handle, SW_SHOWNOACTIVATE);
        SetWindowPos(window.handle, HWND_TOPMOST, position.x, position.y, width, height, SWP_NOACTIVATE);
        Check(chain->Present(1, 0));
        for (int i = 0; i < 40; ++i) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            Sleep(10);
        }
        Image original, corrected;
        RECT visible{};
        GetWindowRect(window.handle, &visible);
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window.handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        const auto hit = WindowFromPoint(POINT{position.x + 80, position.y + 80});
        std::printf("test_window_visible=%d bounds=[%ld,%ld,%ld,%ld] top_at_sample=%d\n",
            IsWindowVisible(window.handle), visible.left, visible.top, visible.right, visible.bottom,
            GetAncestor(hit, GA_ROOT) == window.handle);
        std::printf("cloaked=%lu enabled=%d window=%p hit=%p root=%p style=%lx exstyle=%lx\n", cloaked,
            IsWindowEnabled(window.handle), window.handle, hit, GetAncestor(hit, GA_ROOT),
            GetWindowLongW(window.handle, GWL_STYLE), GetWindowLongW(window.handle, GWL_EXSTYLE));
        if (!IsWindowVisible(window.handle) || GetAncestor(hit, GA_ROOT) != window.handle)
            throw HRESULT_FROM_WIN32(ERROR_NOT_READY);
        HDC screen = GetDC(nullptr);
        const BOOL blit = BitBlt(original.dc, 0, 0, width, height, screen, position.x, position.y, SRCCOPY | CAPTUREBLT);
        ReleaseDC(nullptr, screen);
        if (!blit) throw E_FAIL;
        module = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) throw HRESULT_FROM_WIN32(GetLastError());
        const auto capture = reinterpret_cast<Capture>(GetProcAddress(module, "TakeScreenshotExp"));
        const auto report = reinterpret_cast<Report>(GetProcAddress(module, "GetLastCaptureReport"));
        if (!capture || !capture(corrected.dc, RECT{position.x, position.y, position.x + width, position.y + height})) {
            if (report) { char text[4096]{}; report(text, sizeof(text)); std::puts(text); }
            throw E_FAIL;
        }
        GdiFlush();
        std::printf("gdi_samples=[%d,%d,%d,%d]\n", original.sample(0), original.sample(1), original.sample(2), original.sample(3));
        std::printf("corrected_samples=[%d,%d,%d,%d]\n", corrected.sample(0), corrected.sample(1), corrected.sample(2), corrected.sample(3));
        if (report) { char text[4096]{}; report(text, sizeof(text)); std::puts(text); }
        original.save(std::wstring(argv[3]) + L"-gdi.bmp");
        corrected.save(std::wstring(argv[3]) + L"-corrected.bmp");
        const bool distinct = corrected.sample(1) < corrected.sample(2) && corrected.sample(2) < corrected.sample(3);
        bool matches = std::abs(corrected.sample(0) - 118) <= 2;
        if (sdr) {
            for (int i = 0; i < 4; ++i) matches = matches && std::abs(corrected.sample(i) - original.sample(i)) <= 2;
        } else matches = matches && distinct && std::abs(corrected.sample(1) - 240) <= 2;
        std::printf("hdr_highlights_distinct=%d sdr=%d reference_matches=%d\n", distinct, sdr, matches);
        const int before0 = corrected.sample(0), before1 = corrected.sample(1);
        const RECT captureArea{position.x, position.y, position.x + width, position.y + height};
        if (!capture(corrected.dc, captureArea)) throw E_FAIL;
        GdiFlush();
        const bool unchanged = corrected.sample(0) == before0 && corrected.sample(1) == before1;
        // Change the desktop after populating the cache. Reusing an older frame
        // would incorrectly retain the former first two gray samples.
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width / 4; ++x) {
                for (int channel = 0; channel < 4; ++channel)
                    std::swap(content[(y * width + x) * 4 + channel],
                              content[(y * width + x + width / 4) * 4 + channel]);
            }
        }
        buffer.Reset();
        Check(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)));
        context->UpdateSubresource(buffer.Get(), 0, nullptr, content.data(), width * 8, 0);
        Check(chain->Present(1, 0));
        DwmFlush();
        Sleep(40);
        if (!capture(corrected.dc, captureArea)) throw E_FAIL;
        GdiFlush();
        const bool fresh = std::abs(corrected.sample(0) - before1) <= 1 &&
                           std::abs(corrected.sample(1) - before0) <= 1;
        std::printf("cached_frame_unchanged=%d next_present_observed=%d\n", unchanged, fresh);
        FreeLibrary(module); module = nullptr;
        return matches && unchanged && fresh ? 0 : 1;
    } catch (HRESULT result) {
        if (module) FreeLibrary(module);
        std::fprintf(stderr, "Calibration failed HRESULT=%08lx\n", static_cast<unsigned long>(result));
        return 1;
    }
}
