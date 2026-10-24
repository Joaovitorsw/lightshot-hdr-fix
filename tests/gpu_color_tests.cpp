#include "../src/gpu_color.h"
#include "../src/color.h"
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <chrono>

using Microsoft::WRL::ComPtr;
using namespace lightshot_hdr;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Hr(HRESULT hr, const char* message) { Require(SUCCEEDED(hr),message); }
void Run(D3D_DRIVER_TYPE driver, const char* name) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Hr(D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"D3D11CreateDevice");
    constexpr UINT width = 257, height = 32;
    std::vector<std::array<std::uint16_t,4>> pixels(width*height);
    const std::array<std::uint16_t,16> cases{0,1,0x03ff,0x0400,0x211f,0x31c3,0x3800,0x3c00,
        0x4000,0x4400,0x7bff,0xbc00,0xfbff,0x7c00,0xfc00,0x7e00};
    for (UINT i=0;i<pixels.size();++i) {
        pixels[i] = {cases[i%cases.size()],cases[(i/16)%cases.size()],cases[(i/256)%cases.size()],0};
    }
    pixels[0]={0,0,0,0};
    pixels[1]={0x31c3,0x31c3,0x31c3,0}; // nearest half to linear0.18.
    pixels[2]={0x3c00,0x3c00,0x3c00,0};
    pixels[3]={0x4000,0x4000,0x4000,0};
    pixels[4]={0x4400,0x4400,0x4400,0};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT; // Deliberately no BIND_SHADER_RESOURCE.
    D3D11_SUBRESOURCE_DATA data{pixels.data(),width*8,0};
    ComPtr<ID3D11Texture2D> input;
    Hr(device->CreateTexture2D(&desc,&data,&input),"create FP16 input");
    const auto constructorStart=std::chrono::steady_clock::now();
    GpuToneMapper mapper(device.Get());
    std::cout << name << " mapper constructor_us=" << std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-constructorStart).count() << '\n';
    unsigned maxError=0;
    ID3D11Texture2D* cached=nullptr;
    for (bool hdr : {true,false,true}) {
        for (float white : {1.f,2.5f,0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),std::numeric_limits<float>::denorm_min(),
                std::numeric_limits<float>::max()}) {
            auto output=mapper.Convert(context.Get(),input.Get(),hdr,white);
            Require(output!=nullptr,"nonnull output");
            output->GetDesc(&desc);
            Require(desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM,"output must be BGRA8 UNORM");
            Require(desc.Width==width && desc.Height==height,"output dimensions");
            if (cached) Require(output==cached,"same-size texture is cached");
            cached=output;
            desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            Hr(device->CreateTexture2D(&desc,nullptr,&staging),"create staging");
            context->CopyResource(staging.Get(),output);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"map converted pixels");
            for (UINT y=0;y<height;++y) {
                auto row=reinterpret_cast<const Bgra8*>(static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch);
                for (UINT x=0;x<width;++x) {
                    const auto& p=pixels[y*width+x];
                    auto expected=ConvertPixel({HalfToFloat(p[0]),HalfToFloat(p[1]),HalfToFloat(p[2])},white,hdr);
                    const auto& actual=row[x];
                    const auto error=static_cast<unsigned>(std::max({std::abs(int(actual.r)-int(expected.r)),
                        std::abs(int(actual.g)-int(expected.g)),std::abs(int(actual.b)-int(expected.b))}));
                    maxError=std::max(maxError,error);
                    if (error>1) {
                        std::cerr << name << " pixel=" << y*width+x << " hdr=" << hdr << " white=" << white
                            << " actual=" << int(actual.r) << ',' << int(actual.g) << ',' << int(actual.b)
                            << " expected=" << int(expected.r) << ',' << int(expected.g) << ',' << int(expected.b) << '\n';
                    }
                    Require(error<=1,"GPU differs from CPU by more than one byte");
                    Require(actual.a==255,"opaque output alpha");
                }
            }
            if (hdr && white==1.f) {
                const auto* row=static_cast<const Bgra8*>(mapped.pData);
                Require(row[1].r==118 && row[2].r==240 && row[3].r==250 && row[4].r==253,
                    "HDR reference grays118240250253");
            }
            context->Unmap(staging.Get(),0);
        }
    }
    std::cout << name << " PASSED197376 pixels; maximum byte error=" << maxError << '\n';
}
int main() {
    try { Run(D3D_DRIVER_TYPE_HARDWARE,"hardware"); Run(D3D_DRIVER_TYPE_WARP,"WARP"); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
