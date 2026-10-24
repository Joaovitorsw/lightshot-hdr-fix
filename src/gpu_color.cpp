#include "gpu_color.h"
#include "gpu_color_bytecode.h"
#include <wrl/client.h>
#include <cmath>
#include <stdexcept>
#include <string>

namespace lightshot_hdr {
namespace {
using Microsoft::WRL::ComPtr;
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(result));
}


}

struct GpuToneMapper::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Texture2D> source, output;
    ComPtr<ID3D11ShaderResourceView> sourceView;
    ComPtr<ID3D11RenderTargetView> outputView;
    UINT width=0, height=0;

    explicit Impl(ID3D11Device* inputDevice) : device(inputDevice) {
        if (!device) throw std::invalid_argument("GpuToneMapper requires a D3D11 device");
        Check(device->CreateVertexShader(gpu_shader::vertex,sizeof(gpu_shader::vertex),nullptr,&vertex),"CreateVertexShader");
        Check(device->CreatePixelShader(gpu_shader::pixel,sizeof(gpu_shader::pixel),nullptr,&pixel),"CreatePixelShader");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth=16; buffer.Usage=D3D11_USAGE_DEFAULT; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Check(device->CreateBuffer(&buffer,nullptr,&constants),"Create conversion constants");
        D3D11_RASTERIZER_DESC state{};
        state.FillMode=D3D11_FILL_SOLID; state.CullMode=D3D11_CULL_NONE; state.DepthClipEnable=TRUE;
        Check(device->CreateRasterizerState(&state,&raster),"Create conversion rasterizer");
    }

    void Resize(UINT nextWidth, UINT nextHeight) {
        if (width==nextWidth && height==nextHeight) return;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=nextWidth; desc.Height=nextHeight; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> nextSource,nextOutput;
        ComPtr<ID3D11ShaderResourceView> nextSourceView;
        ComPtr<ID3D11RenderTargetView> nextOutputView;
        Check(device->CreateTexture2D(&desc,nullptr,&nextSource),"Create SRV copy texture");
        Check(device->CreateShaderResourceView(nextSource.Get(),nullptr,&nextSourceView),"Create SRV copy view");
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        Check(device->CreateTexture2D(&desc,nullptr,&nextOutput),"Create BGRA8 render target");
        Check(device->CreateRenderTargetView(nextOutput.Get(),nullptr,&nextOutputView),"Create BGRA8 target view");
        source=std::move(nextSource); output=std::move(nextOutput);
        sourceView=std::move(nextSourceView); outputView=std::move(nextOutputView);
        width=nextWidth; height=nextHeight;
    }
};
GpuToneMapper::GpuToneMapper(ID3D11Device* device) : impl_(std::make_unique<Impl>(device)) {}
GpuToneMapper::~GpuToneMapper() = default;
ID3D11Texture2D* GpuToneMapper::Convert(ID3D11DeviceContext* context, ID3D11Texture2D* input, bool hdr, float white) {
    if (!context || !input) throw std::invalid_argument("Conversion requires context and input texture");
    ComPtr<ID3D11Device> contextDevice,inputDevice;
    context->GetDevice(&contextDevice); input->GetDevice(&inputDevice);
    if (contextDevice.Get()!=impl_->device.Get() || inputDevice.Get()!=impl_->device.Get()
        || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        throw std::invalid_argument("Conversion requires an immediate context and texture from its own device");
    }
    D3D11_TEXTURE2D_DESC desc{};
    input->GetDesc(&desc);
    if (desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT || desc.MipLevels!=1 || desc.ArraySize!=1
        || desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality!=0) {
        throw std::invalid_argument("Conversion requires single-sample single-slice FP16 texture");
    }
    impl_->Resize(desc.Width,desc.Height);
    const struct { float white; UINT hdr; float padding[2]; } parameters{
        std::isfinite(white) && white>0 ? white : 1.0f,hdr ? 1U : 0U,{0,0}};
    context->UpdateSubresource(impl_->constants.Get(),0,nullptr,&parameters,0,0);
    context->CopyResource(impl_->source.Get(),input);
    ID3D11RenderTargetView* target=impl_->outputView.Get();
    context->OMSetRenderTargets(1,&target,nullptr);
    context->OMSetBlendState(nullptr,nullptr,0xffffffffU);
    context->OMSetDepthStencilState(nullptr,0);
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height),0,1};
    context->RSSetViewports(1,&viewport);
    context->RSSetState(impl_->raster.Get());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(impl_->vertex.Get(),nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);
    context->HSSetShader(nullptr,nullptr,0);
    context->DSSetShader(nullptr,nullptr,0);
    context->PSSetShader(impl_->pixel.Get(),nullptr,0);
    ID3D11Buffer* buffer=impl_->constants.Get();
    ID3D11ShaderResourceView* source=impl_->sourceView.Get();
    context->PSSetConstantBuffers(0,1,&buffer);
    context->PSSetShaderResources(0,1,&source);
    context->Draw(3,0);
    source=nullptr;
    context->PSSetShaderResources(0,1,&source);
    context->OMSetRenderTargets(0,nullptr,nullptr);
    return impl_->output.Get();
}
}
