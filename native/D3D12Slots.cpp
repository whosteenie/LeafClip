#define CINTERFACE
#include <d3d12.h>
#include <cstddef>
extern "C" unsigned LC_D3D12GraphicsPSOSlot() {return unsigned(offsetof(ID3D12DeviceVtbl,CreateGraphicsPipelineState)/sizeof(void*));}
extern "C" unsigned LC_D3D12RootSignatureSlot() {return unsigned(offsetof(ID3D12DeviceVtbl,CreateRootSignature)/sizeof(void*));}
extern "C" unsigned LC_D3D12PipelineStreamSlot() {return unsigned(offsetof(ID3D12Device2Vtbl,CreatePipelineState)/sizeof(void*));}
extern "C" unsigned LC_D3D12DrawIndexedSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,DrawIndexedInstanced)/sizeof(void*));}
extern "C" unsigned LC_D3D12ResetSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,Reset)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetPipelineSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetPipelineState)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetHeapsSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetDescriptorHeaps)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetRootSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetGraphicsRootSignature)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetTableSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetGraphicsRootDescriptorTable)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetConstantSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetGraphicsRoot32BitConstant)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetConstantsSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetGraphicsRoot32BitConstants)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetCBVSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetGraphicsRootConstantBufferView)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetSRVSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetGraphicsRootShaderResourceView)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetUAVSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,SetGraphicsRootUnorderedAccessView)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetIndexSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,IASetIndexBuffer)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetViewportsSlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,RSSetViewports)/sizeof(void*));}
extern "C" unsigned LC_D3D12SetTopologySlot() {return unsigned(offsetof(ID3D12GraphicsCommandListVtbl,IASetPrimitiveTopology)/sizeof(void*));}
extern "C" unsigned LC_D3D12ExecuteSlot() {return unsigned(offsetof(ID3D12CommandQueueVtbl,ExecuteCommandLists)/sizeof(void*));}
