#include "capture.h"
#include "gpu_color.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <winrt/base.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace lightshot_hdr {
namespace {
using winrt::com_ptr;
using winrt::check_hresult;
using Clock = std::chrono::steady_clock;

struct ThreadDpi {
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~ThreadDpi() { if (previous) SetThreadDpiAwarenessContext(previous); }
};

struct Monitor { HMONITOR handle; RECT bounds; std::wstring deviceName; };

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    auto& monitors = *reinterpret_cast<std::vector<Monitor>*>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info))) return FALSE;
    try { monitors.push_back({monitor, info.rcMonitor, info.szDevice}); }
    catch (...) { return FALSE; }
    return TRUE;
}

float WhiteScaleFor(const Monitor& monitor) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        auto status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (status != ERROR_SUCCESS) winrt::throw_hresult(HRESULT_FROM_WIN32(status));
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                    &modeCount, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) continue;
        if (status != ERROR_SUCCESS) winrt::throw_hresult(HRESULT_FROM_WIN32(status));
        for (UINT32 i = 0; i < pathCount; ++i) {
            const auto& path = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                             path.sourceInfo.adapterId, path.sourceInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                monitor.deviceName != source.viewGdiDeviceName) continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white),
                            path.targetInfo.adapterId, path.targetInfo.id};
            status = DisplayConfigGetDeviceInfo(&white.header);
            if (status != ERROR_SUCCESS) winrt::throw_hresult(HRESULT_FROM_WIN32(status));
            if (!white.SDRWhiteLevel) throw std::runtime_error("HDR SDR white level is zero");
            return static_cast<float>(white.SDRWhiteLevel) / 1000.0f;
        }
        throw std::runtime_error("HDR monitor has no matching active display path");
    }
    throw std::runtime_error("Display topology changed during white-level query");
}

struct OutputCapture {
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<IDXGIOutput6> output;
    com_ptr<IDXGIOutputDuplication> duplication;
    com_ptr<ID3D11Texture2D> staging;
    std::unique_ptr<GpuToneMapper> mapper;
    Monitor monitor;
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN;
    float white = 0;
    bool hdr = false;
    bool hasFrame = false;
};

std::unique_ptr<OutputCapture> CreateOutput(const Monitor& monitor) {
    com_ptr<IDXGIFactory1> factory;
    check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()));
    for (UINT index = 0;; ++index) {
        com_ptr<IDXGIAdapter1> adapter;
        const auto found = factory->EnumAdapters1(index, adapter.put());
        if (found == DXGI_ERROR_NOT_FOUND) break;
        check_hresult(found);
        for (UINT outputIndex = 0;; ++outputIndex) {
            com_ptr<IDXGIOutput> output;
            const auto foundOutput = adapter->EnumOutputs(outputIndex, output.put());
            if (foundOutput == DXGI_ERROR_NOT_FOUND) break;
            check_hresult(foundOutput);
            DXGI_OUTPUT_DESC description{};
            check_hresult(output->GetDesc(&description));
            if (description.Monitor != monitor.handle) continue;
            auto result = std::make_unique<OutputCapture>();
            result->monitor = monitor;
            result->output = output.as<IDXGIOutput6>();
            result->rotation = description.Rotation;
            check_hresult(D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                result->device.put(), nullptr, result->context.put()));
            constexpr DXGI_FORMAT formats[]{DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
            check_hresult(result->output->DuplicateOutput1(result->device.get(), 0,
                ARRAYSIZE(formats), formats, result->duplication.put()));
            return result;
        }
    }
    throw std::runtime_error("No D3D11 adapter matches the monitor");
}

// Only called under dll.cpp's captureMutex. D3D devices are free threaded;
// immediate contexts never overlap. The pinned DLL retains this bounded cache
// for process lifetime, avoiding driver/COM destruction inside DllMain.
using OutputCache = std::unordered_map<HMONITOR, std::unique_ptr<OutputCapture>>;
OutputCache& Cache() { static auto* cache = new OutputCache; return *cache; }

struct AcquiredFrame {
    IDXGIOutputDuplication* duplication;
    bool owned = true;
    ~AcquiredFrame() { if (owned) duplication->ReleaseFrame(); }
    void Release() { check_hresult(duplication->ReleaseFrame()); owned = false; }
};

struct Mapping {
    ID3D11DeviceContext* context;
    ID3D11Texture2D* texture;
    ~Mapping() { context->Unmap(texture, 0); }
};

void RefreshFrame(OutputCapture& capture, float white, bool hdr) {
    DXGI_OUTDUPL_FRAME_INFO info{};
    com_ptr<IDXGIResource> resource;
    auto status = capture.duplication->AcquireNextFrame(capture.hasFrame ? 0 : 250,
                                                       &info, resource.put());
    if (status == DXGI_ERROR_WAIT_TIMEOUT && capture.hasFrame) return;
    check_hresult(status);
    // The first notification can contain only cursor metadata and an empty
    // desktop surface (LastPresentTime == 0). Never cache that as an image.
    const auto deadline = Clock::now() + std::chrono::milliseconds(250);
    while (!info.LastPresentTime.QuadPart) {
        check_hresult(capture.duplication->ReleaseFrame());
        if (capture.hasFrame) return;
        if (Clock::now() >= deadline) throw std::runtime_error("No desktop image after initial pointer notification");
        resource = nullptr;
        status = capture.duplication->AcquireNextFrame(16, &info, resource.put());
        while (status == DXGI_ERROR_WAIT_TIMEOUT && Clock::now() < deadline)
            status = capture.duplication->AcquireNextFrame(16, &info, resource.put());
        check_hresult(status);
    }
    AcquiredFrame acquired{capture.duplication.get()};
    auto source = resource.as<ID3D11Texture2D>();
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    capture.sourceFormat = description.Format;
    if (hdr && description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        throw std::runtime_error("HDR duplication did not provide FP16; refusing clipped conversion");
    ID3D11Texture2D* converted = source.get();
    if (description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        if (!capture.mapper) capture.mapper = std::make_unique<GpuToneMapper>(capture.device.get());
        converted = capture.mapper->Convert(capture.context.get(), source.get(), hdr, white);
    } else if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        throw std::runtime_error("Unsupported desktop duplication format");
    }
    converted->GetDesc(&description);
    D3D11_TEXTURE2D_DESC previous{};
    if (capture.staging) capture.staging->GetDesc(&previous);
    if (!capture.staging || previous.Width != description.Width || previous.Height != description.Height) {
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.MiscFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        capture.staging = nullptr;
        check_hresult(capture.device->CreateTexture2D(&description, nullptr, capture.staging.put()));
    }
    capture.context->CopyResource(capture.staging.get(), converted);
    // Finish GPU reads of the OS surface before relinquishing its frame.
    D3D11_MAPPED_SUBRESOURCE completed{};
    check_hresult(capture.context->Map(capture.staging.get(), 0, D3D11_MAP_READ, 0, &completed));
    capture.context->Unmap(capture.staging.get(), 0);
    acquired.Release();
    capture.hasFrame = true;
}

std::string CaptureMonitor(const Monitor& monitor, RECT intersection, RECT area, DesktopFrame& desktop) {
    const auto started = Clock::now();
    auto& slot = Cache()[monitor.handle];
    if (slot && (slot->monitor.deviceName != monitor.deviceName ||
        !EqualRect(&slot->monitor.bounds, &monitor.bounds))) slot.reset();
    if (!slot) slot = CreateOutput(monitor);
    auto& capture = *slot;
    DXGI_OUTPUT_DESC1 color{};
    check_hresult(capture.output->GetDesc1(&color));
    const bool hdr = color.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    const float white = hdr ? WhiteScaleFor(monitor) : 1.0f;
    if (capture.hasFrame && (capture.white != white || capture.hdr != hdr)) {
        // A static desktop has no next frame. Recreate on white/HDR changes so
        // a cached SDR bitmap never survives a changed color configuration.
        slot.reset();
        slot = CreateOutput(monitor);
    }
    auto& current = *slot;
    current.white = white;
    current.hdr = hdr;
    RefreshFrame(current, white, hdr);
    D3D11_TEXTURE2D_DESC dimensions{};
    current.staging->GetDesc(&dimensions);
    const int width = monitor.bounds.right - monitor.bounds.left;
    const int height = monitor.bounds.bottom - monitor.bounds.top;
    const bool rotated = current.rotation == DXGI_MODE_ROTATION_ROTATE90 ||
                         current.rotation == DXGI_MODE_ROTATION_ROTATE270;
    if (dimensions.Width != static_cast<UINT>(rotated ? height : width) ||
        dimensions.Height != static_cast<UINT>(rotated ? width : height))
        throw std::runtime_error("Monitor size changed or duplication coordinates disagree");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check_hresult(current.context->Map(current.staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
    Mapping unmap{current.context.get(), current.staging.get()};
    for (int y = intersection.top; y < intersection.bottom; ++y) {
        auto* destination = desktop.pixels.data() + static_cast<std::size_t>(y - area.top) * desktop.width
                            + intersection.left - area.left;
        for (int x = intersection.left; x < intersection.right; ++x) {
            int sx = x - monitor.bounds.left, sy = y - monitor.bounds.top;
            const int dx = sx, dy = sy;
            switch (current.rotation) {
            case DXGI_MODE_ROTATION_ROTATE90: sx = dy; sy = width - 1 - dx; break;
            case DXGI_MODE_ROTATION_ROTATE180: sx = width - 1 - dx; sy = height - 1 - dy; break;
            case DXGI_MODE_ROTATION_ROTATE270: sx = height - 1 - dy; sy = dx; break;
            default: break;
            }
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                              static_cast<std::size_t>(sy) * mapped.RowPitch;
            *destination = reinterpret_cast<const Bgra8*>(row)[sx];
            destination->a = 255;
            ++destination;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << "{\"bounds\":[" << monitor.bounds.left << ',' << monitor.bounds.top << ','
           << monitor.bounds.right << ',' << monitor.bounds.bottom << "],\"hdr\":"
           << (hdr ? "true" : "false") << ",\"white_scale\":" << white
           << ",\"format\":\"" << (current.sourceFormat == DXGI_FORMAT_R16G16B16A16_FLOAT
                ? "R16G16B16A16_FLOAT" : "B8G8R8A8_UNORM")
           << "\",\"gpu_tone_mapping\":" << (current.mapper ? "true" : "false")
           << ",\"capture_us\":" << elapsed << '}';
    return report.str();
}
}

DesktopFrame CaptureDesktop(RECT area) {
    ThreadDpi dpi;
    std::vector<Monitor> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors)))
        throw std::runtime_error("Monitor enumeration failed");
    // Drop disconnected outputs before opening replacements. No background
    // polling, screenshots or image files are produced while the app is idle.
    auto& cache = Cache();
    for (auto it = cache.begin(); it != cache.end();) {
        if (std::none_of(monitors.begin(), monitors.end(), [&](const Monitor& m) { return m.handle == it->first; }))
            it = cache.erase(it);
        else ++it;
    }
    DesktopFrame result;
    result.width = area.right - area.left;
    result.height = area.bottom - area.top;
    result.pixels.assign(static_cast<std::size_t>(result.width) * result.height, {0, 0, 0, 255});
    std::ostringstream report;
    report << "{\"version\":2,\"success\":true,\"backend\":\"DXGI-DuplicateOutput1\",\"borderless\":true,\"monitors\":[";
    unsigned captured = 0;
    try {
        for (const auto& monitor : monitors) {
            RECT intersection{};
            if (!IntersectRect(&intersection, &area, &monitor.bounds)) continue;
            if (captured >= 8) throw std::runtime_error("Capture exceeds eight monitor safety limit");
            if (captured++) report << ',';
            report << CaptureMonitor(monitor, intersection, area, result);
        }
        if (!captured) throw std::runtime_error("Requested rectangle intersects no monitor");
    } catch (...) {
        // Includes ACCESS_LOST/device reset/fullscreen transitions. The next
        // request reconstructs the backend; the caller retains its GDI buffer.
        cache.clear();
        throw;
    }
    report << "]}";
    result.report = report.str();
    return result;
}
}
