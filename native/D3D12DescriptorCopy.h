#pragma once
#include "D3D12Root.h"
#include "D3D12RootArguments.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>

namespace leafclip {
struct CopiedDescriptors12 {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    RootArguments12 arguments;
};
using DescriptorHeapProvider12=Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>(*)(unsigned,void*);
// Copy the live CBV/SRV/UAV tables into one draw-private heap, append b13 and
// t125-t127, and return relocated root arguments. Sampler tables stay in the
// original sampler heap. Caller must keep the returned heap alive through GPU
// completion and restore Unity's original heap/arguments after the draw.
CopiedDescriptors12 CopyDescriptors12(ID3D12Device* device,ID3D12DescriptorHeap* source,
    const std::vector<DescriptorTable12>& tables,const RootArguments12& original,
    const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4>& clip,unsigned cbvParameter,unsigned srvParameter,
    DescriptorHeapProvider12 acquire=nullptr,void* acquireContext=nullptr);
}
