#include "D3D12DescriptorCopy.h"
#include <stdexcept>
#include <string>

namespace leafclip {
namespace {
void Check(HRESULT hr,const char* label) {
    if(FAILED(hr))throw std::runtime_error(std::string(label)+" HRESULT="+std::to_string(uint32_t(hr)));
}
}
CopiedDescriptors12 CopyDescriptors12(ID3D12Device* device,ID3D12DescriptorHeap* source,
    const std::vector<DescriptorTable12>& tables,const RootArguments12& original,
    const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4>& clip,unsigned cbvParameter,unsigned srvParameter,
    DescriptorHeapProvider12 acquire,void* acquireContext) {
    if(!device || cbvParameter>=64 || srvParameter>=64)throw std::runtime_error("invalid D3D12 descriptor copy arguments");
    constexpr auto type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    const unsigned stride=device->GetDescriptorHandleIncrementSize(type);
    if(!stride)throw std::runtime_error("invalid D3D12 descriptor stride");
    struct Copy {unsigned param,sourceOffset,count,destOffset;std::vector<std::pair<unsigned,unsigned>> ranges;};
    std::vector<Copy> copies;
    unsigned total=0;
    for(const auto& table:tables) {
        if(table.heap!=type || !table.descriptors)continue;
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if(!original.GetTable(table.parameter,handle))continue;
        if(!source)throw std::runtime_error("D3D12 source descriptor heap missing");
        auto sourceDesc=source->GetDesc();
        if(sourceDesc.Type!=type || !(sourceDesc.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
            throw std::runtime_error("D3D12 source descriptor heap is not shader visible");
        auto base=source->GetGPUDescriptorHandleForHeapStart().ptr;
        if(handle.ptr<base || (handle.ptr-base)%stride)throw std::runtime_error("D3D12 table outside source heap");
        uint64_t offset=(handle.ptr-base)/stride;
        if(offset>sourceDesc.NumDescriptors || table.descriptors>sourceDesc.NumDescriptors-offset)
            throw std::runtime_error("D3D12 table exceeds source heap");
        if(table.descriptors>4092 || total>4092-table.descriptors)
            throw std::runtime_error("D3D12 copied descriptor budget exceeded");
        copies.push_back({table.parameter,unsigned(offset),table.descriptors,total,table.ranges});
        total+=table.descriptors;
    }
    D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=type;desc.NumDescriptors=total+4;
    desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CopiedDescriptors12 result;result.arguments=original;
    if(acquire)result.heap=acquire(desc.NumDescriptors,acquireContext);
    if(result.heap) {
        const auto reused=result.heap->GetDesc();
        if(reused.Type!=type || reused.NumDescriptors<desc.NumDescriptors ||
           !(reused.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
            throw std::runtime_error("recycled D3D12 descriptor heap is incompatible");
    } else Check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&result.heap)),"create D3D12 draw descriptor heap");
    auto targetCPU=result.heap->GetCPUDescriptorHandleForHeapStart();
    auto targetGPU=result.heap->GetGPUDescriptorHandleForHeapStart();
    for(const auto& item:copies) {
        for(const auto& range:item.ranges) {
            if(!range.second)continue;
            auto src=source->GetCPUDescriptorHandleForHeapStart();
            src.ptr+=size_t(item.sourceOffset+range.first)*stride;
            auto dst=targetCPU;dst.ptr+=size_t(item.destOffset+range.first)*stride;
            device->CopyDescriptorsSimple(range.second,dst,src,type);
        }
        auto relocated=targetGPU;relocated.ptr+=uint64_t(item.destOffset)*stride;
        result.arguments.SetTable(item.param,relocated);
    }
    for(unsigned i=0;i<4;++i) {
        if(!clip[i].ptr)throw std::runtime_error("D3D12 clip descriptor missing");
        auto dst=targetCPU;dst.ptr+=size_t(total+i)*stride;
        device->CopyDescriptorsSimple(1,dst,clip[i],type);
    }
    auto cbv=targetGPU;cbv.ptr+=uint64_t(total)*stride;
    auto srvs=cbv;srvs.ptr+=stride;
    result.arguments.SetTable(cbvParameter,cbv);
    result.arguments.SetTable(srvParameter,srvs);
    return result;
}
}
