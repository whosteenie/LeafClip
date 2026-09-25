#pragma once
#include <d3d12.h>

namespace leafclip {
struct PipelineStreamShaders {
    D3D12_SHADER_BYTECODE pixel{};
    ID3D12RootSignature* root=nullptr;
    bool valid=false;
};
// A pipeline stream is a sequence of naturally aligned tagged subobjects.
// Unknown tags are rejected so that a newer SDK cannot be misread as shader data.
PipelineStreamShaders ReadPipelineStream(const D3D12_PIPELINE_STATE_STREAM_DESC* stream);
}
