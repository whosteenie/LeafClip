#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <string>
#include <vector>

namespace leafclip {
// Own every pointer-bearing field used when a captured graphics PSO is rebuilt
// later on a worker thread. The descriptor returned by Describe is temporary.
class OwnedGraphicsPSO12 {
public:
    explicit OwnedGraphicsPSO12(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& source);
    OwnedGraphicsPSO12(const OwnedGraphicsPSO12&)=delete;
    OwnedGraphicsPSO12& operator=(const OwnedGraphicsPSO12&)=delete;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Describe(ID3D12RootSignature* root,
        const void* replacementPixel=nullptr,size_t replacementSize=0) const;
    D3D12_SHADER_BYTECODE PixelShader() const {return {shaders[1].data(),shaders[1].size()};}
    ID3D12RootSignature* OriginalRoot() const {return originalRoot.Get();}
private:
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> originalRoot;
    std::array<std::vector<uint8_t>,5> shaders;
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputs;
    std::vector<std::string> inputNames;
    std::vector<D3D12_SO_DECLARATION_ENTRY> outputs;
    std::vector<std::string> outputNames;
    std::vector<UINT> outputStrides;
};
}
