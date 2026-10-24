#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>
#include "crash_diagnostics.h"

using NativeCapture = void(__fastcall*)(void*);
using Report = unsigned(__stdcall*)(char*, unsigned);

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) return 2;
    const int repeats = argc == 3 ? _wtoi(argv[2]) : 2;
    if (repeats < 1 || repeats > 20) return 2;
    SetUnhandledExceptionFilter(CrashDiagnostic);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE core = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!core) { std::printf("load_core_error=%lu\n", GetLastError()); return 3; }
    const auto* bytes = reinterpret_cast<const unsigned char*>(core);
    const unsigned char nops[5]{0x90, 0x90, 0x90, 0x90, 0x90};
    if (std::memcmp(bytes + 0x1121b, nops, 2) || std::memcmp(bytes + 0x11258, nops, 5)) {
        std::fprintf(stderr, "Core is not the guarded patched version\n"); return 4;
    }
    const auto alternate = *reinterpret_cast<FARPROC const*>(bytes + 0x3a4e4);
    HMODULE replacement = nullptr;
    if (!alternate || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(alternate), &replacement)) {
        std::fprintf(stderr, "Copied Lightshot core did not bind alternate capture\n"); return 5;
    }
    wchar_t filename[MAX_PATH]{};
    GetModuleFileNameW(replacement, filename, MAX_PATH);
    std::wprintf(L"loaded_capture_dll=%ls\n", filename);
    const auto report = reinterpret_cast<Report>(GetProcAddress(replacement, "GetLastCaptureReport"));
    if (!report) return 6;
    const auto capture = reinterpret_cast<NativeCapture>(const_cast<unsigned char*>(bytes) + 0x11090);
    auto* object = const_cast<unsigned char*>(bytes) + 0x3a4c8;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const auto started = GetTickCount64();
        capture(object);
        std::printf("host_capture_ms=%llu\n", GetTickCount64() - started);
        std::vector<char> text(report(nullptr, 0) + 1);
        report(text.data(), static_cast<unsigned>(text.size()));
        std::printf("host_iteration=%d %s\n", repeat, text.data());
        if (!std::strstr(text.data(), "\"success\":true")) return 7;
    }
    BITMAP bitmap{};
    const auto image = *reinterpret_cast<HBITMAP*>(object);
    if (!GetObjectW(image, sizeof(bitmap), &bitmap) || bitmap.bmWidth != GetSystemMetrics(SM_CXVIRTUALSCREEN))
        return 8;
    std::printf("patched_host_bitmap=%ldx%ld bits=%u host_capture_passed=1\n",
                bitmap.bmWidth, bitmap.bmHeight, bitmap.bmBitsPixel);
    FreeLibrary(core);
    return 0;
}
