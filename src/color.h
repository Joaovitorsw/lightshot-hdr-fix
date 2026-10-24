#pragma once
#include <cstdint>

namespace lightshot_hdr {
struct FloatRgb { float r, g, b; };
struct Bgra8 { std::uint8_t b, g, r, a; };
float HalfToFloat(std::uint16_t bits) noexcept;
// Linear scRGB in, opaque sRGB BGRA8 out. HDR uses per-output SDR white scale.
Bgra8 ConvertPixel(FloatRgb linearScRgb, float sdrWhiteScale, bool hdr) noexcept;
}
