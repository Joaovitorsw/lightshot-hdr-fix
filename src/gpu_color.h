#pragma once
#include <d3d11.h>
#include <memory>

namespace lightshot_hdr {
// Own one mapper per device/worker. Convert changes the context's graphics state;
// callers must serialize access and use a context dedicated to capture processing.
class GpuToneMapper {
public:
    explicit GpuToneMapper(ID3D11Device* device);
    ~GpuToneMapper();
    GpuToneMapper(const GpuToneMapper&) = delete;
    GpuToneMapper& operator=(const GpuToneMapper&) = delete;
    // Input: one mip, one slice, single-sample R16G16B16A16_FLOAT, same device.
    // Output is borrowed, opaque BGRA8 UNORM; valid until resize/destruction.
    // Throws std::exception on invalid input, shader compilation or device errors.
    ID3D11Texture2D* Convert(ID3D11DeviceContext* context,
        ID3D11Texture2D* input, bool hdr, float white);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
