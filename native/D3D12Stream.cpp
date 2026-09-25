#include "D3D12Stream.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace leafclip {
namespace {
template<class T> struct alignas(void*) Subobject { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; T value; };
template<class T> bool Read(const uint8_t* data,size_t bytes,size_t& offset,T* value=nullptr) {
    if(offset>bytes || sizeof(Subobject<T>)>bytes-offset) return false;
    if(value) memcpy(value,data+offset+offsetof(Subobject<T>,value),sizeof(T));
    offset+=sizeof(Subobject<T>);
    return true;
}
}
PipelineStreamShaders ReadPipelineStream(const D3D12_PIPELINE_STATE_STREAM_DESC* stream) {
    PipelineStreamShaders result{};
    if(!stream || !stream->pPipelineStateSubobjectStream || !stream->SizeInBytes ||
       stream->SizeInBytes>1024*1024 ||
       (uintptr_t(stream->pPipelineStateSubobjectStream)%alignof(void*))!=0) return result;
    auto data=static_cast<const uint8_t*>(stream->pPipelineStateSubobjectStream);
    size_t offset=0;
    while(offset<stream->SizeInBytes) {
        if(stream->SizeInBytes-offset<sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)) return {};
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        memcpy(&type,data+offset,sizeof(type));
        bool ok=false;
        switch(type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: ok=Read(data,stream->SizeInBytes,offset,&result.root);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: ok=Read<D3D12_SHADER_BYTECODE>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: ok=Read(data,stream->SizeInBytes,offset,&result.pixel);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: ok=Read<D3D12_STREAM_OUTPUT_DESC>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: ok=Read<D3D12_BLEND_DESC>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: ok=Read<UINT>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: ok=Read<D3D12_RASTERIZER_DESC>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: ok=Read<D3D12_DEPTH_STENCIL_DESC>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: ok=Read<D3D12_INPUT_LAYOUT_DESC>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: ok=Read<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: ok=Read<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: ok=Read<D3D12_RT_FORMAT_ARRAY>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: ok=Read<DXGI_FORMAT>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: ok=Read<DXGI_SAMPLE_DESC>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: ok=Read<D3D12_CACHED_PIPELINE_STATE>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: ok=Read<D3D12_PIPELINE_STATE_FLAGS>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: ok=Read<D3D12_DEPTH_STENCIL_DESC1>(data,stream->SizeInBytes,offset);break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: ok=Read<D3D12_VIEW_INSTANCING_DESC>(data,stream->SizeInBytes,offset);break;
        default: return {};
        }
        if(!ok) return {};
    }
    result.valid=true;
    return result;
}
}
