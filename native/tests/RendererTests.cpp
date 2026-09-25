#include "ShaderPatch.h"
#include "Native.h"
#include <DirectXMath.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace leafclip;
namespace {
void Check(HRESULT hr,const char* what) {if(FAILED(hr))throw std::runtime_error(std::string(what)+": "+std::to_string(uint32_t(hr)));}
void Require(bool test,const std::string& why) {if(!test)throw std::runtime_error(why);}
struct Pixel {float r,g,b,a;};
struct Image {std::vector<Pixel> pixels;std::vector<float> depth;};
struct Harness {
    static constexpr UINT Size=96;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> color,depth,colorRead,depthRead;
    ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Buffer> vertex,index,animation,clip,mask;
    ComPtr<ID3D11ShaderResourceView> maskView;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> original,patched;
    ClipConstants params{};
    struct Animation {float matrix[16];float time[4];} anim{};
    Harness() {
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"WARP");
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=Size;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        Check(device->CreateTexture2D(&td,nullptr,&color),"color");Check(device->CreateRenderTargetView(color.Get(),nullptr,&rtv),"RTV");
        td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        Check(device->CreateTexture2D(&td,nullptr,&depth),"depth");Check(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"DSV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateTexture2D(&td,nullptr,&depthRead),"depth staging");
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;Check(device->CreateTexture2D(&td,nullptr,&colorRead),"color staging");
        // Hook capture before any pixel shader is created, as required in-game.
        Require(LC_Initialize(color.Get())!=0,"native initialization");
        auto vertexCode=Compile(R"(
            cbuffer Animation:register(b0){row_major float4x4 vp;float4 time;};
            struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};
            V main(float3 p:POSITION,float2 uv:TEXCOORD0){
                V o; p.x+=0.23*sin(time.x+p.y*3);p.y+=0.12*cos(time.x+p.x*2);p.z+=0.06*sin(time.x+p.y);
                o.p=mul(vp,float4(p,1));o.uv=uv;return o;}
        )","vs_5_0");
        Check(device->CreateVertexShader(vertexCode->GetBufferPointer(),vertexCode->GetBufferSize(),nullptr,&vs),"VS");
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
        Check(device->CreateInputLayout(elements,2,vertexCode->GetBufferPointer(),vertexCode->GetBufferSize(),&layout),"layout");
        auto pixelCode=Compile("float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target {clip(frac(uv.x*9)-0.3);return float4(uv,p.z,1);}","ps_4_0");
        Check(device->CreatePixelShader(pixelCode->GetBufferPointer(),pixelCode->GetBufferSize(),nullptr,&original),"original PS");
        auto modified=PatchPixelShader(pixelCode->GetBufferPointer(),pixelCode->GetBufferSize());
        Check(device->CreatePixelShader(modified.data(),modified.size(),nullptr,&patched),"patched PS");
        float vertices[]={-.85f,-.85f,.5f,0,0, .85f,-.85f,.5f,1,0, .85f,.85f,.5f,1,1, -.85f,.85f,.5f,0,1};
        uint16_t indices[]={0,1,2,0,2,3};uint32_t eligibility[]={1,1};
        vertex=Buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices);
        index=Buffer(sizeof(indices),D3D11_BIND_INDEX_BUFFER,indices);
        animation=Buffer(sizeof(anim),D3D11_BIND_CONSTANT_BUFFER);
        clip=Buffer(sizeof(params),D3D11_BIND_CONSTANT_BUFFER);
        mask=Buffer(sizeof(eligibility),D3D11_BIND_SHADER_RESOURCE,eligibility);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_UINT;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sd.Buffer.NumElements=2;
        Check(device->CreateShaderResourceView(mask.Get(),&sd,&maskView),"mask SRV");
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;Check(device->CreateRasterizerState(&rs,&raster),"rasterizer");context->RSSetState(raster.Get());
        auto target=rtv.Get();context->OMSetRenderTargets(1,&target,dsv.Get());
        D3D11_VIEWPORT viewport{0,0,float(Size),float(Size),0,1};context->RSSetViewports(1,&viewport);
        UINT stride=20,offset=0;auto vb=vertex.Get();context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);auto cb=animation.Get();context->VSSetConstantBuffers(0,1,&cb);
        cb=clip.Get();context->PSSetConstantBuffers(13,1,&cb);auto srv=maskView.Get();context->PSSetShaderResources(127,1,&srv);
        params.viewport[2]=params.viewport[3]=float(Size);params.tree[0]=-2;
        params.boxCount=1;params.enabled=1;
        params.inverseBoxes[0][0]=20;params.inverseBoxes[0][3]=-1;
        params.inverseBoxes[0][5]=1/.35f;params.inverseBoxes[0][10]=1;
    }
    ComPtr<ID3D11Buffer> Buffer(UINT bytes,UINT bind,const void* data=nullptr) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=data;ComPtr<ID3D11Buffer> result;
        Check(device->CreateBuffer(&desc,data?&initial:nullptr,&result),"buffer");return result;
    }
    void SetFrame(float t,int projection) {
        using namespace DirectX;
        XMMATRIX matrix=XMMatrixIdentity();
        if(projection==1)matrix=XMMatrixSet(.8f,0,0,.1f, 0,-.9f,0,-.07f, 0,0,-.7f,.9f, 0,0,0,1);
        if(projection==2)matrix=XMMatrixSet(.85f,0,0,.1f, 0,.85f,0,0, 0,0,.7f,0, 0,0,.4f,1);
        XMFLOAT4X4 packed;XMStoreFloat4x4(&packed,matrix);memcpy(anim.matrix,&packed,64);
        XMStoreFloat4x4(&packed,XMMatrixInverse(nullptr,matrix));memcpy(params.inverseViewProjection,&packed,64);
        anim.time[0]=t;context->UpdateSubresource(animation.Get(),0,nullptr,&anim,0,0);
        context->UpdateSubresource(clip.Get(),0,nullptr,&params,0,0);
    }
    Image Draw(ID3D11PixelShader* shader,UINT start=0,UINT count=6) {
        float clear[4]={0,0,0,0};context->ClearRenderTargetView(rtv.Get(),clear);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
        context->PSSetShader(shader,nullptr,0);context->DrawIndexed(count,start,0);
        context->CopyResource(colorRead.Get(),color.Get());context->CopyResource(depthRead.Get(),depth.Get());
        Image image;image.pixels.resize(Size*Size);image.depth.resize(Size*Size);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(colorRead.Get(),0,D3D11_MAP_READ,0,&mapped),"read color");
        for(UINT y=0;y<Size;++y)memcpy(image.pixels.data()+y*Size,(char*)mapped.pData+y*mapped.RowPitch,Size*sizeof(Pixel));context->Unmap(colorRead.Get(),0);
        Check(context->Map(depthRead.Get(),0,D3D11_MAP_READ,0,&mapped),"read depth");
        for(UINT y=0;y<Size;++y)memcpy(image.depth.data()+y*Size,(char*)mapped.pData+y*mapped.RowPitch,Size*sizeof(float));context->Unmap(depthRead.Get(),0);
        return image;
    }
    size_t Verify(const Image& before,const Image& after,bool cutting,std::function<bool(float,float,float)> predicate={}) {
        size_t removed=0;
        for(UINT y=0;y<Size;++y)for(UINT x=0;x<Size;++x) {
            size_t i=y*Size+x;
            float screen[4]={2*(x+.5f)/Size-1,1-2*(y+.5f)/Size,before.depth[i],1},p[4]={};
            for(int r=0;r<4;++r)for(int c=0;c<4;++c)p[r]+=params.inverseViewProjection[r*4+c]*screen[c];
            bool hide=cutting && before.pixels[i].a!=0 && (predicate?predicate(p[0]/p[3],p[1]/p[3],p[2]/p[3]):p[0]/p[3]>=0 && std::abs(p[1]/p[3])<=.35f);
            if(hide) {
                ++removed;Require(after.pixels[i].a==0 && after.depth[i]==1,"discard left color/depth at "+std::to_string(i)+" world="+std::to_string(p[0]/p[3])+","+std::to_string(p[1]/p[3])+","+std::to_string(p[2]/p[3])+" time="+std::to_string(anim.time[0]));
            }else {
                Require(memcmp(&before.pixels[i],&after.pixels[i],sizeof(Pixel))==0,"untouched pixel changed at "+std::to_string(i));
                Require(before.depth[i]==after.depth[i],"untouched depth changed");
            }
        }
        if(cutting)Require(removed>20,"test did not cover enough clipped pixels");return removed;
    }
};
void RejectTests() {
    for(const char* source : {
        "float4 main(float2 uv:TEXCOORD0):SV_Target{return float4(uv,0,1);}",
        "cbuffer Occupied:register(b13){float4 occupied;};float4 main(float4 p:SV_Position):SV_Target{return occupied+p;}",
        "Texture2D occupied:register(t127);float4 main(float4 p:SV_Position):SV_Target{return occupied.Load(int3(p.xy,0));}",
        "Texture2D occupied:register(t126);float4 main(float4 p:SV_Position):SV_Target{return occupied.Load(int3(p.xy,0));}",
        "Texture2D occupied:register(t125);float4 main(float4 p:SV_Position):SV_Target{return occupied.Load(int3(p.xy,0));}",
        "[earlydepthstencil]float4 main(float4 p:SV_Position):SV_Target{return p;}"}) {
        auto shader=Compile(source,"ps_5_0");bool rejected=false;
        try{PatchPixelShader(shader->GetBufferPointer(),shader->GetBufferSize());}catch(...){rejected=true;}
        Require(rejected,"unsupported shader not rejected");
    }
    uint32_t invalid[8]={};bool rejected=false;try{PatchPixelShader(invalid,sizeof(invalid));}catch(...){rejected=true;}
    Require(rejected,"malformed container not rejected");
}
}
int main(int argc,char** argv) {
    int result=0;
    try {
        Harness h;RejectTests();size_t removed=0;
        auto diagnosticCode=Compile("float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target {clip(frac(uv.x*9)-0.3);return float4(uv,p.z,1);}","ps_4_0");
        auto diagnosticBytes=PatchPixelShader(diagnosticCode->GetBufferPointer(),diagnosticCode->GetBufferSize(),false,true);
        ComPtr<ID3D11PixelShader> diagnosticShader;
        Check(h.device->CreatePixelShader(diagnosticBytes.data(),diagnosticBytes.size(),nullptr,&diagnosticShader),"visual diagnostic shader");
        h.SetFrame(0,0);
        Image diagnosticOriginal=h.Draw(h.original.Get());
        Image diagnosticNormal=h.Draw(h.patched.Get());
        size_t normalRemoved=0;
        for(size_t i=0;i<diagnosticOriginal.pixels.size();++i)
            if(diagnosticOriginal.pixels[i].a>0 && diagnosticNormal.pixels[i].a==0)++normalRemoved;
        Image diagnosticImage=h.Draw(diagnosticShader.Get());
        size_t red=0,blue=0;
        for(size_t i=0;i<diagnosticImage.pixels.size();++i) {
            const Pixel& pixel=diagnosticImage.pixels[i];
            if(pixel.r>.9f && pixel.g<.1f && pixel.b<.1f)++red;
            if(pixel.b>.9f && pixel.r<.1f && pixel.g<.3f)++blue;
            if(diagnosticOriginal.pixels[i].a>0 && diagnosticNormal.pixels[i].a==0)
                Require(pixel.r>.9f && pixel.g<.1f && pixel.b<.1f,
                    "diagnostic failed to mark a normally hidden pixel red");
        }
        Require(red>20 && blue>20,"visual diagnostic did not mark both accepted and missed pixels: red="+
            std::to_string(red)+" blue="+std::to_string(blue)+" normalRemoved="+std::to_string(normalRemoved));
        auto mrtCode=Compile(R"(
            struct GBuffer {float4 diffuse:SV_Target0;float4 specular:SV_Target1;
                            float4 normal:SV_Target2;float4 emission:SV_Target3;};
            GBuffer main(float4 p:SV_Position,float2 uv:TEXCOORD0) {
                clip(frac(uv.x*9)-0.3);
                GBuffer result;
                result.diffuse=float4(uv,p.z,1);result.specular=0;
                result.normal=0;result.emission=0;
                return result;
            })","ps_5_0");
        auto mrtBytes=PatchPixelShader(mrtCode->GetBufferPointer(),mrtCode->GetBufferSize(),false,true);
        ComPtr<ID3D11PixelShader> mrtShader;
        Check(h.device->CreatePixelShader(mrtBytes.data(),mrtBytes.size(),nullptr,&mrtShader),
            "deferred visual diagnostic shader");
        D3D11_TEXTURE2D_DESC mrtDesc{};h.color->GetDesc(&mrtDesc);
        std::array<ComPtr<ID3D11Texture2D>,3> mrtColors;
        std::array<ComPtr<ID3D11RenderTargetView>,3> mrtViews;
        ID3D11RenderTargetView* targets[4]={h.rtv.Get(),nullptr,nullptr,nullptr};
        for(unsigned i=0;i<3;++i) {
            Check(h.device->CreateTexture2D(&mrtDesc,nullptr,&mrtColors[i]),"diagnostic MRT color");
            Check(h.device->CreateRenderTargetView(mrtColors[i].Get(),nullptr,&mrtViews[i]),"diagnostic MRT view");
            targets[i+1]=mrtViews[i].Get();
        }
        h.context->OMSetRenderTargets(4,targets,h.dsv.Get());
        float clear[4]={0,0,0,0};
        h.context->ClearRenderTargetView(mrtViews[2].Get(),clear);
        Image diffuseImage=h.Draw(mrtShader.Get());
        h.context->CopyResource(h.colorRead.Get(),mrtColors[2].Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(h.context->Map(h.colorRead.Get(),0,D3D11_MAP_READ,0,&mapped),"read diagnostic emission");
        size_t emissionRed=0,emissionBlue=0,diffuseBright=0;
        for(UINT y=0;y<Harness::Size;++y) {
            auto row=reinterpret_cast<const Pixel*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
            for(UINT x=0;x<Harness::Size;++x) {
                const Pixel& pixel=row[x];
                if(pixel.r>.9f && pixel.g<.1f && pixel.b<.1f)++emissionRed;
                if(pixel.b>.9f && pixel.r<.1f && pixel.g<.3f)++emissionBlue;
                const Pixel& diffuse=diffuseImage.pixels[y*Harness::Size+x];
                if(diffuse.r>.01f || diffuse.g>.01f || diffuse.b>.01f)++diffuseBright;
            }
        }
        h.context->Unmap(h.colorRead.Get(),0);
        auto normalTarget=h.rtv.Get();h.context->OMSetRenderTargets(1,&normalTarget,h.dsv.Get());
        Require(emissionRed>20 && emissionBlue>20 && diffuseBright==0,
            "deferred diagnostic did not write full-color emission with black diffuse: red="+
            std::to_string(emissionRed)+" blue="+std::to_string(emissionBlue)+
            " diffuse="+std::to_string(diffuseBright));
        auto worldCode=Compile(R"(
            float4 main(float4 p:SV_Position,float4 x:TEXCOORD1,float4 y:TEXCOORD2,
                        float4 z:TEXCOORD3):SV_Target {
                return float4(x.w,y.w,z.w,1);
            })","ps_5_0");
        auto worldBytes=PatchPixelShader(worldCode->GetBufferPointer(),worldCode->GetBufferSize(),true);
        ComPtr<ID3D11PixelShader> worldPatched;
        Check(h.device->CreatePixelShader(worldBytes.data(),worldBytes.size(),nullptr,&worldPatched),
            "animated world-position patch");
        if(argc==2) {
            size_t accepted=0,rejected=0;std::map<std::string,int> reasons;
            for(auto& entry:std::filesystem::directory_iterator(argv[1])) {
                if(entry.path().extension()!=".dxbc")continue;
                std::ifstream stream(entry.path(),std::ios::binary);
                std::vector<uint8_t> code((std::istreambuf_iterator<char>(stream)),{});
                try {
                    auto bytes=PatchPixelShader(code.data(),code.size());ComPtr<ID3D11PixelShader> ps;
                    Check(h.device->CreatePixelShader(bytes.data(),bytes.size(),nullptr,&ps),"asset variant");++accepted;
                }catch(const std::exception& ex){++rejected;++reasons[ex.what()];}
            }
            std::cout<<"Local shader compatibility: accepted="<<accepted<<" rejected="<<rejected<<"\n";
            for(auto& reason:reasons)std::cout<<reason.second<<": "<<reason.first<<"\n";
            Require(accepted>0,"no compatible local shader variants");
        }
        for(int projection=0;projection<3;++projection)for(int frame=0;frame<4;++frame) {
            h.params.enabled=1;h.SetFrame(frame*1.7f,projection);
            auto before=h.Draw(h.original.Get());auto after=h.Draw(h.patched.Get());removed+=h.Verify(before,after,true);
            h.params.enabled=0;h.SetFrame(frame*1.7f,projection);h.Verify(before,h.Draw(h.patched.Get()),false);
        }
        std::cout<<"12 animated frames / 3 projections: surviving color and depth bit-identical; "<<removed<<" obstructed pixels removed, no depth writes\n";
        // A camera depth pass may have a pixel shader with no color target.
        // Its retained depth must agree with the clipped color pass.
        auto depthCode=Compile("void main(float4 p:SV_Position){clip(p.x-48.0);}","ps_5_0");
        ComPtr<ID3D11PixelShader> depthOriginal,depthClipped;
        Check(h.device->CreatePixelShader(depthCode->GetBufferPointer(),depthCode->GetBufferSize(),nullptr,&depthOriginal),"depth-only original");
        auto depthBytes=PatchPixelShader(depthCode->GetBufferPointer(),depthCode->GetBufferSize());
        Check(h.device->CreatePixelShader(depthBytes.data(),depthBytes.size(),nullptr,&depthClipped),"depth-only patched");
        h.params.enabled=1;h.SetFrame(1.1f,0);
        auto depthBefore=h.Draw(depthOriginal.Get()),depthAfter=h.Draw(depthClipped.Get());
        size_t discardedDepth=0;
        for(UINT y=0;y<Harness::Size;++y)for(UINT x=0;x<Harness::Size;++x) {
            size_t i=y*Harness::Size+x;
            bool cut=depthBefore.depth[i]<1 && x>=48 && y>=31 && y<=64;
            // At identity projection, box height [-.35,.35] maps to these
            // interior pixel rows. Boundary rows are checked by the helper.
            if(cut) {Require(depthAfter.depth[i]==1,"depth-only obstruction not discarded");++discardedDepth;}
            else if(depthBefore.depth[i]==1 || y<30 || y>65) Require(depthAfter.depth[i]==depthBefore.depth[i],"depth-only untouched pixel changed");
        }
        Require(discardedDepth>100,"depth-only test covered too few pixels");
        std::cout<<"Depth-only pixel shader: "<<discardedDepth<<" obstructed depth writes removed\n";
        uint32_t none[2]={0,0};h.context->UpdateSubresource(h.mask.Get(),0,nullptr,none,0,0);
        h.params.enabled=1;h.SetFrame(1,0);auto before=h.Draw(h.original.Get());h.Verify(before,h.Draw(h.patched.Get()),false);
        uint32_t all[2]={1,1};h.context->UpdateSubresource(h.mask.Get(),0,nullptr,all,0,0);
        Require(LC_Register(h.index.Get(),all,2,0)!=0,"register test draw");
        Require(LC_SetObstructions(h.index.Get(),h.params.tree,&h.params.inverseBoxes[0][0],1)!=0,"set obstruction");
        auto event=reinterpret_cast<void(__stdcall*)(int)>(LC_GetRenderEvent());
        event(LC_QueueCamera(h.params.inverseViewProjection));
        // First draw queues compilation on the worker. No shader compilation on
        // the render thread; original rendering stays active while warming up.
        h.Draw(h.original.Get());
        uint64_t stats[8]={};
        for(int attempt=0;attempt<300;++attempt) {LC_GetStats(stats,8);if(stats[1] || stats[2])break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        if(!stats[1]) {for(auto value:stats)std::cerr<<value<<" ";std::cerr<<"\n";char log[2048];while(LC_PollLog(log,sizeof(log)))std::cerr<<log<<"\n";}
        Require(stats[1]>0,"native shader worker did not prepare variant");
        h.Verify(before,h.Draw(h.original.Get()),true);
        h.SetFrame(1.1f,0);h.Draw(depthOriginal.Get()); // Queue depth-only variant.
        for(int attempt=0;attempt<300;++attempt) {LC_GetStats(stats,8);if(stats[1]>=2 || stats[2])break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        Require(stats[1]>=2,"native depth-only shader worker did not prepare variant");
        auto interceptedDepth=h.Draw(depthOriginal.Get());
        for(size_t i=0;i<interceptedDepth.depth.size();++i)
            Require(interceptedDepth.depth[i]==depthAfter.depth[i],"native depth-only interception disagrees with direct clipping");
        std::cout<<"Native depth-only draw interception matches the patched depth image\n";
        ComPtr<ID3D11PixelShader> restored;h.context->PSGetShader(&restored,nullptr,nullptr);Require(restored.Get()==depthOriginal.Get(),"pixel shader not restored");
        ComPtr<ID3D11Buffer> restoredCB;h.context->PSGetConstantBuffers(13,1,&restoredCB);Require(restoredCB.Get()==h.clip.Get(),"constant buffer not restored");
        ComPtr<ID3D11ShaderResourceView> restoredSRV;h.context->PSGetShaderResources(127,1,&restoredSRV);Require(restoredSRV.Get()==h.maskView.Get(),"SRV not restored");
        // More than the original 64-box budget, with a real hit buried among
        // spatially irrelevant leaves. Compare actual intercepted GPU output.
        std::vector<float> boxes;
        for(unsigned i=0;i<257;++i) {
            float x=i==173?0:10+float(i),y=i==173?0:float(i%17);
            float b[18]={20,0,0,-1-20*x, 0,1/.35f,0,-y/.35f, 0,0,1,0,
                x,y-.35f,-1,x+.1f,y+.35f,1};
            boxes.insert(boxes.end(),b,b+18);
        }
        std::cout<<"Testing box hierarchy\n";
        uint64_t geometry=LC_CreateGeometry(boxes.data(),257,nullptr,0);Require(geometry!=0,"build large hierarchy");
        Require(LC_BindGeometry(h.index.Get(),h.params.tree,geometry)!=0,"bind hierarchy");
        LC_ReleaseGeometry(geometry); // Draws retain ownership independently.
        ID3D11ShaderResourceView* occupied=h.maskView.Get();
        h.context->PSSetShaderResources(125,1,&occupied);h.context->PSSetShaderResources(126,1,&occupied);
        for(int projection=0;projection<3;++projection)for(int frame=0;frame<4;++frame) {
            h.SetFrame(frame*1.7f,projection);event(0);auto reference=h.Draw(h.original.Get());
            event(LC_QueueCamera(h.params.inverseViewProjection));h.Verify(reference,h.Draw(h.original.Get()),true);
        }
        for(unsigned slot=125;slot<=127;++slot) {
            ComPtr<ID3D11ShaderResourceView> view;h.context->PSGetShaderResources(slot,1,&view);
            Require(view.Get()==h.maskView.Get(),"hierarchy resource state not restored");
        }
        // Two-sided sloped surface, y <= .35 + .3*z. This is a geometric edge,
        // not the surface's AABB: wind changes both its apparent height and depth.
        std::cout<<"Testing triangle hierarchy\n";
        const float surface[]={0,-.35f,-1, 0,-.35f,1, 0,.65f,1, -1,1,
                               0,-.35f,-1, 0,.65f,1, 0,.05f,-1, -1,1};
        geometry=LC_CreateGeometry(nullptr,0,surface,2);Require(geometry!=0,"build triangle hierarchy");
        Require(LC_BindGeometry(h.index.Get(),h.params.tree,geometry)!=0,"bind triangles");LC_ReleaseGeometry(geometry);
        for(int projection=0;projection<3;++projection)for(int frame=0;frame<4;++frame) {
            h.SetFrame(frame*1.7f,projection);event(0);auto reference=h.Draw(h.original.Get());
            event(LC_QueueCamera(h.params.inverseViewProjection));h.Verify(reference,h.Draw(h.original.Get()),true,
                [](float x,float y,float z){return x>=0 && y>=-.35f && y<=.35f+.3f*z*2/(x+2) && z>=-1.015f && z<=1.015f;});
        }
        Require(LC_BindGeometry(h.index.Get(),h.params.tree,0)!=0,"clear hierarchy");
        h.SetFrame(1,0);event(0);before=h.Draw(h.original.Get());event(LC_QueueCamera(h.params.inverseViewProjection));
        h.Verify(before,h.Draw(h.original.Get()),false);
        uint32_t branches[2]={2,2};
        LC_Unregister(h.index.Get());Require(LC_Register(h.index.Get(),branches,2,2.3f)!=0,"register branch mask");
        Require(LC_SetObstructions(h.index.Get(),h.params.tree,&h.params.inverseBoxes[0][0],1)!=0,"bind branch test wall");
        h.SetFrame(1,0);event(0);before=h.Draw(h.original.Get());event(LC_QueueCamera(h.params.inverseViewProjection));
        h.Verify(before,h.Draw(h.original.Get()),true,[](float x,float y,float z){
            return x>=0 && std::abs(y)<=.35f && std::hypot(x+2,z)>=2.3f;
        });
        LC_Unregister(h.index.Get());
        uint32_t upperTrunk[2]={3,3};
        Require(LC_Register(h.index.Get(),upperTrunk,2,0)!=0,"register upper trunk mask");
        geometry=LC_CreateGeometry(nullptr,0,surface,2);Require(geometry!=0,"build upper trunk wall");
        Require(LC_BindGeometry(h.index.Get(),h.params.tree,geometry)!=0,"bind upper trunk wall");LC_ReleaseGeometry(geometry);
        h.SetFrame(1,0);event(0);before=h.Draw(h.original.Get());event(LC_QueueCamera(h.params.inverseViewProjection));
        h.Verify(before,h.Draw(h.original.Get()),true,
            [](float x,float y,float z){return x>=0 && y>=-.35f && y<=.35f+.3f*z*2/(x+2) && z>=-1.015f && z<=1.015f;});
        const float floorBox[]={1,0,0,0, 0,10,0,0, 0,0,1,0, -1,-.1f,-1, 1,.1f,1};
        geometry=LC_CreateGeometry(floorBox,1,nullptr,0);Require(geometry!=0,"build floor-shaped slab");
        Require(LC_BindGeometry(h.index.Get(),h.params.tree,geometry)!=0,"bind floor-shaped slab");LC_ReleaseGeometry(geometry);
        h.SetFrame(1,0);event(0);before=h.Draw(h.original.Get());event(LC_QueueCamera(h.params.inverseViewProjection));
        h.Verify(before,h.Draw(h.original.Get()),false);
        LC_Unregister(h.index.Get());
        Require(LC_CreateGeometry(nullptr,1,nullptr,0)==0,"missing geometry input accepted");
        Require(LC_CreateGeometry(nullptr,131073,nullptr,0)==0,"geometry budget ignored");
        std::cout<<"257-box BVH and exact sloped mesh surface: 24 animated GPU frames passed; shared resource lifetime and all three SRV slots restored\n";
        // Primitive IDs restart per draw; StartIndexLocation must offset the mask.
        uint32_t mixed[2]={1,0};LC_Unregister(h.index.Get());Require(LC_Register(h.index.Get(),mixed,2,0)!=0,"register mixed mask");
        LC_SetObstructions(h.index.Get(),h.params.tree,&h.params.inverseBoxes[0][0],1);
        event(0);auto second=h.Draw(h.original.Get(),3,3);event(LC_QueueCamera(h.params.inverseViewProjection));h.Verify(second,h.Draw(h.original.Get(),3,3),false);
        event(0);h.Verify(before,h.Draw(h.original.Get()),false);
        LC_Unregister(h.index.Get());event(LC_QueueCamera(h.params.inverseViewProjection));h.Verify(before,h.Draw(h.original.Get()),false);event(0);
        std::cout<<"Native capture, worker patch, targeted draw, primitive mask, state restoration, camera-scope and unregister tests passed\n";
        std::cout<<"Unsupported layouts and malformed bytecode rejected\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
    LC_Shutdown();return result;
}
