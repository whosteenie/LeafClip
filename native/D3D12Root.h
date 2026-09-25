#pragma once
#include <d3d12.h>
#include <cstdint>
#include <utility>
#include <vector>

namespace leafclip {
struct ExtendedRoot12 {
    std::vector<uint8_t> serialized;
    unsigned firstClipParameter=0;
};
struct DescriptorTable12 {
    unsigned parameter=0;
    D3D12_DESCRIPTOR_HEAP_TYPE heap=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    unsigned descriptors=0; // Span including explicit offsets and holes.
    std::vector<std::pair<unsigned,unsigned>> ranges; // Offset and count of initialized descriptors.
};
// Describe the original tables so their live descriptors can be copied into
// a temporary heap and rebound at the same parameter indices for one draw.
// Throws for unbounded/oversized tables, which must render unchanged.
std::vector<DescriptorTable12> DescribeRootTables12(const void* data,size_t size);
// Preserve Unity's parameter order and append a CBV descriptor table at b13
// and an SRV descriptor table at t125-t127. Each table costs one root DWORD.
// Returns an empty blob if the original exceeds the 64-DWORD root budget.
ExtendedRoot12 ExtendRoot12(const void* data,size_t size);
}
