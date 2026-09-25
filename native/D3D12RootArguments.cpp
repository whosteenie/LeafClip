#include "D3D12RootArguments.h"

namespace leafclip {
void RootArguments12::Clear(){arguments={};}
void RootArguments12::ClearTables(){for(auto& arg:arguments)if(arg.kind==Kind::Table)arg={};}
bool RootArguments12::SetAddress(unsigned slot,Kind kind,uint64_t value) {
    if(slot>=arguments.size())return false;
    auto& a=arguments[slot];a={};a.kind=kind;a.value=value;return true;
}
bool RootArguments12::SetTable(unsigned slot,D3D12_GPU_DESCRIPTOR_HANDLE handle){return SetAddress(slot,Kind::Table,handle.ptr);}
bool RootArguments12::SetCBV(unsigned slot,D3D12_GPU_VIRTUAL_ADDRESS address){return SetAddress(slot,Kind::CBV,address);}
bool RootArguments12::SetSRV(unsigned slot,D3D12_GPU_VIRTUAL_ADDRESS address){return SetAddress(slot,Kind::SRV,address);}
bool RootArguments12::SetUAV(unsigned slot,D3D12_GPU_VIRTUAL_ADDRESS address){return SetAddress(slot,Kind::UAV,address);}
bool RootArguments12::SetConstants(unsigned slot,unsigned offset,const uint32_t* values,unsigned count) {
    if(count==0)return true;
    if(slot>=arguments.size() || !values || offset>=64 || count>64-offset)return false;
    auto& a=arguments[slot];
    if(a.kind!=Kind::Constants){a={};a.kind=Kind::Constants;}
    for(unsigned i=0;i<count;++i){a.constants[offset+i]=values[i];a.written|=uint64_t(1)<<(offset+i);}
    return true;
}
bool RootArguments12::GetTable(unsigned slot,D3D12_GPU_DESCRIPTOR_HANDLE& handle) const {
    if(slot>=arguments.size() || arguments[slot].kind!=Kind::Table)return false;
    handle.ptr=arguments[slot].value;return true;
}
bool RootArguments12::Replay(ID3D12GraphicsCommandList* list) const {
    if(!list)return false;
    for(unsigned slot=0;slot<arguments.size();++slot) {
        const auto& a=arguments[slot];
        switch(a.kind) {
        case Kind::Empty:break;
        case Kind::Table:list->SetGraphicsRootDescriptorTable(slot,{a.value});break;
        case Kind::CBV:list->SetGraphicsRootConstantBufferView(slot,a.value);break;
        case Kind::SRV:list->SetGraphicsRootShaderResourceView(slot,a.value);break;
        case Kind::UAV:list->SetGraphicsRootUnorderedAccessView(slot,a.value);break;
        case Kind::Constants:
            for(unsigned i=0;i<64;++i)if(a.written&(uint64_t(1)<<i))
                list->SetGraphicsRoot32BitConstant(slot,a.constants[i],i);
            break;
        }
    }
    return true;
}
}
