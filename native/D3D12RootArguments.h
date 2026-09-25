#pragma once
#include <d3d12.h>
#include <array>
#include <cstdint>

namespace leafclip {
// D3D12 command lists have no state getters. The draw hook uses this mirror
// to restore Unity's root arguments after a temporary clipping draw.
class RootArguments12 {
public:
    void Clear();
    void ClearTables();
    bool SetTable(unsigned slot,D3D12_GPU_DESCRIPTOR_HANDLE handle);
    bool SetCBV(unsigned slot,D3D12_GPU_VIRTUAL_ADDRESS address);
    bool SetSRV(unsigned slot,D3D12_GPU_VIRTUAL_ADDRESS address);
    bool SetUAV(unsigned slot,D3D12_GPU_VIRTUAL_ADDRESS address);
    bool SetConstants(unsigned slot,unsigned offset,const uint32_t* values,unsigned count);
    bool GetTable(unsigned slot,D3D12_GPU_DESCRIPTOR_HANDLE& handle) const;
    bool Replay(ID3D12GraphicsCommandList* list) const;
private:
    enum class Kind:uint8_t {Empty,Table,CBV,SRV,UAV,Constants};
    struct Argument {
        Kind kind=Kind::Empty;
        uint64_t value=0;
        std::array<uint32_t,64> constants{};
        uint64_t written=0;
    };
    std::array<Argument,64> arguments{};
    bool SetAddress(unsigned slot,Kind kind,uint64_t value);
};
}
