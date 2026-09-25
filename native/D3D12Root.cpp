#include "D3D12Root.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace leafclip {
namespace {
void Check(HRESULT hr,const char* label) {
    if(FAILED(hr))throw std::runtime_error(std::string(label)+" HRESULT="+std::to_string(uint32_t(hr)));
}
unsigned Cost(D3D12_ROOT_PARAMETER_TYPE type,unsigned constantCount) {
    return type==D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS?constantCount:
        type==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE?1:2;
}
template<class Range> DescriptorTable12 Table(unsigned parameter,const Range* ranges,unsigned count) {
    if(!ranges && count)throw std::runtime_error("D3D12 descriptor table ranges missing");
    DescriptorTable12 result;result.parameter=parameter;
    unsigned end=0;
    bool sampler=false,ordinary=false;
    for(unsigned i=0;i<count;++i) {
        const auto& r=ranges[i];
        sampler|=r.RangeType==D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ordinary|=r.RangeType!=D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        if(sampler && ordinary)throw std::runtime_error("D3D12 mixed sampler table");
        if(r.NumDescriptors==UINT_MAX)throw std::runtime_error("D3D12 unbounded descriptor table");
        unsigned start=r.OffsetInDescriptorsFromTableStart==D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND?
            end:r.OffsetInDescriptorsFromTableStart;
        if(start>4096 || r.NumDescriptors>4096-start)throw std::runtime_error("D3D12 descriptor table too large");
        result.ranges.emplace_back(start,r.NumDescriptors);
        end=std::max(end,start+r.NumDescriptors);
    }
    result.heap=sampler?D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    result.descriptors=end;
    return result;
}
}
std::vector<DescriptorTable12> DescribeRootTables12(const void* data,size_t size) {
    using Microsoft::WRL::ComPtr;
    if(!data || !size || size>1024*1024)throw std::runtime_error("invalid D3D12 root signature blob");
    ComPtr<ID3D12VersionedRootSignatureDeserializer> parsed;
    Check(D3D12CreateVersionedRootSignatureDeserializer(data,size,IID_PPV_ARGS(&parsed)),"parse root tables");
    const auto* source=parsed->GetUnconvertedRootSignatureDesc();
    std::vector<DescriptorTable12> result;
    if(source->Version==D3D_ROOT_SIGNATURE_VERSION_1_0) {
        const auto& d=source->Desc_1_0;
        for(unsigned i=0;i<d.NumParameters;++i) {
            const auto& p=d.pParameters[i];
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                result.push_back(Table(i,p.DescriptorTable.pDescriptorRanges,p.DescriptorTable.NumDescriptorRanges));
        }
    }else if(source->Version==D3D_ROOT_SIGNATURE_VERSION_1_1) {
        const auto& d=source->Desc_1_1;
        for(unsigned i=0;i<d.NumParameters;++i) {
            const auto& p=d.pParameters[i];
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                result.push_back(Table(i,p.DescriptorTable.pDescriptorRanges,p.DescriptorTable.NumDescriptorRanges));
        }
    }else throw std::runtime_error("unsupported D3D12 root signature version");
    return result;
}
ExtendedRoot12 ExtendRoot12(const void* data,size_t size) {
    using Microsoft::WRL::ComPtr;
    if(!data || !size || size>1024*1024)throw std::runtime_error("invalid D3D12 root signature blob");
    ComPtr<ID3D12VersionedRootSignatureDeserializer> parsed;
    Check(D3D12CreateVersionedRootSignatureDeserializer(data,size,IID_PPV_ARGS(&parsed)),"parse root signature");
    const auto* source=parsed->GetUnconvertedRootSignatureDesc();
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
    desc.Version=source->Version;
    unsigned count=0,cost=2; // Two new descriptor tables, one DWORD each.
    std::vector<D3D12_ROOT_PARAMETER> p10;
    std::vector<D3D12_ROOT_PARAMETER1> p11;
    D3D12_DESCRIPTOR_RANGE r10[2]{};
    D3D12_DESCRIPTOR_RANGE1 r11[2]{};
    r10[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_CBV;r10[0].NumDescriptors=1;r10[0].BaseShaderRegister=13;
    r10[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;r10[1].NumDescriptors=3;r10[1].BaseShaderRegister=125;
    r11[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_CBV;r11[0].NumDescriptors=1;r11[0].BaseShaderRegister=13;
    r11[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;r11[1].NumDescriptors=3;r11[1].BaseShaderRegister=125;
    for(unsigned i=0;i<2;++i) {
        r10[i].OffsetInDescriptorsFromTableStart=0;
        r11[i].OffsetInDescriptorsFromTableStart=0;
        r11[i].Flags=D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    }
    if(desc.Version==D3D_ROOT_SIGNATURE_VERSION_1_0) {
        desc.Desc_1_0=source->Desc_1_0;
        count=desc.Desc_1_0.NumParameters;
        for(unsigned i=0;i<count;++i) {
            const auto& p=desc.Desc_1_0.pParameters[i];cost+=Cost(p.ParameterType,p.Constants.Num32BitValues);
        }
        if(cost>64 || count>62)return {};
        p10.assign(desc.Desc_1_0.pParameters,desc.Desc_1_0.pParameters+count);
        for(unsigned i=0;i<2;++i) {
            D3D12_ROOT_PARAMETER p{};
            p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges=1;
            p.DescriptorTable.pDescriptorRanges=&r10[i];
            p.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            p10.push_back(p);
        }
        desc.Desc_1_0.NumParameters=UINT(p10.size());desc.Desc_1_0.pParameters=p10.data();
    }else if(desc.Version==D3D_ROOT_SIGNATURE_VERSION_1_1) {
        desc.Desc_1_1=source->Desc_1_1;
        count=desc.Desc_1_1.NumParameters;
        for(unsigned i=0;i<count;++i) {
            const auto& p=desc.Desc_1_1.pParameters[i];cost+=Cost(p.ParameterType,p.Constants.Num32BitValues);
        }
        if(cost>64 || count>62)return {};
        p11.assign(desc.Desc_1_1.pParameters,desc.Desc_1_1.pParameters+count);
        for(unsigned i=0;i<2;++i) {
            D3D12_ROOT_PARAMETER1 p{};
            p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges=1;
            p.DescriptorTable.pDescriptorRanges=&r11[i];
            p.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            p11.push_back(p);
        }
        desc.Desc_1_1.NumParameters=UINT(p11.size());desc.Desc_1_1.pParameters=p11.data();
    }else throw std::runtime_error("unsupported D3D12 root signature version");
    ComPtr<ID3DBlob> blob,error;
    Check(D3D12SerializeVersionedRootSignature(&desc,&blob,&error),"serialize extended root signature");
    ExtendedRoot12 result;
    result.firstClipParameter=count;
    const auto* bytes=static_cast<const uint8_t*>(blob->GetBufferPointer());
    result.serialized.assign(bytes,bytes+blob->GetBufferSize());
    return result;
}
}
