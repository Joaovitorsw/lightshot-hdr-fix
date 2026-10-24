#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "crash_diagnostics.h"

using Capture = bool(__stdcall*)(HDC, RECT);
using Report = unsigned(__stdcall*)(char*, unsigned);

struct Bitmap {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    std::uint32_t* pixels = nullptr;
    int width, height;
    Bitmap(int w, int h) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                                 reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (dc && bitmap) previous = SelectObject(dc, bitmap);
    }
    ~Bitmap() {
        if (previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    void save(const std::wstring& filename) const {
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + width * height * 4;
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = -height;
        info.biPlanes = 1;
        info.biBitCount = 32;
        HANDLE output = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) throw "Cannot write BMP";
        DWORD written;
        const bool ok = WriteFile(output, &file, sizeof(file), &written, nullptr) &&
            WriteFile(output, &info, sizeof(info), &written, nullptr) &&
            WriteFile(output, pixels, width * height * 4, &written, nullptr);
        CloseHandle(output);
        if (!ok) throw "BMP write failed";
    }
};

int wmain(int argc, wchar_t** argv) {
    SetUnhandledExceptionFilter(CrashDiagnostic);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring dll, save;
    bool all = false, invalidOnly = false, recovery = false;
    int repeats = 1;
    int maximumMs = 0;
    int warmMaximumMs = 0;
    bool requireBorderless = false;
    RECT rect{};
    bool customRect = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--dll" && i + 1 < argc) dll = argv[++i];
        else if (arg == L"--save" && i + 1 < argc) save = argv[++i];
        else if (arg == L"--all") all = true;
        else if (arg == L"--invalid-only") invalidOnly = true;
        else if (arg == L"--failure-recovery") recovery = true;
        else if (arg == L"--repeat" && i + 1 < argc) repeats = _wtoi(argv[++i]);
        else if (arg == L"--max-ms" && i + 1 < argc) maximumMs = _wtoi(argv[++i]);
        else if (arg == L"--warm-max-ms" && i + 1 < argc) warmMaximumMs = _wtoi(argv[++i]);
        else if (arg == L"--require-borderless") requireBorderless = true;
        else if (arg == L"--rect" && i + 4 < argc) {
            rect.left = _wtoi(argv[++i]); rect.top = _wtoi(argv[++i]);
            rect.right = rect.left + _wtoi(argv[++i]);
            rect.bottom = rect.top + _wtoi(argv[++i]);
            customRect = true;
        } else { std::fprintf(stderr, "Invalid arguments\n"); return 2; }
    }
    if (dll.empty() || repeats < 1 || repeats > 50) return 2;
    HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { std::fprintf(stderr, "LoadLibrary failed %lu\n", GetLastError()); return 3; }
    const auto capture = reinterpret_cast<Capture>(GetProcAddress(module, "TakeScreenshotExp"));
    const auto report = reinterpret_cast<Report>(GetProcAddress(module, "GetLastCaptureReport"));
    if (!capture) return 4;
    if (recovery) {
        Bitmap bmp(32, 32);
        std::fill_n(bmp.pixels, 1024, 0x00123456u);
        const bool failed = !capture(bmp.dc, RECT{100000, 100000, 100032, 100032});
        const bool untouched = std::all_of(bmp.pixels, bmp.pixels + 1024, [](auto p) { return p == 0x00123456u; });
        const bool recovered = capture(bmp.dc, RECT{0, 0, 32, 32});
        std::printf("failure_reported=%d destination_preserved=%d next_call_recovered=%d\n", failed, untouched, recovered);
        FreeLibrary(module);
        return failed && untouched && recovered ? 0 : 1;
    }
    if (invalidOnly) {
        Bitmap bmp(32, 32);
        std::fill_n(bmp.pixels, 32 * 32, 0x00123456u);
        const RECT good{0, 0, 32, 32};
        int failures = 0;
        if (capture(nullptr, good)) ++failures;
        if (capture(bmp.dc, RECT{0, 0, 0, 32})) ++failures;
        if (capture(bmp.dc, RECT{0, 0, 100000, 100000})) ++failures;
        if (!std::all_of(bmp.pixels, bmp.pixels + 1024,
                         [](auto p) { return p == 0x00123456u; })) ++failures;
        std::printf("invalid_argument_failures=%d\n", failures);
        FreeLibrary(module);
        return failures ? 1 : 0;
    }
    if (!customRect) {
        if (all) rect = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        else {
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
            rect = monitor.rcMonitor;
        }
    }
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    if (width <= 0 || height <= 0 || static_cast<long long>(width) * height > 32000000) return 2;
    Bitmap bmp(width, height);
    if (!bmp.bitmap || !bmp.dc) return 5;
    const DWORD initialGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    DWORD firstGdi = 0;
    int failures = 0;
    for (int iteration = 0; iteration < repeats; ++iteration) {
        std::fill_n(bmp.pixels, width * height, 0x00123456u);
        const auto start = GetTickCount64();
        if (!capture(bmp.dc, rect)) ++failures;
        const auto elapsed = GetTickCount64() - start;
        std::printf("iteration=%d success=%d elapsed_ms=%llu\n", iteration,
                    failures == 0, elapsed);
        if (maximumMs > 0 && elapsed > static_cast<ULONGLONG>(maximumMs)) {
            std::printf("latency_budget_exceeded=%d\n", maximumMs);
            ++failures;
        }
        if (iteration > 0 && warmMaximumMs > 0 && elapsed > static_cast<ULONGLONG>(warmMaximumMs)) {
            std::printf("warm_latency_budget_exceeded=%d\n", warmMaximumMs);
            ++failures;
        }
        if (report) {
            std::vector<char> text(report(nullptr, 0) + 1);
            report(text.data(), static_cast<unsigned>(text.size()));
            std::printf("%s\n", text.data());
            if (requireBorderless && !std::strstr(text.data(), "\"borderless\":true")) {
                std::printf("borderless_backend_missing=1\n");
                ++failures;
            }
        }
        if (!iteration) firstGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    }
    GdiFlush();
    std::size_t unchanged = 0, white = 0;
    for (int i = 0; i < width * height; ++i) {
        const auto rgb = bmp.pixels[i] & 0x00ffffff;
        if (rgb == 0x00123456) ++unchanged;
        if (rgb == 0x00ffffff) ++white;
    }
    if (unchanged == static_cast<std::size_t>(width) * height) ++failures;
    const DWORD finalGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const long growth = static_cast<long>(finalGdi) - static_cast<long>(firstGdi);
    if (repeats > 1 && growth > 2) ++failures;
    std::printf("size=%dx%d unchanged_pixels=%zu white_pixels=%zu gdi_initial=%lu gdi_final=%lu gdi_growth_after_first=%ld failures=%d\n",
                width, height, unchanged, white, initialGdi, finalGdi, growth, failures);
    if (!save.empty()) bmp.save(save);
    FreeLibrary(module);
    return failures ? 1 : 0;
}
