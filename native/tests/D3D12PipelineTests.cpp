#include "ShaderPatch.h"
#include "Native.h"
#include "Geometry.h"
#include "D3D12Stream.h"
#include "D3D12Root.h"
#include "D3D12PipelineCopy.h"
#include "D3D12RootArguments.h"
#include "D3D12DescriptorCopy.h"
#include "D3D12Resources.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <array>
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace leafclip;
namespace {
void Check(HRESULT hr,const char* label) {if(FAILED(hr))throw std::runtime_error(std::string(label)+" HRESULT="+std::to_string(uint32_t(hr)));}
template<class T> struct alignas(void*) StreamField {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;T value;};
}
int main() {
    try {
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        ComPtr<IDXGIAdapter> adapter;
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP adapter");
        ComPtr<ID3D12Device> device;
        Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"D3D12 device");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width=textureDesc.Height=1;textureDesc.DepthOrArraySize=1;
        textureDesc.MipLevels=1;textureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count=1;textureDesc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        ComPtr<ID3D12Resource> marker;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&textureDesc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&marker)),"probe marker");
        if(!LC_Initialize12(marker.Get()))throw std::runtime_error("native D3D12 probe initialization failed");
        auto vs=Compile("float4 main(float3 pos:POSITION):SV_Position{return float4(pos,1);}","vs_5_0");
        auto ps=Compile("float4 main(float4 p:SV_Position):SV_Target{return float4(p.z,0,0,1);}","ps_5_0");
        auto patched=PatchPixelShader(ps->GetBufferPointer(),ps->GetBufferSize());
        struct alignas(void*) StreamItemRoot {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;ID3D12RootSignature* value;};
        struct alignas(void*) StreamItemPS {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;D3D12_SHADER_BYTECODE value;};
        struct alignas(void*) StreamItems {StreamItemRoot root;StreamItemPS pixel;};
        StreamItems items{{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,nullptr},
                          {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,{patched.data(),patched.size()}}};
        D3D12_PIPELINE_STATE_STREAM_DESC stream{sizeof(items),&items};
        auto decoded=ReadPipelineStream(&stream);
        if(!decoded.valid || decoded.pixel.pShaderBytecode!=patched.data() || decoded.pixel.BytecodeLength!=patched.size())
            throw std::runtime_error("D3D12 pipeline stream shader parser failed");
        --stream.SizeInBytes;
        if(ReadPipelineStream(&stream).valid)throw std::runtime_error("D3D12 truncated stream was accepted");

        D3D12_DESCRIPTOR_RANGE ranges[3]{};
        for(unsigned i=0;i<3;++i) {
            ranges[i].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[i].NumDescriptors=1;ranges[i].BaseShaderRegister=125+i;
            ranges[i].RegisterSpace=0;ranges[i].OffsetInDescriptorsFromTableStart=0;
        }
        D3D12_ROOT_PARAMETER parameters[4]{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister=13;
        parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        for(unsigned i=0;i<3;++i) {
            parameters[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[i+1].DescriptorTable.NumDescriptorRanges=1;
            parameters[i+1].DescriptorTable.pDescriptorRanges=&ranges[i];
            parameters[i+1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters=4;rootDesc.pParameters=parameters;
        rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> rootCode,errors;
        Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootCode,&errors),"serialize root signature");
        auto tableLayout=DescribeRootTables12(rootCode->GetBufferPointer(),rootCode->GetBufferSize());
        if(tableLayout.size()!=3 || tableLayout[0].parameter!=1 || tableLayout[0].descriptors!=1 ||
           tableLayout[2].parameter!=3 || tableLayout[2].descriptors!=1)
            throw std::runtime_error("D3D12 descriptor table layout parser failed");
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0,rootCode->GetBufferPointer(),rootCode->GetBufferSize(),IID_PPV_ARGS(&root)),"create root signature");
        D3D12_ROOT_PARAMETER baseParameter{};
        D3D12_DESCRIPTOR_RANGE baseRange{};baseRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        baseRange.NumDescriptors=1;baseRange.BaseShaderRegister=0;
        baseParameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        baseParameter.DescriptorTable.NumDescriptorRanges=1;
        baseParameter.DescriptorTable.pDescriptorRanges=&baseRange;
        baseParameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC baseDesc{};
        baseDesc.NumParameters=1;baseDesc.pParameters=&baseParameter;
        baseDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> baseCode;
        Check(D3D12SerializeRootSignature(&baseDesc,D3D_ROOT_SIGNATURE_VERSION_1,&baseCode,&errors),"serialize base root");
        ComPtr<ID3D12RootSignature> baseRoot;
        Check(device->CreateRootSignature(0,baseCode->GetBufferPointer(),baseCode->GetBufferSize(),
            IID_PPV_ARGS(&baseRoot)),"create base root signature");
        auto extension=ExtendRoot12(baseCode->GetBufferPointer(),baseCode->GetBufferSize());
        if(extension.serialized.empty() || extension.firstClipParameter!=1)
            throw std::runtime_error("D3D12 root signature extension failed");
        ComPtr<ID3D12RootSignature> expandedRoot;
        Check(device->CreateRootSignature(0,extension.serialized.data(),extension.serialized.size(),IID_PPV_ARGS(&expandedRoot)),
            "create expanded root signature");
        D3D12_ROOT_PARAMETER1 baseParameter11{};
        baseParameter11.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        baseParameter11.Descriptor.ShaderRegister=0;
        baseParameter11.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedBase{};
        versionedBase.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
        versionedBase.Desc_1_1.NumParameters=1;
        versionedBase.Desc_1_1.pParameters=&baseParameter11;
        versionedBase.Desc_1_1.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> baseCode11;
        Check(D3D12SerializeVersionedRootSignature(&versionedBase,&baseCode11,&errors),"serialize version 1.1 base root");
        auto extension11=ExtendRoot12(baseCode11->GetBufferPointer(),baseCode11->GetBufferSize());
        if(extension11.serialized.empty() || extension11.firstClipParameter!=1)
            throw std::runtime_error("D3D12 version 1.1 root extension failed");
        ComPtr<ID3D12RootSignature> expandedRoot11;
        Check(device->CreateRootSignature(0,extension11.serialized.data(),extension11.serialized.size(),
            IID_PPV_ARGS(&expandedRoot11)),"create expanded version 1.1 root signature");
        bool sawRootLayout=false;
        char nativeLog[2048];
        while(LC_PollLog(nativeLog,sizeof(nativeLog))>0) {
            std::string line(nativeLog);
            if(line.find("b13=1")!=std::string::npos && line.find("t125-127=111")!=std::string::npos)
                sawRootLayout=true;
        }
        if(!sawRootLayout)throw std::runtime_error("D3D12 root binding probe missed shader resources");
        D3D12_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature=root.Get();
        desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        desc.PS={patched.data(),patched.size()};
        desc.InputLayout={&element,1};
        desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable=TRUE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask=UINT_MAX;
        desc.NumRenderTargets=1;
        desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline)),"patched D3D12 graphics pipeline");
        desc.pRootSignature=expandedRoot.Get();
        ComPtr<ID3D12PipelineState> expandedPipeline;
        Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&expandedPipeline)),"expanded-root D3D12 graphics pipeline");
        OwnedGraphicsPSO12 owned(desc);
        auto copiedDesc=owned.Describe(expandedRoot.Get(),patched.data(),patched.size());
        ComPtr<ID3D12PipelineState> copiedPipeline;
        Check(device->CreateGraphicsPipelineState(&copiedDesc,IID_PPV_ARGS(&copiedPipeline)),"captured D3D12 graphics pipeline");
        desc.pRootSignature=root.Get();
        desc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        ComPtr<ID3D12PipelineState> originalPipeline;
        Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&originalPipeline)),"original D3D12 graphics pipeline");
        desc.pRootSignature=baseRoot.Get();
        ComPtr<ID3D12PipelineState> basePipeline;
        Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&basePipeline)),"original D3D12 base-root pipeline");
        desc.pRootSignature=root.Get();
        uint64_t counts[14]{};LC_GetStats12(counts,14);
        if(counts[0]<1 || counts[1]<1 || counts[2]<1)
            throw std::runtime_error("D3D12 device creation hooks did not observe PSO/root signature");
        ComPtr<ID3D12Device2> modern;
        if(SUCCEEDED(device.As(&modern))) {
            struct alignas(void*) GraphicsStream {
                StreamField<ID3D12RootSignature*> root;
                StreamField<D3D12_SHADER_BYTECODE> vertex,pixel;
                StreamField<D3D12_INPUT_LAYOUT_DESC> input;
                StreamField<D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
                StreamField<D3D12_RASTERIZER_DESC> raster;
                StreamField<D3D12_BLEND_DESC> blend;
                StreamField<UINT> sampleMask;
                StreamField<D3D12_RT_FORMAT_ARRAY> targets;
                StreamField<DXGI_SAMPLE_DESC> samples;
            } graphics{};
            graphics.root={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,root.Get()};
            graphics.vertex={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,desc.VS};
            graphics.pixel={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,desc.PS};
            graphics.input={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,desc.InputLayout};
            graphics.topology={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,desc.PrimitiveTopologyType};
            graphics.raster={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,desc.RasterizerState};
            graphics.blend={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,desc.BlendState};
            graphics.sampleMask={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,desc.SampleMask};
            D3D12_RT_FORMAT_ARRAY formats{};formats.NumRenderTargets=1;formats.RTFormats[0]=desc.RTVFormats[0];
            graphics.targets={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,formats};
            graphics.samples={D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,desc.SampleDesc};
            D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{sizeof(graphics),&graphics};
            auto parsed=ReadPipelineStream(&streamDesc);
            if(!parsed.valid || parsed.root!=root.Get() || parsed.pixel.pShaderBytecode!=ps->GetBufferPointer())
                throw std::runtime_error("D3D12 graphics stream parser failed");
            ComPtr<ID3D12PipelineState> streamPipeline;
            Check(modern->CreatePipelineState(&streamDesc,IID_PPV_ARGS(&streamPipeline)),"stream D3D12 graphics pipeline");
            LC_GetStats12(counts,4);
            if(counts[3]<1 || counts[1]<3)throw std::runtime_error("D3D12 pipeline-stream hook missed pixel shader");
        }
        auto makeBuffer=[&](size_t bytes,const void* data) {
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=(bytes+255)&~size_t(255);
            d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_UPLOAD;
            ComPtr<ID3D12Resource> result;
            Check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result)),"upload buffer");
            void* mapped=nullptr;Check(result->Map(0,nullptr,&mapped),"map upload");memcpy(mapped,data,bytes);result->Unmap(0,nullptr);
            return result;
        };
        constexpr unsigned side=64;
        float vertices[]={-.85f,-.85f,.5f, .85f,-.85f,.5f, .85f,.85f,.5f,
                          -.85f,-.85f,.5f, .85f,.85f,.5f, -.85f,.85f,.5f};
        auto vertex=makeBuffer(sizeof(vertices),vertices);
        D3D12_VERTEX_BUFFER_VIEW vb{vertex->GetGPUVirtualAddress(),sizeof(vertices),12};
        uint16_t indices[]={0,1,2,3,4,5};
        auto index=makeBuffer(sizeof(indices),indices);
        D3D12_INDEX_BUFFER_VIEW ib{index->GetGPUVirtualAddress(),sizeof(indices),DXGI_FORMAT_R16_UINT};
        float box[18]={20,0,0,-1, 0,1/.35f,0,0, 0,0,1,0, 0,-.35f,-1,.1f,.35f,1};
        auto geometry=BuildGeometry(box,1,nullptr,0);
        auto nativeGeometry=MakeGeometryViews12(device.Get(),geometry);
        if(nativeGeometry.nodeCount!=geometry.nodes.size()/2)throw std::runtime_error("D3D12 geometry upload failed");
        auto nodes=makeBuffer(geometry.nodes.size()*16,geometry.nodes.data());
        auto primitives=makeBuffer(geometry.primitives.size()*16,geometry.primitives.data());
        uint32_t eligibility[]={1,1};auto mask=makeBuffer(sizeof(eligibility),eligibility);
        if(!LC_Register(index.Get(),eligibility,2))throw std::runtime_error("D3D12 private mesh registration failed");
        uint64_t registeredGeometry=LC_CreateGeometry(box,1,nullptr,0);
        float treeOrigin[]={-2.f,0.f,0.f};
        if(!registeredGeometry || !LC_BindGeometry(index.Get(),treeOrigin,registeredGeometry))
            throw std::runtime_error("D3D12 geometry binding failed");
        LC_ReleaseGeometry(registeredGeometry);
        auto nativeMask=MakeTypedBuffer12(device.Get(),eligibility,sizeof(eligibility),DXGI_FORMAT_R32_UINT,2);
        ClipConstants constants{};
        for(unsigned i=0;i<4;++i)constants.inverseViewProjection[i*4+i]=1;
        constants.viewport[2]=constants.viewport[3]=float(side);
        constants.tree[0]=-2;constants.boxCount=unsigned(geometry.nodes.size()/2);constants.enabled=constants.reserved=1;
        auto params=makeBuffer(sizeof(constants),&constants);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=3;
        hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> descriptors;
        Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors)),"SRV heap");
        unsigned stride=device->GetDescriptorHandleIncrementSize(hd.Type);
        auto makeSRV=[&](unsigned slot,ID3D12Resource* resource,DXGI_FORMAT format,unsigned count) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=format;view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Buffer.NumElements=count;
            auto handle=descriptors->GetCPUDescriptorHandleForHeapStart();handle.ptr+=size_t(slot)*stride;
            device->CreateShaderResourceView(resource,&view,handle);
        };
        makeSRV(0,primitives.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,unsigned(geometry.primitives.size()));
        makeSRV(1,nodes.Get(),DXGI_FORMAT_R32G32B32A32_UINT,unsigned(geometry.nodes.size()));
        makeSRV(2,mask.Get(),DXGI_FORMAT_R32_UINT,2);
        auto descriptorCPU=descriptors->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1,descriptorCPU,nativeGeometry.primitives.view,hd.Type);
        descriptorCPU.ptr+=stride;device->CopyDescriptorsSimple(1,descriptorCPU,nativeGeometry.nodes.view,hd.Type);
        descriptorCPU.ptr+=stride;device->CopyDescriptorsSimple(1,descriptorCPU,nativeMask.view,hd.Type);
        D3D12_DESCRIPTOR_HEAP_DESC extendedHeapDesc=hd;extendedHeapDesc.NumDescriptors=4;
        ComPtr<ID3D12DescriptorHeap> extendedHeap;
        Check(device->CreateDescriptorHeap(&extendedHeapDesc,IID_PPV_ARGS(&extendedHeap)),"extended SRV heap");
        auto extendedCPU=extendedHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{params->GetGPUVirtualAddress(),UINT((sizeof(constants)+255)&~size_t(255))};
        device->CreateConstantBufferView(&cbv,extendedCPU);
        extendedCPU.ptr+=stride;
        device->CopyDescriptorsSimple(3,extendedCPU,descriptors->GetCPUDescriptorHandleForHeapStart(),hd.Type);
        auto baseTables=DescribeRootTables12(baseCode->GetBufferPointer(),baseCode->GetBufferSize());
        RootArguments12 originalArguments;
        originalArguments.SetTable(0,descriptors->GetGPUDescriptorHandleForHeapStart());
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4> clipHandles{};
        for(unsigned i=0;i<4;++i) {
            clipHandles[i]=extendedHeap->GetCPUDescriptorHandleForHeapStart();
            clipHandles[i].ptr+=size_t(i)*stride;
        }
        auto copiedDescriptors=CopyDescriptors12(device.Get(),descriptors.Get(),baseTables,originalArguments,
            clipHandles,extension.firstClipParameter,extension.firstClipParameter+1);
        D3D12_RESOURCE_DESC targetDesc=textureDesc;targetDesc.Width=targetDesc.Height=side;
        targetDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};clear.Format=targetDesc.Format;
        ComPtr<ID3D12Resource> target;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&targetDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&target)),"render target");
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};rtvDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rtvDesc.NumDescriptors=1;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;Check(device->CreateDescriptorHeap(&rtvDesc,IID_PPV_ARGS(&rtvHeap)),"RTV heap");
        auto rtv=rtvHeap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target.Get(),nullptr,rtv);
        D3D12_RESOURCE_DESC readDesc{};readDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;readDesc.Width=side*side*4;
        readDesc.Height=1;readDesc.DepthOrArraySize=1;readDesc.MipLevels=1;readDesc.SampleDesc.Count=1;readDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES readHeap{};readHeap.Type=D3D12_HEAP_TYPE_READBACK;
        ComPtr<ID3D12Resource> readback;
        Check(device->CreateCommittedResource(&readHeap,D3D12_HEAP_FLAG_NONE,&readDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format=targetDesc.Format;dst.PlacedFootprint.Footprint.Width=dst.PlacedFootprint.Footprint.Height=side;
        dst.PlacedFootprint.Footprint.Depth=1;dst.PlacedFootprint.Footprint.RowPitch=side*4;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=target.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"queue");
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        ComPtr<ID3D12GraphicsCommandList> cmd;
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd)),"command list");
        ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
        HANDLE finished=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!finished)throw std::runtime_error("completion event");
        auto render=[&](ID3D12PipelineState* state,unsigned signal,bool expanded=false,bool base=false) {
            if(signal>1){Check(allocator->Reset(),"allocator reset");Check(cmd->Reset(allocator.Get(),nullptr),"command list reset");}
            D3D12_VIEWPORT viewport{0,0,float(side),float(side),0,1};D3D12_RECT scissor{0,0,LONG(side),LONG(side)};
            cmd->RSSetViewports(1,&viewport);cmd->RSSetScissorRects(1,&scissor);
            cmd->SetGraphicsRootSignature(base?baseRoot.Get():expanded?expandedRoot.Get():root.Get());cmd->SetPipelineState(state);
            auto heapPointer=expanded?copiedDescriptors.heap.Get():descriptors.Get();cmd->SetDescriptorHeaps(1,&heapPointer);
            if(expanded) {
                if(!copiedDescriptors.arguments.Replay(cmd.Get()))
                    throw std::runtime_error("root table replay failed");
            }else if(base) {
                cmd->SetGraphicsRootDescriptorTable(0,descriptors->GetGPUDescriptorHandleForHeapStart());
            }else {
                cmd->SetGraphicsRootConstantBufferView(0,params->GetGPUVirtualAddress());
                for(unsigned i=0;i<3;++i) {
                    auto handle=descriptors->GetGPUDescriptorHandleForHeapStart();handle.ptr+=uint64_t(i)*stride;
                    cmd->SetGraphicsRootDescriptorTable(i+1,handle);
                }
            }
            cmd->IASetVertexBuffers(0,1,&vb);cmd->IASetIndexBuffer(&ib);cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            float color[4]={0,0,0,0};cmd->OMSetRenderTargets(1,&rtv,FALSE,nullptr);cmd->ClearRenderTargetView(rtv,color,0,nullptr);
            if(signal==6) {
                D3D12_RECT top{0,0,LONG(side),LONG(side/2)};
                cmd->RSSetScissorRects(1,&top);
                cmd->DrawIndexedInstanced(6,1,0,0,0);
                LC_Enable12(0);
                D3D12_RECT bottom{0,LONG(side/2),LONG(side),LONG(side)};
                cmd->RSSetScissorRects(1,&bottom);
                // No root, pipeline or heap calls here: this draw exercises
                // restoration after the intercepted draw above.
                cmd->DrawIndexedInstanced(6,1,0,0,0);
            }else cmd->DrawIndexedInstanced(6,1,0,0,0);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource=target.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            cmd->ResourceBarrier(1,&barrier);cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);cmd->ResourceBarrier(1,&barrier);
            Check(cmd->Close(),"close command list");
            auto list=static_cast<ID3D12CommandList*>(cmd.Get());queue->ExecuteCommandLists(1,&list);
            Check(queue->Signal(fence.Get(),signal),"signal fence");
            if(fence->GetCompletedValue()<signal){Check(fence->SetEventOnCompletion(signal,finished),"set fence event");WaitForSingleObject(finished,5000);}
            if(fence->GetCompletedValue()<signal)throw std::runtime_error("GPU timeout");
            void* mapped=nullptr;D3D12_RANGE range{0,size_t(side*side*4)};
            Check(readback->Map(0,&range,&mapped),"map readback");
            std::vector<uint8_t> pixels(side*side*4);memcpy(pixels.data(),mapped,pixels.size());readback->Unmap(0,nullptr);
            return pixels;
        };
        float cameraInverse[16]{};for(unsigned i=0;i<4;++i)cameraInverse[i*4+i]=1;
        if(!LC_SetFallbackCamera12(cameraInverse,cameraInverse,side,side))
            throw std::runtime_error("D3D12 fallback camera update failed");
        int cameraToken=LC_QueueCamera(cameraInverse);
        if(!cameraToken)throw std::runtime_error("D3D12 camera scope queue failed");
        auto renderEvent=reinterpret_cast<void(__stdcall*)(int)>(LC_GetRenderEvent());
        renderEvent(cameraToken);
        auto baseline=render(originalPipeline.Get(),1),clipped=render(pipeline.Get(),2);
        auto expandedPixels=render(expandedPipeline.Get(),3,true);
        auto copiedPixels=render(copiedPipeline.Get(),4,true);
        auto basePixels=render(basePipeline.Get(),5,false,true);
        renderEvent(0);
        if(expandedPixels!=clipped)throw std::runtime_error("D3D12 expanded root changed clipped pixels");
        if(copiedPixels!=clipped)throw std::runtime_error("D3D12 captured pipeline changed clipped pixels");
        if(basePixels!=baseline)throw std::runtime_error("D3D12 base root changed baseline pixels");
        unsigned removed=0;
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
            unsigned at=(y*side+x)*4;
            bool expected=baseline[at+3]!=0 && x>=side/2 && y>=21 && y<=42;
            if(expected) {if(clipped[at+3]!=0)throw std::runtime_error("D3D12 obstructed pixel retained");++removed;}
            else if(memcmp(baseline.data()+at,clipped.data()+at,4)!=0)throw std::runtime_error("D3D12 unobstructed pixel changed");
        }
        if(removed<100)throw std::runtime_error("D3D12 image test did not cover obstruction");
        for(unsigned attempt=0;attempt<200;++attempt) {
            LC_GetStats12(counts,12);
            if(counts[8] || counts[9])break;
            Sleep(10);
        }
        if(counts[4]<5)throw std::runtime_error("D3D12 indexed draw interception missed WARP draws");
        if(counts[7]<5)throw std::runtime_error("D3D12 private indexed draw matching missed WARP draws");
        if(counts[8]<1 || counts[9]!=0)throw std::runtime_error("D3D12 background pipeline patch failed");
        if(counts[10]<5)throw std::runtime_error("D3D12 command queue interception missed WARP submissions");
        if(!LC_Enable12(1))throw std::runtime_error("D3D12 clipping activation failed");
        int clippingToken=LC_QueueCamera(cameraInverse);
        if(!clippingToken)throw std::runtime_error("D3D12 clipping camera scope failed");
        renderEvent(clippingToken);
        auto interceptedPixels=render(basePipeline.Get(),6,false,true);
        renderEvent(0);
        LC_Enable12(1);
        auto frameFallbackPixels=render(basePipeline.Get(),7,false,true);
        renderEvent(-1);
        auto afterFramePixels=render(basePipeline.Get(),8,false,true);
        LC_Enable12(0);
        CloseHandle(finished);
        for(unsigned y=0;y<side;++y) {
            const auto& expected=y<side/2?clipped:baseline;
            if(memcmp(interceptedPixels.data()+y*side*4,expected.data()+y*side*4,side*4)!=0)
                throw std::runtime_error("D3D12 intercepted draw or restored state differs from reference");
        }
        if(frameFallbackPixels!=clipped)
            throw std::runtime_error("D3D12 camera-frame fallback did not clip the inter-pass draw");
        if(afterFramePixels!=clipped)
            throw std::runtime_error("D3D12 full-resolution fallback missed a draw outside camera events");
        LC_GetStats12(counts,14);
        if(counts[12]<1 || counts[13]!=0)throw std::runtime_error("D3D12 intercepted draw did not clip cleanly");
        if(counts[11]<1)throw std::runtime_error("D3D12 draw resources were not retired after the fence");
        if(!LC_BindGeometry(index.Get(),treeOrigin,0))throw std::runtime_error("D3D12 geometry removal failed");
        LC_Unregister(index.Get());
        LC_Shutdown();
        std::cout<<"D3D12 WARP accepted patched DXBC vegetation-style pixel shader and root bindings\n";
        std::cout<<"D3D12 rendered "<<removed<<" clipped pixels; every surviving pixel matched the original\n";
        return 0;
    } catch(const std::exception& ex) {std::cerr<<ex.what()<<"\n";return 1;}
}
