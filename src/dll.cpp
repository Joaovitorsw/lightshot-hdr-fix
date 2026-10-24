#include "capture.h"
#include <objbase.h>
#include <winrt/base.h>
#include <cstring>
#include <array>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>

namespace {
std::mutex captureMutex;
CO_MTA_USAGE_COOKIE processMta = nullptr;
std::string lastReport = "{\"version\":1,\"success\":false,\"error\":\"No capture attempted\"}";

void KeepCaptureRuntimeAlive() {
    if (processMta) return;
    CO_MTA_USAGE_COOKIE cookie = nullptr;
    winrt::check_hresult(CoIncrementMTAUsage(&cookie));
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&processMta), &self)) {
        const DWORD error = GetLastError();
        CoDecrementMTAUsage(cookie);
        winrt::throw_hresult(HRESULT_FROM_WIN32(error));
    }
    // Retain the capture runtime and the module owning cached D3D resources
    // until process exit; never perform COM teardown inside DllMain.
    processMta = cookie;
}

std::string ErrorReport(const std::string& message) {
    std::string escaped;
    for (const unsigned char c : message) {
        if (c == '"' || c == '\\') escaped += '\\';
        if (c >= 32) escaped += static_cast<char>(c);
    }
    return "{\"version\":1,\"success\":false,\"error\":\"" + escaped + "\"}";
}
}

extern "C" bool __stdcall TakeScreenshotExp(HDC destination, RECT area) noexcept {
    std::unique_lock<std::mutex> lock(captureMutex, std::defer_lock);
    try {
        lock.lock();
        const auto width = static_cast<long long>(area.right) - area.left;
        const auto height = static_cast<long long>(area.bottom) - area.top;
        if (!destination || GetObjectType(destination) != OBJ_MEMDC || width <= 0 || height <= 0 ||
            width > 16384 || height > 16384 || width * height > 32000000) {
            lastReport = ErrorReport("Invalid destination or capture bounds");
            return false;
        }
        BITMAP bitmap{};
        const auto target = GetCurrentObject(destination, OBJ_BITMAP);
        if (!target || !GetObjectW(target, sizeof(bitmap), &bitmap) ||
            bitmap.bmWidth < width || bitmap.bmHeight < height) {
            lastReport = ErrorReport("Destination bitmap is smaller than the requested capture");
            return false;
        }
        KeepCaptureRuntimeAlive();
        lightshot_hdr::DesktopFrame frame;
        std::array<char, 512> error{};
        std::thread worker([&] {
            bool initialized = false;
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                initialized = true;
                frame = lightshot_hdr::CaptureDesktop(area);
            } catch (const winrt::hresult_error& failure) {
                try {
                    const auto message = winrt::to_string(failure.message());
                    std::snprintf(error.data(), error.size(), "%s", message.c_str());
                } catch (...) { std::strcpy(error.data(), "WinRT capture failure"); }
            } catch (const std::exception& failure) {
                std::snprintf(error.data(), error.size(), "%s", failure.what());
            } catch (...) { std::strcpy(error.data(), "Unknown worker failure"); }
            if (initialized) {
                winrt::clear_factory_cache();
                winrt::uninit_apartment();
            }
        });
        worker.join();
        if (error[0]) {
            lastReport = ErrorReport(error.data());
            return false;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frame.width;
        info.bmiHeader.biHeight = -frame.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        const int written = SetDIBitsToDevice(destination, 0, 0, frame.width, frame.height,
            0, 0, 0, frame.height, frame.pixels.data(), &info, DIB_RGB_COLORS);
        if (written != frame.height) {
            lastReport = ErrorReport("Failed to transfer SDR bitmap to the caller");
            return false;
        }
        lastReport = std::move(frame.report);
        return true;
    } catch (const winrt::hresult_error& error) {
        try { if (lock.owns_lock()) lastReport = ErrorReport(winrt::to_string(error.message())); } catch (...) {}
    } catch (const std::exception& error) {
        try { if (lock.owns_lock()) lastReport = ErrorReport(error.what()); } catch (...) {}
    } catch (...) {
        try { if (lock.owns_lock()) lastReport = ErrorReport("Unknown capture failure"); } catch (...) {}
    }
    return false;
}

extern "C" unsigned __stdcall GetLastCaptureReport(char* buffer, unsigned capacity) noexcept {
    try {
        std::lock_guard<std::mutex> lock(captureMutex);
        const auto length = static_cast<unsigned>(lastReport.size());
        if (buffer && capacity) {
            const auto copied = (length < capacity - 1) ? length : capacity - 1;
            std::memcpy(buffer, lastReport.data(), copied);
            buffer[copied] = '\0';
        }
        return length;
    } catch (...) { if (buffer && capacity) *buffer = '\0'; return 0; }
}
