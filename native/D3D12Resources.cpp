#include "D3D12Resources.h"
#include <cstring>
#include <stdexcept>
#include <string>

namespace leafclip {
namespace {
void Check(HRESULT hr,const char* label) {
    if(FAILED(hr))throw std::runtime_error(std::string(label)+" HRESULT="+std::to_string(uint32_t(hr)));
}
}
BufferView12 MakeTypedBuffer12(ID3D12Device* device,const void* data,size_t bytes,
    DXGI_FORMAT format,unsigned elements) {
    if(!device || !data || !bytes || bytes>64*1024*1024 || !elements)
        throw std::runtime_error("invalid D3D12 upload buffer");
    BufferView12 result;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=(bytes+255)&~size_t(255);desc.Height=1;desc.DepthOrArraySize=1;
    desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_UPLOAD;
    Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result.resource)),"create D3D12 buffer");
    void* target=nullptr;Check(result.resource->Map(0,nullptr,&target),"map D3D12 buffer");
    memcpy(target,data,bytes);result.resource->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors=1;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&result.descriptors)),"create D3D12 CPU descriptor");
    result.view=result.descriptors->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements=elements;
    device->CreateShaderResourceView(result.resource.Get(),&srv,result.view);
    return result;
}
BufferView12 MakeClipConstants12(ID3D12Device* device,const ClipConstants& constants) {
    if(!device)throw std::runtime_error("D3D12 device missing");
    BufferView12 result;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=(sizeof(constants)+255)&~size_t(255);desc.Height=1;desc.DepthOrArraySize=1;
    desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_UPLOAD;
    Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result.resource)),"create D3D12 clip constants");
    void* target=nullptr;Check(result.resource->Map(0,nullptr,&target),"map D3D12 clip constants");
    memcpy(target,&constants,sizeof(constants));result.resource->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=1;
    Check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&result.descriptors)),"create D3D12 clip CBV");
    result.view=result.descriptors->GetCPUDescriptorHandleForHeapStart();
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{result.resource->GetGPUVirtualAddress(),UINT(desc.Width)};
    device->CreateConstantBufferView(&cbv,result.view);
    return result;
}
GeometryViews12 MakeGeometryViews12(ID3D12Device* device,const GeometryData& data) {
    GeometryViews12 result;
    if(data.primitives.empty() || data.nodes.empty())return result;
    result.primitives=MakeTypedBuffer12(device,data.primitives.data(),data.primitives.size()*16,
        DXGI_FORMAT_R32G32B32A32_FLOAT,unsigned(data.primitives.size()));
    result.nodes=MakeTypedBuffer12(device,data.nodes.data(),data.nodes.size()*16,
        DXGI_FORMAT_R32G32B32A32_UINT,unsigned(data.nodes.size()));
    result.nodeCount=unsigned(data.nodes.size()/2);
    return result;
}
}
