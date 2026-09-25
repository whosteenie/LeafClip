#define LEAFCLIP_EXPORTS
#include "Native.h"
#include "ShaderPatch.h"
#include "Geometry.h"
#include "D3D12Stream.h"
#include "D3D12Root.h"
#include "D3D12PipelineCopy.h"
#include "D3D12Resources.h"
#include "D3D12RootArguments.h"
#include "D3D12DescriptorCopy.h"
#include "MinHook.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <atomic>
#include <array>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cstring>
#include <stdexcept>
extern "C" unsigned LC_D3D12GraphicsPSOSlot();
extern "C" unsigned LC_D3D12RootSignatureSlot();
extern "C" unsigned LC_D3D12PipelineStreamSlot();
extern "C" unsigned LC_D3D12DrawIndexedSlot();
extern "C" unsigned LC_D3D12ResetSlot();
extern "C" unsigned LC_D3D12SetPipelineSlot();
extern "C" unsigned LC_D3D12SetHeapsSlot();
extern "C" unsigned LC_D3D12SetRootSlot();
extern "C" unsigned LC_D3D12SetTableSlot();
extern "C" unsigned LC_D3D12SetConstantSlot();
extern "C" unsigned LC_D3D12SetConstantsSlot();
extern "C" unsigned LC_D3D12SetCBVSlot();
extern "C" unsigned LC_D3D12SetSRVSlot();
extern "C" unsigned LC_D3D12SetUAVSlot();
extern "C" unsigned LC_D3D12SetIndexSlot();
extern "C" unsigned LC_D3D12SetViewportsSlot();
extern "C" unsigned LC_D3D12SetTopologySlot();
extern "C" unsigned LC_D3D12ExecuteSlot();

namespace {
using namespace leafclip;
using CreatePSO12=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**);
using CreateRoot12=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,const void*,SIZE_T,REFIID,void**);
using CreateStream12=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
using DrawIndexed12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,INT,UINT);
using Reset12=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using SetPipeline12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
using SetHeaps12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,ID3D12DescriptorHeap* const*);
using SetRoot12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
using SetTable12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
using SetConstant12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT);
using SetConstants12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,const void*,UINT);
using SetAddress12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS);
using SetIndex12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,const D3D12_INDEX_BUFFER_VIEW*);
using SetViewports12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_VIEWPORT*);
using SetTopology12=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,D3D12_PRIMITIVE_TOPOLOGY);
using Execute12=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
ComPtr<ID3D12Device> device12;
ComPtr<ID3D12Device2> device12v2;
struct Root12 {ComPtr<ID3D12RootSignature> original,extended;unsigned firstClipParameter=0;std::vector<DescriptorTable12> tables;};
struct Pipeline12 {
    ComPtr<ID3D12PipelineState> original,clipped;
    std::shared_ptr<OwnedGraphicsPSO12> copy;
    std::shared_ptr<Root12> root;
    bool queued=false,failed=false;
};
struct Geometry12 {GeometryViews12 views;};
struct Target12 {
    ComPtr<ID3D12Resource> index;
    D3D12_GPU_VIRTUAL_ADDRESS address=0;
    uint64_t bytes=0;
    BufferView12 mask;
    std::shared_ptr<Geometry12> geometry;
    ClipConstants data{};
    unsigned triangleCount=0;
};
struct DrawPacket12 {
    BufferView12 constants;
    CopiedDescriptors12 descriptors;
    std::shared_ptr<Target12> target;
    std::shared_ptr<Geometry12> geometry;
    std::shared_ptr<Pipeline12> pipeline;
};
struct Queue12 {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    uint64_t next=0;
    std::deque<std::pair<uint64_t,std::vector<std::shared_ptr<DrawPacket12>>>> retired;
};
std::mutex mutex12;
std::mutex executeMutex12;
std::unordered_map<ID3D12RootSignature*,std::shared_ptr<Root12>> roots12;
std::unordered_map<ID3D12PipelineState*,std::shared_ptr<Pipeline12>> pipelines12;
std::deque<std::shared_ptr<Pipeline12>> jobs12;
std::condition_variable wake12;
std::thread worker12;
std::atomic<bool> runningWorker12{false};
std::unordered_map<ID3D12Resource*,std::shared_ptr<Target12>> targets12;
std::map<D3D12_GPU_VIRTUAL_ADDRESS,std::shared_ptr<Target12>> targetAddresses12;
std::unordered_map<uint64_t,std::shared_ptr<Geometry12>> geometries12;
std::unordered_map<ID3D12CommandQueue*,std::shared_ptr<Queue12>> queues12;
std::vector<std::shared_ptr<DrawPacket12>> orphanPackets12;
struct DrawState12 {
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> resources,samplers;
    RootArguments12 arguments;
    D3D12_INDEX_BUFFER_VIEW index{};
    D3D12_VIEWPORT viewport{};
    D3D12_PRIMITIVE_TOPOLOGY topology=D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bool hasIndex=false,hasViewport=false,complete=true,recordedSubmitted=false;
    std::vector<std::shared_ptr<DrawPacket12>> recorded;
};
std::unordered_map<ID3D12GraphicsCommandList*,std::shared_ptr<DrawState12>> drawStates12;
DrawState12& State12(ID3D12GraphicsCommandList* list) {
    auto& state=drawStates12[list];
    if(!state){state=std::make_shared<DrawState12>();state->list=list;}
    return *state;
}
uint64_t geometrySerial12=0;
std::atomic<bool> scope12{false};
float cameraViewportWidth12=0,cameraViewportHeight12=0; // protected by mutex12
std::array<float,16> activeInverse12{}; // protected by mutex, which also owns camera tokens
std::array<float,16> fallbackColorInverse12{},fallbackDepthInverse12{}; // protected by mutex
thread_local bool forwarding12=false;
CreatePSO12 originalPSO12=nullptr;
CreateRoot12 originalRoot12=nullptr;
CreateStream12 originalStream12=nullptr;
DrawIndexed12 originalDrawIndexed12=nullptr;
Reset12 originalReset12=nullptr;
SetPipeline12 originalSetPipeline12=nullptr;
SetHeaps12 originalSetHeaps12=nullptr;
SetRoot12 originalSetRoot12=nullptr;
SetTable12 originalSetTable12=nullptr;
SetConstant12 originalSetConstant12=nullptr;
SetConstants12 originalSetConstants12=nullptr;
SetAddress12 originalSetCBV12=nullptr,originalSetSRV12=nullptr,originalSetUAV12=nullptr;
SetIndex12 originalSetIndex12=nullptr;
SetViewports12 originalSetViewports12=nullptr;
SetTopology12 originalSetTopology12=nullptr;
Execute12 originalExecute12=nullptr;
std::atomic<bool> probing12{false};
std::atomic<bool> clipping12{false};
std::atomic<uint64_t> stats12[29]{}; // first 14 counters retain their ABI positions; later counters diagnose unmodified private draws
using CreatePS=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
using DrawIndexed=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using DrawIndexedInstanced=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
CreatePS originalCreate=nullptr;
DrawIndexed originalDraw[8]{};
DrawIndexedInstanced originalInstanced[8]{};
void* drawAddresses[8]{};
void* instancedAddresses[8]{};
unsigned drawHooks=0,instancedHooks=0;
thread_local bool forwarding=false;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> immediate;
ComPtr<ID3D11Buffer> constants;
std::atomic<bool> running{false};
bool scope=false; // render thread only
std::array<float,16> activeInverse{};
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
std::deque<std::string> messages;
struct Shader { std::vector<uint8_t> code; ComPtr<ID3D11PixelShader> patched; bool queued=false,done=false; };
struct Geometry { ComPtr<ID3D11ShaderResourceView> nodes,primitives; unsigned count=0; };
struct Target { ComPtr<ID3D11Buffer> index; ComPtr<ID3D11ShaderResourceView> mask; std::shared_ptr<Geometry> geometry; ClipConstants data{}; unsigned triangleCount=0; };
std::unordered_map<uint64_t,std::shared_ptr<Geometry>> geometries;
uint64_t geometrySerial=0;
std::unordered_map<uint64_t,std::shared_ptr<Shader>> shaders;
std::unordered_map<ID3D11Buffer*,std::shared_ptr<Target>> targets;
std::unordered_map<int,std::array<float,16>> cameras;
std::deque<std::shared_ptr<Shader>> jobs;
uint64_t serial=0;
size_t capturedBytes=0;
int cameraSerial=100;
std::atomic<uint64_t> stats[8]{}; // captured, built, rejected, matched, clipped, missing, unsupported draw, worker failures
const GUID shaderId={0x7132b186,0x52ef,0x4d26,{0xaf,0x25,0x1e,0x5a,0x99,0x20,0x54,0x31}};
void Log(const std::string& message) { std::lock_guard<std::mutex> lock(mutex); if(messages.size()<100) messages.push_back(message); }
void Check(HRESULT hr,const char* what) { if(FAILED(hr)) throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(uint32_t(hr))); }
template<class F> void Hook(void* object,unsigned slot,F replacement,F& original) {
    void** table=*reinterpret_cast<void***>(object);
    if(MH_CreateHook(table[slot],reinterpret_cast<void*>(replacement),reinterpret_cast<void**>(&original))!=MH_OK ||
        MH_EnableHook(table[slot])!=MH_OK) throw std::runtime_error("native function interception failed");
}
bool HasDXBC(const D3D12_SHADER_BYTECODE& code) {
    return code.pShaderBytecode && code.BytecodeLength>=32 && code.BytecodeLength<=16*1024*1024 &&
        memcmp(code.pShaderBytecode,"DXBC",4)==0;
}
std::string RootSummary12(const void* blob,SIZE_T length) {
    ComPtr<ID3D12VersionedRootSignatureDeserializer> parsed;
    if(!blob || FAILED(D3D12CreateVersionedRootSignatureDeserializer(blob,length,IID_PPV_ARGS(&parsed))))
        return "unreadable";
    const auto& versioned=parsed->GetUnconvertedRootSignatureDesc();
    unsigned parameters=0,staticSamplers=0;
    bool cb13=false,srv125=false,srv126=false,srv127=false;
    auto range=[&](D3D12_DESCRIPTOR_RANGE_TYPE type,UINT base,UINT count,UINT space) {
        if(space!=0 || count==0)return;
        auto covers=[&](UINT slot) {return base<=slot && (count==UINT_MAX || slot-base<count);};
        if(type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV)cb13|=covers(13);
        if(type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV) {
            srv125|=covers(125);srv126|=covers(126);srv127|=covers(127);
        }
    };
    if(versioned->Version==D3D_ROOT_SIGNATURE_VERSION_1_1) {
        const auto& d=versioned->Desc_1_1;
        parameters=d.NumParameters;staticSamplers=d.NumStaticSamplers;
        for(UINT i=0;i<d.NumParameters;++i) {
            const auto& p=d.pParameters[i];
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_CBV)range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,p.Descriptor.ShaderRegister,1,p.Descriptor.RegisterSpace);
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_SRV)range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,p.Descriptor.ShaderRegister,1,p.Descriptor.RegisterSpace);
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                for(UINT j=0;j<p.DescriptorTable.NumDescriptorRanges;++j) {
                    const auto& r=p.DescriptorTable.pDescriptorRanges[j];range(r.RangeType,r.BaseShaderRegister,r.NumDescriptors,r.RegisterSpace);
                }
        }
    }else if(versioned->Version==D3D_ROOT_SIGNATURE_VERSION_1_0) {
        const auto& d=versioned->Desc_1_0;
        parameters=d.NumParameters;staticSamplers=d.NumStaticSamplers;
        for(UINT i=0;i<d.NumParameters;++i) {
            const auto& p=d.pParameters[i];
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_CBV)range(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,p.Descriptor.ShaderRegister,1,p.Descriptor.RegisterSpace);
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_SRV)range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,p.Descriptor.ShaderRegister,1,p.Descriptor.RegisterSpace);
            if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                for(UINT j=0;j<p.DescriptorTable.NumDescriptorRanges;++j) {
                    const auto& r=p.DescriptorTable.pDescriptorRanges[j];range(r.RangeType,r.BaseShaderRegister,r.NumDescriptors,r.RegisterSpace);
                }
        }
    }
    return "parameters="+std::to_string(parameters)+", samplers="+std::to_string(staticSamplers)+
        ", b13="+std::to_string(cb13)+", t125-127="+std::to_string(srv125)+std::to_string(srv126)+std::to_string(srv127);
}
HRESULT STDMETHODCALLTYPE CapturePSO12(ID3D12Device* dev,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,REFIID iid,void** out) {
    HRESULT hr=originalPSO12(dev,desc,iid,out);
    if(probing12 && dev==device12.Get() && desc) {
        uint64_t count=++stats12[0];
        if(HasDXBC(desc->PS))++stats12[1];
        if(count<=12)Log("D3D12 graphics PSO: pixelDXBC="+std::to_string(HasDXBC(desc->PS))+", VSbytes="+
            std::to_string(desc->VS.BytecodeLength)+", PSbytes="+std::to_string(desc->PS.BytecodeLength)+
            ", root="+std::to_string(uintptr_t(desc->pRootSignature)));
        if(SUCCEEDED(hr) && out && *out && HasDXBC(desc->PS)) {
            try {
                auto record=std::make_shared<Pipeline12>();
                Check(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&record->original)),"query original pipeline");
                record->copy=std::make_shared<OwnedGraphicsPSO12>(*desc);
                std::lock_guard<std::mutex> lock(mutex12);
                pipelines12.emplace(record->original.Get(),std::move(record));
                ++stats12[6];
            }catch(const std::exception& ex) {if(count<=12)Log(std::string("D3D12 pipeline retained: ")+ex.what());}
        }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE CaptureRoot12(ID3D12Device* dev,UINT node,const void* blob,SIZE_T length,REFIID iid,void** out) {
    HRESULT hr=originalRoot12(dev,node,blob,length,iid,out);
    if(probing12 && dev==device12.Get() && SUCCEEDED(hr) && out && *out) {
        uint64_t count=++stats12[2];
        if(count<=12) Log("D3D12 root signature created: id="+
            std::to_string(uintptr_t(*out))+", "+RootSummary12(blob,length));
        try {
            auto added=ExtendRoot12(blob,length);
            if(!added.serialized.empty()) {
                auto record=std::make_shared<Root12>();
                record->tables=DescribeRootTables12(blob,length);
                Check(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&record->original)),"query original root");
                Check(originalRoot12(dev,node,added.serialized.data(),added.serialized.size(),
                    IID_PPV_ARGS(&record->extended)),"create extended root");
                record->firstClipParameter=added.firstClipParameter;
                {
                    std::lock_guard<std::mutex> lock(mutex12);
                    roots12.emplace(record->original.Get(),std::move(record));
                    ++stats12[5];
                }
                if(count<=12)Log("D3D12 extended root ready: original="+std::to_string(uintptr_t(*out))+
                    ", appendedParameters="+std::to_string(added.firstClipParameter)+"/"+
                    std::to_string(added.firstClipParameter+1));
            }else if(count<=12)Log("D3D12 root exceeds extension budget; keeping original.");
        }catch(const std::exception& ex) {if(count<=12)Log(std::string("D3D12 root retained: ")+ex.what());}
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE CaptureStream12(ID3D12Device2* dev,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,REFIID iid,void** out) {
    HRESULT hr=originalStream12(dev,desc,iid,out);
    if(probing12 && dev==device12v2.Get()) {
        uint64_t count=++stats12[3];
        auto info=ReadPipelineStream(desc);
        if(info.valid && HasDXBC(info.pixel)) ++stats12[1];
        if(count<=12) Log("D3D12 stream PSO: parsed="+std::to_string(info.valid)+
            ", pixelDXBC="+std::to_string(HasDXBC(info.pixel))+
            ", PSbytes="+std::to_string(info.pixel.BytecodeLength)+
            ", root="+std::to_string(uintptr_t(info.root)));
    }
    return hr;
}
void LogShaderInterface12(const char* stage,const D3D12_SHADER_BYTECODE& code) {
    ComPtr<ID3D11ShaderReflection> shader;
    if(!code.pShaderBytecode || FAILED(D3DReflect(code.pShaderBytecode,code.BytecodeLength,IID_PPV_ARGS(&shader)))) {
        Log(std::string("D3D12 ")+stage+" reflection unavailable.");return;
    }
    D3D11_SHADER_DESC desc{};
    if(FAILED(shader->GetDesc(&desc)))return;
    std::string info=std::string("D3D12 ")+stage+" interface: input=";
    for(unsigned i=0;i<desc.InputParameters;++i) {
        D3D11_SIGNATURE_PARAMETER_DESC p{};
        if(SUCCEEDED(shader->GetInputParameterDesc(i,&p)))
            info+=std::string(p.SemanticName)+std::to_string(p.SemanticIndex)+"@"+std::to_string(p.Register)+"/"+std::to_string(p.Mask)+", ";
    }
    info+=" output=";
    for(unsigned i=0;i<desc.OutputParameters;++i) {
        D3D11_SIGNATURE_PARAMETER_DESC p{};
        if(SUCCEEDED(shader->GetOutputParameterDesc(i,&p)))
            info+=std::string(p.SemanticName)+std::to_string(p.SemanticIndex)+"@"+std::to_string(p.Register)+"/"+std::to_string(p.Mask)+", ";
    }
    Log(info);
}
void PreparePipelines12() {
    for(;;) {
        std::shared_ptr<Pipeline12> job;
        {
            std::unique_lock<std::mutex> lock(mutex12);
            wake12.wait(lock,[]{return !runningWorker12 || !jobs12.empty();});
            if(!runningWorker12)return;
            job=jobs12.front();jobs12.pop_front();
        }
        ComPtr<ID3D12PipelineState> clipped;
        std::string error;
        try {
            auto pixel=job->copy->PixelShader();
            static std::atomic<unsigned> inspected{0};
            if(inspected.fetch_add(1)<4) {
                auto original=job->copy->Describe(job->root->original.Get());
                LogShaderInterface12("VS",original.VS);
                LogShaderInterface12("PS",pixel);
            }
            std::vector<uint8_t> patched;
            try {patched=PatchPixelShader(pixel.pShaderBytecode,pixel.BytecodeLength,true);}
            catch(const std::exception& ex) {
                Log(std::string("D3D12 animated-position varying unavailable: ")+ex.what());
                patched=PatchPixelShader(pixel.pShaderBytecode,pixel.BytecodeLength);
            }
            auto desc=job->copy->Describe(job->root->extended.Get(),patched.data(),patched.size());
            Check(originalPSO12(device12.Get(),&desc,IID_PPV_ARGS(&clipped)),"create D3D12 clipped pipeline");
        }catch(const std::exception& ex){error=ex.what();}
        bool failed=!clipped;
        {
            std::lock_guard<std::mutex> lock(mutex12);
            job->clipped=std::move(clipped);job->failed=failed;
        }
        if(failed){++stats12[9];Log("D3D12 original pipeline retained: "+error);}
        else {++stats12[8];Log("D3D12 clipped pipeline prepared.");}
    }
}
void DrainCompleted12() {
    std::lock_guard<std::mutex> lock(mutex12);
    for(auto& entry:queues12) {
        auto& q=*entry.second;
        uint64_t complete=q.fence?q.fence->GetCompletedValue():0;
        while(!q.retired.empty() && q.retired.front().first<=complete) {
            stats12[11]+=q.retired.front().second.size();
            q.retired.pop_front();
        }
    }
}
void STDMETHODCALLTYPE CaptureExecute12(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists) {
    std::lock_guard<std::mutex> order(executeMutex12);
    std::shared_ptr<Queue12> state;
    std::vector<std::shared_ptr<DrawPacket12>> packets;
    if(probing12)try {
        std::lock_guard<std::mutex> lock(mutex12);
        auto& found=queues12[queue];
        if(!found) {
            found=std::make_shared<Queue12>();found->queue=queue;
            Check(device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&found->fence)),
                "create D3D12 completion fence");
        }
        state=found;
        for(UINT i=0;i<count && lists;++i) {
            auto list=reinterpret_cast<ID3D12GraphicsCommandList*>(lists[i]);
            auto draw=drawStates12.find(list);
            if(draw!=drawStates12.end()) {
                packets.insert(packets.end(),draw->second->recorded.begin(),draw->second->recorded.end());
                draw->second->recordedSubmitted=true;
            }
        }
    }catch(const std::exception& ex){Log(std::string("D3D12 queue fence unavailable: ")+ex.what());}
    originalExecute12(queue,count,lists);
    if(state) {
        uint64_t value=++state->next;
        HRESULT hr=queue->Signal(state->fence.Get(),value);
        if(SUCCEEDED(hr)) {
            ++stats12[10];
            if(!packets.empty()) {
                std::lock_guard<std::mutex> lock(mutex12);
                state->retired.emplace_back(value,std::move(packets));
            }
            DrainCompleted12();
        }else {
            std::lock_guard<std::mutex> lock(mutex12);
            orphanPackets12.insert(orphanPackets12.end(),packets.begin(),packets.end());
            Log("D3D12 queue signal failed; keeping draw resources until process exit.");
        }
    }
}
HRESULT STDMETHODCALLTYPE CaptureReset12(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* initial) {
    HRESULT hr=originalReset12(list,allocator,initial);
    if(probing12 && SUCCEEDED(hr) && !forwarding12) {
        std::lock_guard<std::mutex> lock(mutex12);
        auto& s=State12(list);
        if(!s.recordedSubmitted)orphanPackets12.insert(orphanPackets12.end(),s.recorded.begin(),s.recorded.end());
        s.recorded.clear();s.recordedSubmitted=false;
        s.pipeline=initial;s.root.Reset();s.resources.Reset();s.samplers.Reset();
        s.arguments.Clear();s.hasIndex=s.hasViewport=false;s.topology=D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;s.complete=true;
    }
    return hr;
}
void STDMETHODCALLTYPE CaptureSetPipeline12(ID3D12GraphicsCommandList* list,ID3D12PipelineState* value) {
    originalSetPipeline12(list,value);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);State12(list).pipeline=value;}
}
void STDMETHODCALLTYPE CaptureSetHeaps12(ID3D12GraphicsCommandList* list,UINT count,ID3D12DescriptorHeap* const* heaps) {
    originalSetHeaps12(list,count,heaps);
    if(!probing12 || forwarding12)return;
    ComPtr<ID3D12DescriptorHeap> resources,samplers;
    if(heaps)for(UINT i=0;i<count && i<2;++i)if(heaps[i]) {
        if(heaps[i]->GetDesc().Type==D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)resources=heaps[i];
        if(heaps[i]->GetDesc().Type==D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)samplers=heaps[i];
    }
    std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);
    if(s.resources.Get()!=resources.Get() || s.samplers.Get()!=samplers.Get())s.arguments.ClearTables();
    s.resources=std::move(resources);s.samplers=std::move(samplers);
}
void STDMETHODCALLTYPE CaptureSetRoot12(ID3D12GraphicsCommandList* list,ID3D12RootSignature* value) {
    originalSetRoot12(list,value);
    if(!probing12 || forwarding12)return;
    std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);
    if(s.root.Get()!=value)s.arguments.Clear();s.root=value;
}
void STDMETHODCALLTYPE CaptureSetTable12(ID3D12GraphicsCommandList* list,UINT slot,D3D12_GPU_DESCRIPTOR_HANDLE value) {
    originalSetTable12(list,slot,value);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.complete&=s.arguments.SetTable(slot,value);}
}
void STDMETHODCALLTYPE CaptureSetConstant12(ID3D12GraphicsCommandList* list,UINT slot,UINT value,UINT offset) {
    originalSetConstant12(list,slot,value,offset);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.complete&=s.arguments.SetConstants(slot,offset,&value,1);}
}
void STDMETHODCALLTYPE CaptureSetConstants12(ID3D12GraphicsCommandList* list,UINT slot,UINT count,const void* values,UINT offset) {
    originalSetConstants12(list,slot,count,values,offset);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.complete&=s.arguments.SetConstants(slot,offset,static_cast<const uint32_t*>(values),count);}
}
void STDMETHODCALLTYPE CaptureSetCBV12(ID3D12GraphicsCommandList* list,UINT slot,D3D12_GPU_VIRTUAL_ADDRESS value) {
    originalSetCBV12(list,slot,value);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.complete&=s.arguments.SetCBV(slot,value);}
}
void STDMETHODCALLTYPE CaptureSetSRV12(ID3D12GraphicsCommandList* list,UINT slot,D3D12_GPU_VIRTUAL_ADDRESS value) {
    originalSetSRV12(list,slot,value);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.complete&=s.arguments.SetSRV(slot,value);}
}
void STDMETHODCALLTYPE CaptureSetUAV12(ID3D12GraphicsCommandList* list,UINT slot,D3D12_GPU_VIRTUAL_ADDRESS value) {
    originalSetUAV12(list,slot,value);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.complete&=s.arguments.SetUAV(slot,value);}
}
void STDMETHODCALLTYPE CaptureSetIndex12(ID3D12GraphicsCommandList* list,const D3D12_INDEX_BUFFER_VIEW* value) {
    originalSetIndex12(list,value);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.hasIndex=value!=nullptr;if(value)s.index=*value;}
}
void STDMETHODCALLTYPE CaptureSetViewports12(ID3D12GraphicsCommandList* list,UINT count,const D3D12_VIEWPORT* values) {
    originalSetViewports12(list,count,values);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);auto& s=State12(list);s.hasViewport=count==1 && values;if(s.hasViewport)s.viewport=*values;}
}
void STDMETHODCALLTYPE CaptureSetTopology12(ID3D12GraphicsCommandList* list,D3D12_PRIMITIVE_TOPOLOGY value) {
    originalSetTopology12(list,value);
    if(probing12 && !forwarding12){std::lock_guard<std::mutex> lock(mutex12);State12(list).topology=value;}
}
struct Snapshot12 {
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> resources,samplers;
    RootArguments12 arguments;
    D3D12_INDEX_BUFFER_VIEW index{};
    D3D12_VIEWPORT viewport{};
};
bool TryClipDraw12(ID3D12GraphicsCommandList* list,UINT indices,UINT instances,
    UINT firstIndex,INT baseVertex,UINT firstInstance) {
    if(!indices)return false;
    const bool inScope=scope12.load();
    if(inScope && stats12[12]>stats12[11]+2048)return false;
    Snapshot12 snapshot;
    std::shared_ptr<Target12> target;
    std::shared_ptr<Geometry12> geometry;
    std::shared_ptr<Pipeline12> pipeline;
    std::shared_ptr<Root12> root;
    ClipConstants data{};
    bool colorDraw=false;
    {
        std::lock_guard<std::mutex> lock(mutex12);
        auto state=drawStates12.find(list);
        if(state==drawStates12.end() || !state->second->hasIndex)return false;
        const auto& view=state->second->index;
        auto end=targetAddresses12.upper_bound(view.BufferLocation);
        if(end==targetAddresses12.begin())return false;
        target=std::prev(end)->second;
        unsigned stride=view.Format==DXGI_FORMAT_R16_UINT?2:view.Format==DXGI_FORMAT_R32_UINT?4:0;
        if(!stride || view.BufferLocation<target->address)return false;
        uint64_t offset=view.BufferLocation-target->address;
        uint64_t start=offset/stride+firstIndex;
        if(offset>=target->bytes || offset%stride || view.SizeInBytes>target->bytes-offset ||
           (uint64_t(firstIndex)+indices)*stride>view.SizeInBytes || start%3 ||
           start+indices>uint64_t(target->triangleCount)*3)return false;
        ++stats12[14];
        if(instances!=1){
            uint64_t count=++stats12[20];
            if(count<=4)Log("D3D12 private indexed draw has instances="+std::to_string(instances)+
                ", viewport="+std::to_string(state->second->viewport.Width)+"x"+
                std::to_string(state->second->viewport.Height));
            return false;
        }
        if(indices%3){++stats12[21];return false;}
        if(!inScope){
            uint64_t missed=++stats12[15];
            const bool shadowSize=state->second->viewport.Width==2048 && state->second->viewport.Height==2048;
            uint64_t viewportCount=++stats12[shadowSize?18:19];
            if(missed<=4)Log("D3D12 private draw outside camera scope: viewport="+
                std::to_string(state->second->viewport.Width)+"x"+
                std::to_string(state->second->viewport.Height)+
                ", indices="+std::to_string(indices)+
                ", root="+std::to_string(uintptr_t(state->second->root.Get())));
            if(!shadowSize && viewportCount<=8)Log("D3D12 non-shadow-sized private draw outside camera scope: viewport="+
                std::to_string(state->second->viewport.Width)+"x"+
                std::to_string(state->second->viewport.Height)+
                ", indices="+std::to_string(indices)+
                ", root="+std::to_string(uintptr_t(state->second->root.Get())));
        }
        if(!state->second->complete || !state->second->hasViewport ||
           state->second->topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ||
           !state->second->pipeline || !state->second->root){++stats12[16];return false;}
        if(!target->geometry){++stats12[17];return false;}
        geometry=target->geometry;
        if(!geometry->views.nodeCount)return false;
        uint64_t matched=++stats12[7];
        if(matched<=2)Log("D3D12 matched private indexed draw: indices="+
            std::to_string(indices)+", first="+std::to_string(firstIndex)+
            ", root="+std::to_string(uintptr_t(state->second->root.Get())));
        auto psos=pipelines12.find(state->second->pipeline.Get());
        auto roots=roots12.find(state->second->root.Get());
        const bool fullResolution=state->second->viewport.Width!=2048 || state->second->viewport.Height!=2048;
        if(psos==pipelines12.end()){
            if(fullResolution){uint64_t n=++stats12[25];if(n<=4)Log("D3D12 uncaptured private pipeline: viewport="+
                std::to_string(state->second->viewport.Width)+"x"+std::to_string(state->second->viewport.Height)+
                ", indices="+std::to_string(indices));}
            return false;
        }
        if(roots==roots12.end()){
            if(fullResolution){uint64_t n=++stats12[26];if(n<=4)Log("D3D12 uncaptured private root: viewport="+
                std::to_string(state->second->viewport.Width)+"x"+std::to_string(state->second->viewport.Height)+
                ", indices="+std::to_string(indices));}
            return false;
        }
        if(psos->second->copy->OriginalRoot()!=state->second->root.Get()){
            if(fullResolution)++stats12[27];return false;
        }
        pipeline=psos->second;root=roots->second;
        const bool colorPipeline=pipeline->copy->Describe(nullptr,nullptr,0).NumRenderTargets!=0;
        colorDraw=colorPipeline;
        // Main-camera depth must be clipped too. Shadow-map passes have a
        // different viewport aspect in Valheim and cannot use the camera VP.
        if(!inScope){
            const float width=state->second->viewport.Width,height=state->second->viewport.Height;
            if(cameraViewportWidth12<=0 || cameraViewportHeight12<=0 || height<=0 ||
               std::abs(width/height-cameraViewportWidth12/cameraViewportHeight12)>0.015f){
                ++stats12[colorPipeline?23:24];return false;
            }
        }
        if(!pipeline->queued) {
            pipeline->root=root;pipeline->queued=true;
            jobs12.push_back(pipeline);wake12.notify_one();
        }
        if(!clipping12 || !pipeline->clipped || !stats12[10] ||
           state->second->viewport.Width<=0 || state->second->viewport.Height<=0 ||
           state->second->viewport.MinDepth!=0 || state->second->viewport.MaxDepth!=1){
            if(fullResolution)++stats12[28];return false;
        }
        snapshot.pipeline=state->second->pipeline;snapshot.root=state->second->root;
        snapshot.resources=state->second->resources;snapshot.samplers=state->second->samplers;
        snapshot.arguments=state->second->arguments;snapshot.index=view;
        snapshot.viewport=state->second->viewport;
        data=target->data;
        data.triangleBase=UINT(start/3);
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto& inverse=inScope?activeInverse12:(colorDraw?fallbackColorInverse12:fallbackDepthInverse12);
        memcpy(data.inverseViewProjection,inverse.data(),64);
    }
    data.viewport[0]=snapshot.viewport.TopLeftX;data.viewport[1]=snapshot.viewport.TopLeftY;
    data.viewport[2]=snapshot.viewport.Width;data.viewport[3]=snapshot.viewport.Height;
    data.enabled=1;
    auto packet=std::make_shared<DrawPacket12>();
    packet->target=target;packet->geometry=geometry;packet->pipeline=pipeline;
    packet->constants=MakeClipConstants12(device12.Get(),data);
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,4> clip={
        packet->constants.view,geometry->views.primitives.view,geometry->views.nodes.view,target->mask.view};
    packet->descriptors=CopyDescriptors12(device12.Get(),snapshot.resources.Get(),root->tables,
        snapshot.arguments,clip,root->firstClipParameter,root->firstClipParameter+1);
    // Retain every draw resource before issuing commands; command list state
    // alone does not own the descriptors after this function returns.
    {
        std::lock_guard<std::mutex> lock(mutex12);
        auto state=drawStates12.find(list);
        if(state==drawStates12.end())return false;
        state->second->recorded.push_back(packet);
    }
    struct ForwardGuard {ForwardGuard(){forwarding12=true;}~ForwardGuard(){forwarding12=false;}} guard;
    ID3D12DescriptorHeap* active[]={packet->descriptors.heap.Get(),snapshot.samplers.Get()};
    list->SetDescriptorHeaps(snapshot.samplers?2:1,active);
    list->SetGraphicsRootSignature(root->extended.Get());
    packet->descriptors.arguments.Replay(list);
    list->SetPipelineState(pipeline->clipped.Get());
    originalDrawIndexed12(list,indices,instances,firstIndex,baseVertex,firstInstance);
    ID3D12DescriptorHeap* restore[]={snapshot.resources.Get(),snapshot.samplers.Get()};
    UINT heapCount=0;
    if(snapshot.resources)++heapCount;
    if(snapshot.samplers){if(!snapshot.resources)restore[0]=snapshot.samplers.Get();++heapCount;}
    list->SetDescriptorHeaps(heapCount,heapCount?restore:nullptr);
    list->SetGraphicsRootSignature(snapshot.root.Get());
    snapshot.arguments.Replay(list);
    list->SetPipelineState(snapshot.pipeline.Get());
    ++stats12[12];
    if(!inScope)++stats12[22];
    return true;
}
void STDMETHODCALLTYPE CaptureDrawIndexed12(ID3D12GraphicsCommandList* list,UINT indices,UINT instances,
    UINT firstIndex,INT baseVertex,UINT firstInstance) {
    if(probing12 && !forwarding12) {
        ++stats12[4];
        try {if(TryClipDraw12(list,indices,instances,firstIndex,baseVertex,firstInstance))return;}
        catch(const std::exception& ex){
            uint64_t failures=++stats12[13];
            if(failures<=3)Log(std::string("D3D12 original draw retained: ")+ex.what());
        }
    }
    originalDrawIndexed12(list,indices,instances,firstIndex,baseVertex,firstInstance);
}
HRESULT STDMETHODCALLTYPE CapturePS(ID3D11Device* dev,const void* bytes,SIZE_T length,ID3D11ClassLinkage* linkage,ID3D11PixelShader** out) {
    HRESULT hr=originalCreate(dev,bytes,length,linkage,out);
    if(SUCCEEDED(hr) && out && *out && running && dev==device.Get() && !linkage) {
        try {
            auto record=std::make_shared<Shader>();
            std::lock_guard<std::mutex> lock(mutex);
            if(capturedBytes+length>64*1024*1024) return hr;
            record->code.assign((const uint8_t*)bytes,(const uint8_t*)bytes+length);
            uint64_t id=++serial;
            if(SUCCEEDED((*out)->SetPrivateData(shaderId,sizeof(id),&id))) {
                shaders.emplace(id,std::move(record)); capturedBytes+=length; ++stats[0];
            }
        } catch(...) { ++stats[7]; }
    }
    return hr;
}
void Work() {
    for(;;) {
        std::shared_ptr<Shader> job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock,[]{return !running || !jobs.empty();});
            if(!running) return;
            job=jobs.front(); jobs.pop_front();
        }
        ComPtr<ID3D11PixelShader> result;
        try {
            auto bytes=PatchPixelShader(job->code.data(),job->code.size());
            Check(originalCreate(device.Get(),bytes.data(),bytes.size(),nullptr,&result),"create patched pixel shader");
            ++stats[1]; Log("Prepared a post-wind pixel shader variant.");
        } catch(const std::exception& ex) {++stats[2]; Log(std::string("Original shader retained: ")+ex.what());}
        {
            std::lock_guard<std::mutex> lock(mutex);
            job->patched=std::move(result); job->done=true;
        }
    }
}
// Scope restores all slots touched by the patch before returning to Unity.
struct DrawScope {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11PixelShader> shader;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11ShaderResourceView> srv[3];
    bool changed=false;
    explicit DrawScope(ID3D11DeviceContext* c):context(c) {}
    ~DrawScope() {
        if(!changed) return;
        context->PSSetShader(shader.Get(),nullptr,0);
        auto b=cb.Get(); context->PSSetConstantBuffers(13,1,&b);
        ID3D11ShaderResourceView* views[]={srv[0].Get(),srv[1].Get(),srv[2].Get()}; context->PSSetShaderResources(125,3,views);
    }
    void Apply(UINT indexCount,UINT firstIndex,UINT instances) {
        if(!running || !scope || context!=immediate.Get()) return;
        ComPtr<ID3D11Buffer> index; DXGI_FORMAT format; UINT offset=0;
        context->IAGetIndexBuffer(&index,&format,&offset);
        std::shared_ptr<Target> target;
        std::shared_ptr<Geometry> geometry;
        ClipConstants data;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto found=targets.find(index.Get()); if(found==targets.end()) return;
            target=found->second; data=target->data; geometry=target->geometry;
        }
        ++stats[3];
        D3D11_PRIMITIVE_TOPOLOGY topology;
        context->IAGetPrimitiveTopology(&topology);
        ComPtr<ID3D11GeometryShader> gs; context->GSGetShader(&gs,nullptr,nullptr);
        ComPtr<ID3D11HullShader> hs; context->HSGetShader(&hs,nullptr,nullptr);
        if(instances!=1 || offset!=0 || firstIndex%3 || indexCount%3 ||
            topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST || gs || hs ||
            uint64_t(firstIndex)+indexCount>uint64_t(target->triangleCount)*3) {++stats[6]; return;}
        if(data.boxCount==0) return;
        UINT classCount=0;
        context->PSGetShader(&shader,nullptr,&classCount);
        if(!shader || classCount) {++stats[6]; return;}
        uint64_t id=0; UINT idSize=sizeof(id);
        if(FAILED(shader->GetPrivateData(shaderId,&idSize,&id))) {++stats[5]; return;}
        ComPtr<ID3D11PixelShader> patched;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto found=shaders.find(id); if(found==shaders.end()) {++stats[5]; return;}
            auto record=found->second;
            if(!record->queued) {record->queued=true; jobs.push_back(record); wake.notify_one();}
            patched=record->patched;
        }
        if(!patched) return;
        UINT count=1; D3D11_VIEWPORT viewport{}; context->RSGetViewports(&count,&viewport);
        if(count!=1 || viewport.Width<=0 || viewport.Height<=0 || viewport.MinDepth!=0 || viewport.MaxDepth!=1) {++stats[6]; return;}
        memcpy(data.inverseViewProjection,activeInverse.data(),64);
        data.viewport[0]=viewport.TopLeftX; data.viewport[1]=viewport.TopLeftY;
        data.viewport[2]=viewport.Width; data.viewport[3]=viewport.Height;
        data.triangleBase=firstIndex/3; data.enabled=1;
        context->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
        context->PSGetConstantBuffers(13,1,&cb);
        for(unsigned i=0;i<3;++i)context->PSGetShaderResources(125+i,1,&srv[i]);
        changed=true;
        auto buffer=constants.Get(); context->PSSetConstantBuffers(13,1,&buffer);
        ID3D11ShaderResourceView* resources[]={geometry?geometry->primitives.Get():nullptr,geometry?geometry->nodes.Get():nullptr,target->mask.Get()};
        context->PSSetShaderResources(125,3,resources);
        context->PSSetShader(patched.Get(),nullptr,0); ++stats[4];
    }
};
template<unsigned Slot> void STDMETHODCALLTYPE OnDraw(ID3D11DeviceContext* c,UINT count,UINT start,INT base) {
    if(forwarding){originalDraw[Slot](c,count,start,base);return;}
    forwarding=true;
    {
    DrawScope state(c);
    try {state.Apply(count,start,1);} catch(...) {++stats[7];}
    originalDraw[Slot](c,count,start,base);
    }
    forwarding=false;
}
template<unsigned Slot> void STDMETHODCALLTYPE OnInstanced(ID3D11DeviceContext* c,UINT count,UINT instances,UINT start,INT base,UINT firstInstance) {
    if(forwarding){originalInstanced[Slot](c,count,instances,start,base,firstInstance);return;}
    forwarding=true;
    {
    DrawScope state(c);
    try {state.Apply(count,start,instances);} catch(...) {++stats[7];}
    originalInstanced[Slot](c,count,instances,start,base,firstInstance);
    }
    forwarding=false;
}
const DrawIndexed drawCallbacks[]={OnDraw<0>,OnDraw<1>,OnDraw<2>,OnDraw<3>,OnDraw<4>,OnDraw<5>,OnDraw<6>,OnDraw<7>};
const DrawIndexedInstanced instancedCallbacks[]={OnInstanced<0>,OnInstanced<1>,OnInstanced<2>,OnInstanced<3>,OnInstanced<4>,OnInstanced<5>,OnInstanced<6>,OnInstanced<7>};
void EnsureDrawHooks() {
    // The runtime can switch from validation to optimized dispatch functions as
    // state changes. Detour implementations, never its writable dispatch table.
    void** table=*reinterpret_cast<void***>(immediate.Get());
    bool found=false;for(unsigned i=0;i<drawHooks;++i)found|=drawAddresses[i]==table[12];
    if(!found) {
        if(drawHooks==8)throw std::runtime_error("draw dispatch variant limit");
        void* address=table[12];Hook(immediate.Get(),12,drawCallbacks[drawHooks],originalDraw[drawHooks]);drawAddresses[drawHooks++]=address;
    }
    found=false;for(unsigned i=0;i<instancedHooks;++i)found|=instancedAddresses[i]==table[20];
    if(!found) {
        if(instancedHooks==8)throw std::runtime_error("instanced dispatch variant limit");
        void* address=table[20];Hook(immediate.Get(),20,instancedCallbacks[instancedHooks],originalInstanced[instancedHooks]);instancedAddresses[instancedHooks++]=address;
    }
}
void __stdcall RenderEvent(int id) {
    scope=false;scope12=false;
    if(id==0 || (!running && !probing12))return;
    if(running)try {EnsureDrawHooks();}catch(const std::exception& ex){Log(ex.what());return;}
    std::lock_guard<std::mutex> lock(mutex);
    auto found=cameras.find(id);
    if(found!=cameras.end()) {
        if(running){activeInverse=found->second;scope=true;}
        if(probing12){activeInverse12=found->second;scope12=true;}
        cameras.erase(found);
    }
}
}
int LC_GetApiVersion() {return 6;}
int LC_Enable12(int enabled) {
    if(!probing12)return 0;
    clipping12=enabled!=0;
    Log(enabled?"D3D12 draw clipping enabled.":"D3D12 draw clipping disabled.");
    return 1;
}
int LC_SetFallbackCamera12(const float* backbuffer,const float* texture,int width,int height) {
    if(!probing12 || !backbuffer || !texture || width<=0 || height<=0)return 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        memcpy(fallbackColorInverse12.data(),backbuffer,64);
        memcpy(fallbackDepthInverse12.data(),texture,64);
    }
    {
        std::lock_guard<std::mutex> lock(mutex12);
        cameraViewportWidth12=float(width);cameraViewportHeight12=float(height);
    }
    return 1;
}
int LC_Initialize12(void* resource) {
    if(probing12)return 1;
    try {
        if(!resource || device || device12)return 0;
        ComPtr<ID3D12Resource> texture;
        Check(static_cast<IUnknown*>(resource)->QueryInterface(IID_PPV_ARGS(&texture)),"query D3D12 texture");
        Check(texture->GetDevice(IID_PPV_ARGS(&device12)),"get D3D12 device");
        HMODULE pin=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&LC_Initialize12),&pin))throw std::runtime_error("native module pin failed");
        MH_STATUS status=MH_Initialize();
        if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("MinHook initialization failed");
        Hook(device12.Get(),LC_D3D12GraphicsPSOSlot(),CapturePSO12,originalPSO12);
        Hook(device12.Get(),LC_D3D12RootSignatureSlot(),CaptureRoot12,originalRoot12);
        if(SUCCEEDED(device12.As(&device12v2))) Hook(device12v2.Get(),LC_D3D12PipelineStreamSlot(),CaptureStream12,originalStream12);
        // Detour the implementation through a temporary list. Unity may have
        // created its lists before the managed plugin loads, so hooking only
        // future CreateCommandList calls would miss all existing draws.
        ComPtr<ID3D12CommandAllocator> allocator12;
        Check(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator12)),"create D3D12 probe allocator");
        ComPtr<ID3D12GraphicsCommandList> list12;
        Check(device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator12.Get(),nullptr,
            IID_PPV_ARGS(&list12)),"create D3D12 probe command list");
        Hook(list12.Get(),LC_D3D12ResetSlot(),CaptureReset12,originalReset12);
        Hook(list12.Get(),LC_D3D12SetPipelineSlot(),CaptureSetPipeline12,originalSetPipeline12);
        Hook(list12.Get(),LC_D3D12SetHeapsSlot(),CaptureSetHeaps12,originalSetHeaps12);
        Hook(list12.Get(),LC_D3D12SetRootSlot(),CaptureSetRoot12,originalSetRoot12);
        Hook(list12.Get(),LC_D3D12SetTableSlot(),CaptureSetTable12,originalSetTable12);
        Hook(list12.Get(),LC_D3D12SetConstantSlot(),CaptureSetConstant12,originalSetConstant12);
        Hook(list12.Get(),LC_D3D12SetConstantsSlot(),CaptureSetConstants12,originalSetConstants12);
        Hook(list12.Get(),LC_D3D12SetCBVSlot(),CaptureSetCBV12,originalSetCBV12);
        Hook(list12.Get(),LC_D3D12SetSRVSlot(),CaptureSetSRV12,originalSetSRV12);
        Hook(list12.Get(),LC_D3D12SetUAVSlot(),CaptureSetUAV12,originalSetUAV12);
        Hook(list12.Get(),LC_D3D12SetIndexSlot(),CaptureSetIndex12,originalSetIndex12);
        Hook(list12.Get(),LC_D3D12SetViewportsSlot(),CaptureSetViewports12,originalSetViewports12);
        Hook(list12.Get(),LC_D3D12SetTopologySlot(),CaptureSetTopology12,originalSetTopology12);
        Hook(list12.Get(),LC_D3D12DrawIndexedSlot(),CaptureDrawIndexed12,originalDrawIndexed12);
        list12->Close();
        D3D12_COMMAND_QUEUE_DESC queueDesc12{};queueDesc12.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue12;
        Check(device12->CreateCommandQueue(&queueDesc12,IID_PPV_ARGS(&queue12)),"create D3D12 probe queue");
        Hook(queue12.Get(),LC_D3D12ExecuteSlot(),CaptureExecute12,originalExecute12);
        probing12=true;
        runningWorker12=true;worker12=std::thread(PreparePipelines12);
        Log("D3D12 probe attached to Unity's device. Shader/root data and indexed draws are counted; clipping is not active yet.");
        return 1;
    }catch(const std::exception& ex){Log(std::string("D3D12 probe failed: ")+ex.what());return 0;}
}
void LC_GetStats12(uint64_t* values,int count) {
    if(probing12)DrainCompleted12();
    if(values)for(int i=0;i<std::min(count,29);++i)values[i]=stats12[i].load();
}
int LC_Initialize(void* resource) {
    if(running) return 1;
    if(device) {Log("Native renderer requires a process restart after shutdown.");return 0;}
    try {
        if(!resource) return 0;
        HMODULE pin=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&LC_Initialize),&pin)) throw std::runtime_error("native module pin failed");
        static_cast<ID3D11Resource*>(resource)->GetDevice(&device);
        if(!device || device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0 || (device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED))
            throw std::runtime_error("requires a multithread-capable D3D11 feature-level 11 device");
        device->GetImmediateContext(&immediate);
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth=sizeof(ClipConstants); desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Check(device->CreateBuffer(&desc,nullptr,&constants),"create clip constants");
        MH_STATUS status=MH_Initialize();
        if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("MinHook initialization failed");
        Hook(device.Get(),15,CapturePS,originalCreate);
        EnsureDrawHooks();
        running=true; worker=std::thread(Work);
        Log("Native D3D11 shader capture active. Render changes require registered meshes and a camera scope.");
        return 1;
    } catch(const std::exception& ex) {Log(ex.what());return 0;}
}
int LC_Register(void* buffer,const uint32_t* eligible,int triangleCount) {
    try {
        if(device12 && probing12) {
            if(!buffer || !eligible || triangleCount<=0 || triangleCount>1000000)return 0;
            auto target=std::make_shared<Target12>();
            Check(static_cast<IUnknown*>(buffer)->QueryInterface(IID_PPV_ARGS(&target->index)),"query D3D12 index buffer");
            auto desc=target->index->GetDesc();
            target->address=target->index->GetGPUVirtualAddress();target->bytes=desc.Width;
            if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || !target->address || !target->bytes)
                throw std::runtime_error("invalid D3D12 mesh index resource");
            target->mask=MakeTypedBuffer12(device12.Get(),eligible,size_t(triangleCount)*4,DXGI_FORMAT_R32_UINT,triangleCount);
            target->triangleCount=triangleCount;
            std::lock_guard<std::mutex> lock(mutex12);
            if(targets12.count(target->index.Get()) || targetAddresses12.count(target->address))
                throw std::runtime_error("D3D12 private mesh index resource is not unique");
            targets12[target->index.Get()]=target;targetAddresses12[target->address]=target;
            return 1;
        }
        if(!running || !buffer || !eligible || triangleCount<=0 || triangleCount>1000000) return 0;
        auto target=std::make_shared<Target>(); target->index=static_cast<ID3D11Buffer*>(buffer); target->triangleCount=triangleCount;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=triangleCount*4;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=eligible;
        ComPtr<ID3D11Buffer> mask;
        Check(device->CreateBuffer(&desc,&initial,&mask),"create eligibility buffer");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{}; view.Format=DXGI_FORMAT_R32_UINT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=triangleCount;
        Check(device->CreateShaderResourceView(mask.Get(),&view,&target->mask),"create eligibility view");
        std::lock_guard<std::mutex> lock(mutex);targets[static_cast<ID3D11Buffer*>(buffer)]=std::move(target);return 1;
    } catch(const std::exception& ex) {Log(ex.what());return 0;}
}
void LC_Unregister(void* buffer) {
    if(device12){
        std::lock_guard<std::mutex> lock(mutex12);
        auto found=targets12.find(static_cast<ID3D12Resource*>(buffer));
        if(found!=targets12.end()){targetAddresses12.erase(found->second->address);targets12.erase(found);}
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);targets.erase(static_cast<ID3D11Buffer*>(buffer));
}
int LC_SetObstructions(void* buffer,const float* tree,const float* boxes,int count) {
    if(!tree || count<0 || count>int(MaxBoxes) || (count && !boxes)) return 0;
    std::lock_guard<std::mutex> lock(mutex);
    auto found=targets.find(static_cast<ID3D11Buffer*>(buffer));if(found==targets.end())return 0;
    auto& data=found->second->data;memcpy(data.tree,tree,12);data.boxCount=count;data.reserved=0;found->second->geometry.reset();
    if(count) memcpy(data.inverseBoxes,boxes,count*12*sizeof(float));return 1;
}
uint64_t LC_CreateGeometry(const float* boxes,int boxCount,const float* triangles,int triangleCount) {
    try {
        if(device12 && probing12) {
            if(boxCount<0 || triangleCount<0)return 0;
            auto packed=BuildGeometry(boxes,boxCount,triangles,triangleCount);
            auto geometry=std::make_shared<Geometry12>();
            geometry->views=MakeGeometryViews12(device12.Get(),packed);
            std::lock_guard<std::mutex> lock(mutex12);
            uint64_t id=++geometrySerial12;geometries12[id]=std::move(geometry);return id;
        }
        if(!running || boxCount<0 || triangleCount<0)return 0;
        auto packed=BuildGeometry(boxes,boxCount,triangles,triangleCount);
        auto geometry=std::make_shared<Geometry>();geometry->count=unsigned(packed.nodes.size()/2);
        auto upload=[&](const std::vector<std::array<float,4>>& values,ComPtr<ID3D11ShaderResourceView>& view,DXGI_FORMAT format) {
            if(values.empty())return;
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(values.size()*16);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};data.pSysMem=values.data();ComPtr<ID3D11Buffer> buffer;
            Check(device->CreateBuffer(&desc,&data,&buffer),"create obstruction buffer");
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=format;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sd.Buffer.NumElements=UINT(values.size());
            Check(device->CreateShaderResourceView(buffer.Get(),&sd,&view),"create obstruction view");
        };
        upload(packed.nodes,geometry->nodes,DXGI_FORMAT_R32G32B32A32_UINT);upload(packed.primitives,geometry->primitives,DXGI_FORMAT_R32G32B32A32_FLOAT);
        std::lock_guard<std::mutex> lock(mutex);if(!running)return 0;
        uint64_t id=++geometrySerial;geometries[id]=std::move(geometry);return id;
    }catch(const std::exception& ex){Log(ex.what());return 0;}
}
int LC_BindGeometry(void* buffer,const float* tree,uint64_t id) {
    if(!tree)return 0;
    if(device12){
        std::lock_guard<std::mutex> lock(mutex12);
        auto target=targets12.find(static_cast<ID3D12Resource*>(buffer));if(target==targets12.end())return 0;
        auto found=geometries12.find(id);if(id && found==geometries12.end())return 0;
        target->second->geometry=id?found->second:nullptr;
        auto& data=target->second->data;memcpy(data.tree,tree,12);data.reserved=1;
        data.boxCount=id?found->second->views.nodeCount:0;return 1;
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto target=targets.find(static_cast<ID3D11Buffer*>(buffer));if(target==targets.end())return 0;
    auto found=geometries.find(id);if(id && found==geometries.end())return 0;
    target->second->geometry=id?found->second:nullptr;
    auto& data=target->second->data;memcpy(data.tree,tree,12);data.reserved=1;data.boxCount=id?found->second->count:0;return 1;
}
void LC_ReleaseGeometry(uint64_t id) {
    if(device12){std::lock_guard<std::mutex> lock(mutex12);geometries12.erase(id);return;}
    std::lock_guard<std::mutex> lock(mutex);geometries.erase(id);
}
int LC_QueueCamera(const float* inverse) {
    if((!running && !probing12) || !inverse) return 0;
    std::lock_guard<std::mutex> lock(mutex);
    if(cameras.size()>16) cameras.clear();
    if(cameraSerial==INT_MAX) cameraSerial=100;
    int id=++cameraSerial; std::array<float,16> data;memcpy(data.data(),inverse,64);cameras[id]=data;return id;
}
void* LC_GetRenderEvent() {return reinterpret_cast<void*>(&RenderEvent);}
int LC_PollLog(char* output,int capacity) {
    if(!output || capacity<1)return 0;
    std::lock_guard<std::mutex> lock(mutex);if(messages.empty()){output[0]=0;return 0;}
    auto message=messages.front();messages.pop_front();size_t n=std::min(message.size(),size_t(capacity-1));memcpy(output,message.data(),n);output[n]=0;return int(n);
}
void LC_GetStats(uint64_t* values,int count) {if(values)for(int i=0;i<std::min(count,8);++i)values[i]=stats[i].load();}
void LC_Shutdown() {
    running=false;probing12=false;clipping12=false;wake.notify_all();if(worker.joinable())worker.join();
    runningWorker12=false;wake12.notify_all();if(worker12.joinable())worker12.join();
    {std::lock_guard<std::mutex> lock(mutex);targets.clear();geometries.clear();jobs.clear();shaders.clear();cameras.clear();}
    // The D3D12 module is pinned. Preserve command-list packets after shutdown:
    // Unity may submit a recorded list after the managed plugin is torn down.
    // Hooks remain transparent and the module stays pinned: other graphics mods
    // may have chained our entry points. Never unload code they may still call.
}
