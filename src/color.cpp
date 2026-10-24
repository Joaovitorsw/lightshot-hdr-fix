#include "color.h"
#include <cmath>
#include <limits>
#include <array>
#include <cstring>

namespace lightshot_hdr {
namespace {
double FiniteChannel(float value) noexcept {
    if (std::isnan(value)) return 0.0;
    if (std::isinf(value)) {
        return std::copysign(static_cast<double>(std::numeric_limits<float>::max()), value);
    }
    return value;
}

std::uint8_t EncodeSrgb(double linear) noexcept {
    if (linear <= 0) return 0;
    if (linear >= 1) return 255;
    // 32KiB table initialized once, with exact transfer samples. Interpolation
    // preserves monotonicity; dense tests enforce at most one quantized byte error.
    static const auto table = [] {
        std::array<double,4097> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double x = static_cast<double>(i) / 4096.0;
            values[i] = 255.0 * (x <= .0031308 ? 12.92*x : 1.055*std::pow(x,1.0/2.4)-.055);
        }
        return values;
    }();
    const double position = linear * 4096.0;
    const auto index = static_cast<std::size_t>(position);
    const double encoded = table[index] + (table[index+1]-table[index])*(position-static_cast<double>(index));
    return static_cast<std::uint8_t>(encoded + .5);
}
}

float HalfToFloat(std::uint16_t bits) noexcept {
    const std::uint32_t sign = static_cast<std::uint32_t>(bits & 0x8000) << 16;
    const int exponent = (bits >> 10) & 31;
    const int fraction = bits & 1023;
    float magnitude;
    if (exponent == 31) {
        magnitude = fraction ? std::numeric_limits<float>::quiet_NaN()
                             : std::numeric_limits<float>::infinity();
    } else if (exponent == 0) {
        magnitude = std::ldexp(static_cast<float>(fraction), -24);
    } else {
        const std::uint32_t expanded = sign | (static_cast<std::uint32_t>(exponent+112) << 23)
            | (static_cast<std::uint32_t>(fraction) << 13);
        std::memcpy(&magnitude,&expanded,sizeof(magnitude));
        return magnitude;
    }
    return bits & 0x8000 ? -magnitude : magnitude;
}

Bgra8 ConvertPixel(FloatRgb input, float sdrWhiteScale, bool hdr) noexcept {
    const double white = hdr && std::isfinite(sdrWhiteScale) && sdrWhiteScale > 0
        ? sdrWhiteScale : 1.0;
    // Double intermediates keep float extremes / a subnormal white scale finite.
    double r = FiniteChannel(input.r) / white;
    double g = FiniteChannel(input.g) / white;
    double b = FiniteChannel(input.b) / white;
    const double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    if (hdr && luminance > 0.75) {
        const double excess = luminance - 0.75;
        const double mapped = 0.75 + 0.25 * excess / (excess + 0.25);
        const double gain = mapped / luminance;
        r *= gain;
        g *= gain;
        b *= gain;
    }
    // A shared luminance gain preserves hue until the final sRGB gamut clip.
    // SDR white becomes ~240 in HDR: highlight detail trades off peak white.
    return {EncodeSrgb(b), EncodeSrgb(g), EncodeSrgb(r), 255};
}
}
