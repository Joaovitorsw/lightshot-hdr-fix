// Build-time tool only. Runtime GPU conversion consumes the generated header.
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
ComPtr<ID3DBlob> Compile(const std::string& source,const char* entry,const char* target) {
    ComPtr<ID3DBlob> code,errors;
    const HRESULT hr=D3DCompile(source.data(),source.size(),"lightshot_hdr_gpu",nullptr,nullptr,
        entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if (FAILED(hr)) {
        throw std::runtime_error(errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
            errors->GetBufferSize()) : "D3DCompile failed without diagnostics");
    }
    return code;
}
void WriteArray(std::ostream& output,const char* name,ID3DBlob* code) {
    output << "inline constexpr unsigned char " << name << "[] = {\n";
    const auto* bytes=static_cast<const unsigned char*>(code->GetBufferPointer());
    for (std::size_t i=0;i<code->GetBufferSize();++i) {
        if (i%16==0) output << "    ";
        output << "0x" << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[i]) << ',';
        output << (i%16==15 ? '\n' : ' ');
    }
    output << "\n};\n";
}
int main(int argc,char** argv) {
    try {
        if (argc!=3) throw std::runtime_error("Usage: shader_builder.exe input.hlsl output.h");
        std::ifstream input(argv[1],std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open HLSL input");
        const std::string source{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        auto vertex=Compile(source,"Vertex","vs_4_0");
        auto pixel=Compile(source,"Pixel","ps_4_0");
        std::ofstream output(argv[2],std::ios::binary|std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open output header");
        output << "// Generated from gpu_color.hlsl by scripts/shader_builder.cpp. Do not edit.\n"
            << "#pragma once\nnamespace lightshot_hdr::gpu_shader {\n";
        WriteArray(output,"vertex",vertex.Get());
        WriteArray(output,"pixel",pixel.Get());
        output << "}\n";
        output.close();
        if (!output) throw std::runtime_error("Writing shader header failed");
        std::cout << "Compiled vs_4_0 " << vertex->GetBufferSize() << " bytes, ps_4_0 "
            << pixel->GetBufferSize() << " bytes\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
