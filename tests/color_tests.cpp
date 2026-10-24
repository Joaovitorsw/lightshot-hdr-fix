#include "../src/color.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace reference {
float Half(std::uint16_t bits) {
    int e = (bits >> 10) & 31, f = bits & 1023;
    float v = e == 31 ? (f ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity())
        : std::ldexp(static_cast<float>(e ? 1024 + f : f), e ? e - 25 : -24);
    return bits & 0x8000 ? -v : v;
}
double Finite(float v) {
    return std::isnan(v) ? 0.0 : std::isinf(v)
        ? std::copysign(static_cast<double>(std::numeric_limits<float>::max()), v) : v;
}
std::uint8_t Encode(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(255 * (v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - .055)));
}
lightshot_hdr::Bgra8 Pixel(lightshot_hdr::FloatRgb input, float scale, bool hdr) {
    const double w = hdr && std::isfinite(scale) && scale > 0 ? scale : 1.0;
    double r = Finite(input.r) / w, g = Finite(input.g) / w, b = Finite(input.b) / w;
    const double l = .2126 * r + .7152 * g + .0722 * b;
    if (hdr && l > .75) {
        const double x = l - .75;
        const double gain = (.75 + .25 * x / (x + .25)) / l;
        r *= gain; g *= gain; b *= gain;
    }
    return {Encode(b), Encode(g), Encode(r), 255};
}
}

using namespace lightshot_hdr;
int failures = 0;
void Check(bool value, const char* name) {
    if (!value) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
bool Equal(Bgra8 x, Bgra8 y) {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}
int main(int argc, char**) {
    bool halfMatches = true;
    for (unsigned bits = 0; bits < 65536; ++bits) {
        float a = HalfToFloat(static_cast<std::uint16_t>(bits));
        float b = reference::Half(static_cast<std::uint16_t>(bits));
        if (!(std::isnan(a) && std::isnan(b)) && std::memcmp(&a, &b, sizeof(a)) != 0) halfMatches = false;
    }
    Check(halfMatches, "all65536 half patterns match exact reference");
    int maxError = 0;
    std::uint8_t previous = 0;
    bool monotonic = true;
    for (int i = 0; i <= 100000; ++i) {
        const float x = static_cast<float>(i) / 10000.0f;
        auto grayHdr = ConvertPixel({x,x,x}, 1, true);
        if (grayHdr.r < previous) monotonic = false;
        previous = grayHdr.r;
        for (bool hdr : {false,true}) {
            for (float scale : {.001f,1.0f,3.5f,100.0f}) {
                FloatRgb sample{x, x * .37f - .1f, x * .81f};
                auto a = ConvertPixel(sample,scale,hdr), b = reference::Pixel(sample,scale,hdr);
                maxError = std::max({maxError,std::abs(int(a.r)-int(b.r)),std::abs(int(a.g)-int(b.g)),std::abs(int(a.b)-int(b.b))});
            }
        }
    }
    Check(monotonic, "quantized HDR grayscale monotonic");
    for (int i = 0; i <= 1000000; ++i) {
        const float x = static_cast<float>(i) / 1000000.0f;
        auto pixel = ConvertPixel({x,x,x},1,false);
        maxError = std::max(maxError,std::abs(int(pixel.r)-int(reference::Encode(x))));
    }
    Check(maxError <= 1, "sRGB lookup differs by at most one byte");
    std::cout << "Max channel byte error: " << maxError << '\n';
    Check(HalfToFloat(0x3c00) == 1.0f, "half normal one");
    Check(HalfToFloat(0xc000) == -2.0f, "half negative");
    Check(HalfToFloat(0x7bff) == 65504.0f, "half largest finite");
    Check(HalfToFloat(1) == std::ldexp(1.0f, -24), "half smallest subnormal");
    Check(HalfToFloat(0x03ff) == std::ldexp(1023.0f, -24), "half largest subnormal");
    Check(HalfToFloat(0x0400) == std::ldexp(1.0f, -14), "half smallest normal");
    Check(std::signbit(HalfToFloat(0x8000)), "half negative zero");
    Check(std::isinf(HalfToFloat(0x7c00)), "half infinity");
    Check(std::isnan(HalfToFloat(0x7e00)), "half NaN");
    Check(Equal(ConvertPixel({1,1,1}, 1, false), {255,255,255,255}), "SDR white");
    Check(Equal(ConvertPixel({0,0,0}, 1, false), {0,0,0,255}), "SDR black opaque");
    auto gray = ConvertPixel({0.18f,0.18f,0.18f}, 1, false);
    Check(gray.r == 118 && gray.g == 118 && gray.b == 118, "sRGB middle gray");
    Check(Equal(gray, ConvertPixel({0.18f,0.18f,0.18f}, 1, true)), "HDR gray below shoulder unchanged");
    Check(Equal(ConvertPixel({1,0,0}, 1, false), {0,0,255,255}), "BGRA channel order");
    auto white = ConvertPixel({1,1,1},1,true);
    auto twice = ConvertPixel({2,2,2},1,true);
    auto four = ConvertPixel({4,4,4},1,true);
    Check(white.r == 240, "HDR white leaves highlight headroom");
    Check(white.r < twice.r && twice.r < four.r, "HDR 1 2 4 remain distinct after quantization");
    Check(Equal(white, ConvertPixel({3,3,3},3,true)), "HDR white normalization");
    Check(Equal(ConvertPixel({1,1,1},2,false), {255,255,255,255}), "SDR ignores HDR scale");
    Check(Equal(white, ConvertPixel({1,1,1},0,true)), "zero scale fallback");
    Check(Equal(white, ConvertPixel({1,1,1},-1,true)), "negative scale fallback");
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(Equal(white, ConvertPixel({1,1,1},nan,true)), "NaN scale fallback");
    Check(Equal(white, ConvertPixel({1,1,1},inf,true)), "infinite scale fallback");
    Check(Equal(ConvertPixel({nan,-inf,-1},1,false), {0,0,0,255}), "invalid dark channels safe");
    Check(ConvertPixel({inf,inf,inf},1,true).r == 255, "positive infinity maps to white");
    const float huge = std::numeric_limits<float>::max();
    Check(Equal(ConvertPixel({huge,huge,huge},std::numeric_limits<float>::denorm_min(),true), {255,255,255,255}), "finite extremes safe");
    if (argc > 1) {
        struct Sample { std::uint16_t r, g, b; };
        std::vector<Sample> samples;
        for (unsigned i = 0; i < 262144; ++i) {
            samples.push_back({static_cast<std::uint16_t>(0x1800 + i % 0x3400),
                static_cast<std::uint16_t>(0x1800 + (i * 31) % 0x3400),
                static_cast<std::uint16_t>(0x1800 + (i * 131) % 0x3400)});
        }
        volatile std::uint64_t sink = 0;
        auto measure = [&](auto half, auto pixel) {
            const auto start = std::chrono::steady_clock::now();
            std::uint64_t sum = 0;
            for (int repeat = 0; repeat < 8; ++repeat) {
                for (const auto& s : samples) {
                    auto p = pixel({half(s.r),half(s.g),half(s.b)},2.5f,true);
                    sum += p.r + p.g + p.b;
                }
            }
            sink = sum;
            return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        };
        measure(HalfToFloat,ConvertPixel); // Warm lazy encoding tables outside timed run.
        const double oldSeconds = measure(reference::Half,reference::Pixel);
        const double newSeconds = measure(HalfToFloat,ConvertPixel);
        std::cout << "Benchmark2097152 pixels: exact=" << oldSeconds << "s optimized=" << newSeconds
            << "s speedup=" << oldSeconds/newSeconds << " checksum=" << sink << '\n';
        Check(newSeconds < oldSeconds * .8, "optimized corpus at least20percent faster");
    }
    std::cout << (failures ? "FAILED " : "PASSED ") << failures << " failures\n";
    return failures ? 1 : 0;
}
