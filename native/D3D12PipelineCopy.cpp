#include "D3D12PipelineCopy.h"
#include <cstring>
#include <stdexcept>

namespace leafclip {
OwnedGraphicsPSO12::OwnedGraphicsPSO12(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& source):desc(source),originalRoot(source.pRootSignature) {
    if(source.InputLayout.NumElements>64 || source.StreamOutput.NumEntries>64 ||
       source.StreamOutput.NumStrides>32)throw std::runtime_error("D3D12 pipeline layout limit");
    const D3D12_SHADER_BYTECODE* codes[]={&source.VS,&source.PS,&source.DS,&source.HS,&source.GS};
    for(size_t i=0;i<shaders.size();++i) {
        const auto& c=*codes[i];
        if(c.BytecodeLength>16*1024*1024 || (c.BytecodeLength && !c.pShaderBytecode))
            throw std::runtime_error("D3D12 shader bytecode limit");
        if(c.BytecodeLength) {
            auto start=static_cast<const uint8_t*>(c.pShaderBytecode);
            shaders[i].assign(start,start+c.BytecodeLength);
        }
    }
    if(source.InputLayout.NumElements) {
        if(!source.InputLayout.pInputElementDescs)throw std::runtime_error("D3D12 input layout missing");
        inputs.assign(source.InputLayout.pInputElementDescs,source.InputLayout.pInputElementDescs+source.InputLayout.NumElements);
        inputNames.reserve(inputs.size());
        for(const auto& input:inputs) {
            if(!input.SemanticName || strnlen_s(input.SemanticName,256)==256)
                throw std::runtime_error("D3D12 input semantic invalid");
            inputNames.emplace_back(input.SemanticName);
        }
        for(size_t i=0;i<inputs.size();++i)inputs[i].SemanticName=inputNames[i].c_str();
    }
    if(source.StreamOutput.NumEntries) {
        if(!source.StreamOutput.pSODeclaration)throw std::runtime_error("D3D12 stream output missing");
        outputs.assign(source.StreamOutput.pSODeclaration,source.StreamOutput.pSODeclaration+source.StreamOutput.NumEntries);
        outputNames.reserve(outputs.size());
        for(const auto& output:outputs) {
            if(!output.SemanticName || strnlen_s(output.SemanticName,256)==256)
                throw std::runtime_error("D3D12 stream semantic invalid");
            outputNames.emplace_back(output.SemanticName);
        }
        for(size_t i=0;i<outputs.size();++i)outputs[i].SemanticName=outputNames[i].c_str();
    }
    if(source.StreamOutput.NumStrides) {
        if(!source.StreamOutput.pBufferStrides)throw std::runtime_error("D3D12 stream strides missing");
        outputStrides.assign(source.StreamOutput.pBufferStrides,source.StreamOutput.pBufferStrides+source.StreamOutput.NumStrides);
    }
}
D3D12_GRAPHICS_PIPELINE_STATE_DESC OwnedGraphicsPSO12::Describe(ID3D12RootSignature* root,
    const void* replacementPixel,size_t replacementSize) const {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC result=desc;
    result.pRootSignature=root?root:originalRoot.Get();
    D3D12_SHADER_BYTECODE* codes[]={&result.VS,&result.PS,&result.DS,&result.HS,&result.GS};
    for(size_t i=0;i<shaders.size();++i) {
        codes[i]->pShaderBytecode=shaders[i].empty()?nullptr:shaders[i].data();
        codes[i]->BytecodeLength=shaders[i].size();
    }
    if(replacementPixel)result.PS={replacementPixel,replacementSize};
    result.InputLayout={inputs.empty()?nullptr:inputs.data(),UINT(inputs.size())};
    result.StreamOutput.pSODeclaration=outputs.empty()?nullptr:outputs.data();
    result.StreamOutput.NumEntries=UINT(outputs.size());
    result.StreamOutput.pBufferStrides=outputStrides.empty()?nullptr:outputStrides.data();
    result.StreamOutput.NumStrides=UINT(outputStrides.size());
    result.CachedPSO={}; // Unity-owned cached blob may have gone out of scope.
    return result;
}
}
