#pragma once
#include "Geometry.h"
#include "ShaderPatch.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

namespace leafclip {
struct BufferView12 {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors;
    D3D12_CPU_DESCRIPTOR_HANDLE view{};
};
BufferView12 MakeTypedBuffer12(ID3D12Device* device,const void* data,size_t bytes,
    DXGI_FORMAT format,unsigned elements);
BufferView12 MakeClipConstants12(ID3D12Device* device,const ClipConstants& constants);
struct GeometryViews12 {
    BufferView12 primitives,nodes;
    unsigned nodeCount=0;
};
GeometryViews12 MakeGeometryViews12(ID3D12Device* device,const GeometryData& data);
}
