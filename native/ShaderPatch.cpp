#include "ShaderPatch.h"
#include "third_party/tokens.hpp"
#include "third_party/DXBCChecksum.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <limits>

namespace leafclip {
namespace {
using Words = std::vector<uint32_t>;
constexpr uint32_t Tag(char a, char b, char c, char d) {
    return uint32_t(a) | uint32_t(b)<<8 | uint32_t(c)<<16 | uint32_t(d)<<24;
}
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Chunk { uint32_t tag; Words words; };
std::vector<Chunk> Read(const void* data, size_t size) {
    Require(size >= 32 && size % 4 == 0, "invalid DXBC size");
    Words words(size / 4); memcpy(words.data(), data, size);
    Require(words[0] == Tag('D','X','B','C') && words[6] == size && words[7] <= (size-32)/4, "invalid DXBC header");
    std::vector<Chunk> result;
    for (unsigned i=0; i<words[7]; ++i) {
        size_t offset=words[8+i];
        Require(offset % 4 == 0 && offset <= size-8, "invalid chunk offset");
        size_t start=offset/4, count=words[start+1]/4;
        Require(words[start+1]%4==0 && count <= words.size()-start-2, "invalid chunk length");
        result.push_back({words[start], Words(words.begin()+start+2,words.begin()+start+2+count)});
    }
    return result;
}
Chunk& Find(std::vector<Chunk>& chunks, uint32_t tag) {
    for (auto& c: chunks) if(c.tag==tag) return c;
    throw std::runtime_error("required shader chunk missing");
}
Chunk& Program(std::vector<Chunk>& chunks) {
    for(auto& c:chunks) if(c.tag==Tag('S','H','E','X') || c.tag==Tag('S','H','D','R')) return c;
    throw std::runtime_error("shader program missing");
}
struct Instruction { Words words; unsigned Op() const { return words[0]&0x7ff; } };
bool Declaration(unsigned op) { return (op>=D3D10_SB_OPCODE_DCL_RESOURCE && op<=D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) || op==D3D10_SB_OPCODE_CUSTOMDATA; }
std::vector<Instruction> Instructions(const Words& code) {
    Require(code.size()>=2 && code[1]==code.size(), "invalid token program");
    unsigned major=DECODE_D3D10_SB_TOKENIZED_PROGRAM_MAJOR_VERSION(code[0]), minor=DECODE_D3D10_SB_TOKENIZED_PROGRAM_MINOR_VERSION(code[0]);
    Require(DECODE_D3D10_SB_TOKENIZED_PROGRAM_TYPE(code[0])==D3D10_SB_PIXEL_SHADER &&
        ((major==4 && minor<=1) || (major==5 && minor==0)),"requires pixel shader 4.0, 4.1 or 5.0");
    std::vector<Instruction> result;
    for(size_t p=2; p<code.size();) {
        unsigned op=code[p]&0x7ff;
        size_t n=op==D3D10_SB_OPCODE_CUSTOMDATA ? (p+1<code.size()?code[p+1]:0) : ((code[p]>>24)&0x7f);
        Require(n>0 && n<=code.size()-p,"invalid instruction length");
        result.push_back({Words(code.begin()+p,code.begin()+p+n)}); p+=n;
    }
    return result;
}
// Parse operands, including relative constant-buffer indices. Only our small
// compiled helper is rewritten; unknown operand encodings reject the patch.
void Operand(Words& w, size_t& p, unsigned tempBase, const std::vector<unsigned>& inputs) {
    Require(p<w.size(),"truncated operand");
    uint32_t token=w[p++], extension=token;
    while(extension&0x80000000u) { Require(p<w.size(),"truncated extended operand"); extension=w[p++]; }
    unsigned type=(token>>12)&0xff, dims=(token>>20)&3;
    if(type==D3D10_SB_OPERAND_TYPE_IMMEDIATE32 || type==D3D10_SB_OPERAND_TYPE_IMMEDIATE64) {
        unsigned count=(token&3)==D3D10_SB_OPERAND_4_COMPONENT?4:1;
        p+=count*(type==D3D10_SB_OPERAND_TYPE_IMMEDIATE64?2:1);
        Require(p<=w.size(),"truncated literal"); return;
    }
    Require(type!=D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP && type!=D3D10_SB_OPERAND_TYPE_IMMEDIATE_CONSTANT_BUFFER,
        "unsupported helper storage");
    for(unsigned d=0;d<dims;++d) {
        unsigned rep=(token>>(22+3*d))&7;
        Require(rep==0 || rep==2 || rep==3,"unsupported operand index");
        if(rep==0 || rep==3) {
            Require(p<w.size(),"truncated operand index");
            if(d==0 && type==D3D10_SB_OPERAND_TYPE_TEMP) w[p]+=tempBase;
            if(d==0 && type==D3D10_SB_OPERAND_TYPE_INPUT) {
                Require(w[p]<inputs.size() && inputs[w[p]]!=UINT_MAX,"unexpected helper input"); w[p]=inputs[w[p]];
            }
            ++p;
        }
        if(rep==2 || rep==3) Operand(w,p,tempBase,inputs);
    }
}
void Relocate(Instruction& inst,unsigned tempBase,const std::vector<unsigned>& inputs) {
    size_t p=1; uint32_t token=inst.words[0];
    while(token&0x80000000u) { Require(p<inst.words.size(),"truncated opcode extension"); token=inst.words[p++]; }
    unsigned trailing=0;
    switch(inst.Op()) {
    case D3D10_SB_OPCODE_DCL_RESOURCE:
    case D3D10_SB_OPCODE_DCL_INPUT_PS_SGV:
    case D3D10_SB_OPCODE_DCL_INPUT_PS_SIV: trailing=1; break;
    }
    while(p<inst.words.size()-trailing) Operand(inst.words,p,tempBase,inputs);
    Require(p+trailing==inst.words.size(),"unexpected helper instruction encoding");
}
const char* Helper=R"hlsl(
cbuffer LeafClip : register(b13) {
    row_major float4x4 inverseVP;
    float4 viewport;
    float4 tree;
    uint boxCount; uint triangleBase; uint enabled; uint reserved;
    float4 inverseBoxes[192];
};
Buffer<uint> eligible : register(t127);
Buffer<uint4> nodes : register(t126);
Buffer<float4> primitives : register(t125);
bool rangeSlab(float a,float d,float lo,float hi,inout float low,inout float high) {
    bool parallel=abs(d)<1e-7;
    float safeD=parallel?1:d;
    float t0=(lo-a)/safeD,t1=(hi-a)/safeD;
    low=max(low,parallel?0:min(t0,t1));
    high=min(high,parallel?1:max(t0,t1));
    return (!parallel || (a>=lo && a<=hi)) && low<=high;
}
bool slab(float a,float d,inout float low,inout float high) {
    bool parallel=abs(d)<1e-7;
    float safeD=parallel?1:d;
    float t0=(-1-a)/safeD,t1=(1-a)/safeD;
    low=max(low,parallel?0:min(t0,t1));
    high=min(high,parallel?1:max(t0,t1));
    return (!parallel || abs(a)<=1) && low<=high;
}
void main(float4 screen:SV_Position,uint primitive:SV_PrimitiveID) {
    float2 xy=(screen.xy-viewport.xy)/viewport.zw;
    float4 hp=mul(inverseVP,float4(xy.x*2-1,1-xy.y*2,screen.z,1));
    float3 p=hp.xyz/hp.w;
    float3 start=float3(tree.x,p.y,tree.z);
    bool hide=false;
    uint selection=eligible[triangleBase+primitive];
    bool branchPixel=selection==2 && length(p.xz-tree.xz)>=tree.w;
    if(enabled!=0 && (selection==1 || branchPixel)) {
        if(reserved!=0) {
            float3 direction=p-start;
            uint node=0;
            [loop] while(node<boxCount && !hide) {
                uint4 packedLow=nodes[node*2],packedHigh=nodes[node*2+1];
                float3 boundsLow=asfloat(packedLow.xyz),boundsHigh=asfloat(packedHigh.xyz);
                float low=0,high=1;
                bool hit=rangeSlab(start.x,direction.x,boundsLow.x,boundsHigh.x,low,high);
                hit=rangeSlab(start.y,direction.y,boundsLow.y,boundsHigh.y,low,high) && hit;
                hit=rangeSlab(start.z,direction.z,boundsLow.z,boundsHigh.z,low,high) && hit;
                uint payload=packedHigh.w;
                if(!hit) {node=packedLow.w;continue;}
                ++node;
                if(payload==0xffffffffu) continue;
                uint offset=(payload & 0x7fffffffu)*3;
                float4 v0=primitives[offset],v1=primitives[offset+1],v2=primitives[offset+2];
                if((payload & 0x80000000u)!=0) {
                    float3 a=float3(dot(v0,float4(start,1)),dot(v1,float4(start,1)),dot(v2,float4(start,1)));
                    float3 d=float3(dot(v0,float4(direction,0)),dot(v1,float4(direction,0)),dot(v2,float4(direction,0)));
                    low=0;high=1;
                    hit=slab(a.x,d.x,low,high);
                    hit=slab(a.y,d.y,low,high) && hit;
                    hide=slab(a.z,d.z,low,high) && hit;
                } else {
                    // Two-sided segment/triangle intersection; no face-normal or
                    // winding assumption about imported collider meshes.
                    float3 e1=v1.xyz-v0.xyz,e2=v2.xyz-v0.xyz;
                    float3 q=cross(direction,e2);float determinant=dot(e1,q);
                    if(abs(determinant)>1e-8) {
                        float3 s=start-v0.xyz;float u=dot(s,q)/determinant;
                        float3 r=cross(s,e1);float v=dot(direction,r)/determinant;
                        float t=dot(e2,r)/determinant;
                        hide=u>=0 && v>=0 && u+v<=1 && t>=0 && t<=1;
                        if(hide) {
                            // Use the whole building piece's horizontal span.
                            // A per-triangle span makes a tessellated wall open
                            // seams as foliage sways across internal edges.
                            float3 normal=abs(cross(e1,e2));
                            float margin=0.06;
                            if(normal.x>=normal.y && normal.x>=normal.z)
                                hide=p.z>=v0.w-margin && p.z<=v1.w+margin;
                            else if(normal.z>=normal.y)
                                hide=p.x>=v0.w-margin && p.x<=v1.w+margin;
                        }
                    }
                }
            }
        } else [loop] for(uint i=0;i<min(boxCount,64u);++i) {
            float3 a=float3(dot(inverseBoxes[i*3],float4(start,1)),dot(inverseBoxes[i*3+1],float4(start,1)),dot(inverseBoxes[i*3+2],float4(start,1)));
            float3 d=float3(dot(inverseBoxes[i*3],float4(p-start,0)),dot(inverseBoxes[i*3+1],float4(p-start,0)),dot(inverseBoxes[i*3+2],float4(p-start,0)));
            float low=0,high=1;
            bool hit=slab(a.x,d.x,low,high);
            hit=slab(a.y,d.y,low,high) && hit;
            hit=slab(a.z,d.z,low,high) && hit;
            hide=hide || hit;
        }
    }
    clip(hide?-1:1);
}
)hlsl";
}
ComPtr<ID3DBlob> Compile(const std::string& source,const char* profile) {
    ComPtr<ID3DBlob> code,error;
    HRESULT hr=D3DCompile(source.data(),source.size(),"LeafClip",nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(hr)) throw std::runtime_error(error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"D3DCompile failed");
    return code;
}
std::vector<uint8_t> PatchPixelShader(const void* data,size_t size,bool useAnimatedWorldPosition) {
    auto chunks=Read(data,size);
    auto original=Instructions(Program(chunks).words);
    // ISGN entries are 24 bytes. Reject newer signatures until independently tested.
    auto& signature=Find(chunks,Tag('I','S','G','N')).words;
    Require(signature.size()>=2 && signature[0]<32 && 2+signature[0]*6<=signature.size(),"unsupported input signature");
    unsigned pos=UINT_MAX,primitive=UINT_MAX,next=0;
    for(unsigned i=0;i<signature[0];++i) {
        size_t o=2+i*6; unsigned sys=signature[o+2],reg=signature[o+4];
        next=std::max(next,reg+1);
        if(sys==D3D_NAME_POSITION) {pos=reg; signature[o+5]=(signature[o+5]&0xffff0000u)|0x0f0f;}
        if(sys==D3D_NAME_PRIMITIVE_ID) primitive=reg;
    }
    Require(pos!=UINT_MAX,"pixel shader has no SV_Position; leaving original shader intact");
    bool addPrimitive=primitive==UINT_MAX;
    if(addPrimitive) {
        Require(next<32,"no free input register"); primitive=next;
        size_t oldRecordsEnd=2+signature[0]*6;
        for(unsigned i=0;i<signature[0];++i) signature[2+i*6]+=24;
        signature.insert(signature.begin()+oldRecordsEnd,6,0);
        unsigned nameOffset=unsigned(signature.size()*4);
        const char name[]="SV_PrimitiveID";
        size_t bytes=sizeof(name),old=signature.size(); signature.resize(old+(bytes+3)/4,0);
        memcpy(signature.data()+old,name,bytes);
        signature[oldRecordsEnd]=nameOffset;
        signature[oldRecordsEnd+2]=D3D_NAME_PRIMITIVE_ID;
        signature[oldRecordsEnd+3]=D3D_REGISTER_COMPONENT_UINT32;
        signature[oldRecordsEnd+4]=primitive;
        signature[oldRecordsEnd+5]=0x0101;
        ++signature[0];
    }
    // Valheim's sampled vegetation variants use SM4 despite running on D3D11.
    // Keep their original shader model; compile only our helper for that model.
    unsigned version=Program(chunks).words[0]&0xff;
    static const auto helper40=Compile(Helper,"ps_4_0");
    static const auto helper41=Compile(Helper,"ps_4_1");
    static const auto helper50=Compile(Helper,"ps_5_0");
    auto makeWorldSource=[] {
        std::string source=Helper;
        auto replace=[&](const char* before,const char* after) {
            size_t at=source.find(before);
            Require(at!=std::string::npos,"world-position helper source changed");
            source.replace(at,strlen(before),after);
        };
        replace("void main(float4 screen:SV_Position,uint primitive:SV_PrimitiveID)",
            "void main(float4 worldX:TEXCOORD1,float4 worldY:TEXCOORD2,float4 worldZ:TEXCOORD3,uint primitive:SV_PrimitiveID)");
        replace("    float2 xy=(screen.xy-viewport.xy)/viewport.zw;\n"
                "    float4 hp=mul(inverseVP,float4(xy.x*2-1,1-xy.y*2,screen.z,1));\n"
                "    float3 p=hp.xyz/hp.w;",
                "    float3 p=float3(worldX.w,worldY.w,worldZ.w);");
        return source;
    };
    static const auto worldSource=makeWorldSource();
    static const auto world40=Compile(worldSource,"ps_4_0");
    static const auto world41=Compile(worldSource,"ps_4_1");
    static const auto world50=Compile(worldSource,"ps_5_0");
    auto helper=useAnimatedWorldPosition
        ?(version==0x40?world40:version==0x41?world41:world50)
        :(version==0x40?helper40:version==0x41?helper41:helper50);
    auto helperChunks=Read(helper->GetBufferPointer(),helper->GetBufferSize());
    auto extra=Instructions(Program(helperChunks).words);
    const auto& helperSignature=Find(helperChunks,Tag('I','S','G','N')).words;
    Require(helperSignature.size()>=2 && 2+helperSignature[0]*6<=helperSignature.size(),"invalid helper signature");
    auto name=[](const Words& words,size_t record) {
        unsigned offset=words[record];
        Require(offset<words.size()*4,"invalid semantic name offset");
        const char* first=reinterpret_cast<const char*>(words.data())+offset;
        const char* end=reinterpret_cast<const char*>(words.data())+words.size()*4;
        const char* zero=std::find(first,end,'\0');
        Require(zero!=end,"unterminated semantic name");
        return std::string(first,zero);
    };
    std::vector<unsigned> inputs(32,UINT_MAX);
    std::array<unsigned,32> requiredMasks{};
    for(unsigned i=0;i<helperSignature[0];++i) {
        size_t h=2+i*6;
        unsigned helperRegister=helperSignature[h+4];
        Require(helperRegister<inputs.size(),"helper input register out of range");
        for(unsigned j=0;j<signature[0];++j) {
            size_t o=2+j*6;
            if(helperSignature[h+1]!=signature[o+1] || name(helperSignature,h)!=name(signature,o))continue;
            unsigned originalRegister=signature[o+4];
            Require(originalRegister<requiredMasks.size(),"original input register out of range");
            Require(((signature[o+5]&0xffu)&(helperSignature[h+5]&0xffu))==(helperSignature[h+5]&0xffu),
                "animated-position varying components missing");
            inputs[helperRegister]=originalRegister;
            requiredMasks[originalRegister]|=helperSignature[h+5]&0xffu;
            break;
        }
        Require(inputs[helperRegister]!=UINT_MAX,"required animated-position varying missing");
    }
    unsigned originalTemps=0,helperTemps=0;
    size_t body=0;
    while(body<original.size() && Declaration(original[body].Op())) {
        auto& in=original[body];
        if(in.Op()==D3D10_SB_OPCODE_DCL_TEMPS) originalTemps=in.words.at(1);
        if(in.Op()==D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) Require(in.words.at(2)!=13,"b13 already occupied");
        if(in.Op()==D3D10_SB_OPCODE_DCL_RESOURCE) Require(in.words.at(2)<125,"t125-t127 already occupied");
        if(in.Op()==D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS)
            Require((in.words[0]&D3D11_SB_GLOBAL_FLAG_FORCE_EARLY_DEPTH_STENCIL)==0,"forced early depth writes unsupported");
        if((in.Op()==D3D10_SB_OPCODE_DCL_INPUT_PS_SIV ||
            in.Op()==D3D10_SB_OPCODE_DCL_INPUT_PS_SGV ||
            in.Op()==D3D10_SB_OPCODE_DCL_INPUT_PS) && in.words.size()>2 && in.words[2]<requiredMasks.size()) {
            unsigned mask=((in.words[1]>>4)&15u)|requiredMasks[in.words[2]];
            in.words[1]=(in.words[1]&~D3D10_SB_OPERAND_4_COMPONENT_MASK_MASK)|(mask<<4);
        }
        ++body;
    }
    for(const auto& in:extra) if(in.Op()==D3D10_SB_OPCODE_DCL_TEMPS) helperTemps=in.words.at(1);
    Require(originalTemps+helperTemps<=4096,"temporary register budget exceeded");
    Words result{Program(chunks).words[0],0};
    auto append=[&](const Instruction& in){result.insert(result.end(),in.words.begin(),in.words.end());};
    for(size_t i=0;i<body;++i) if(original[i].Op()!=D3D10_SB_OPCODE_DCL_TEMPS) append(original[i]);
    result.push_back(D3D10_SB_OPCODE_DCL_TEMPS | (2u<<24)); result.push_back(originalTemps+helperTemps);
    for(auto in:extra) {
        unsigned op=in.Op();
        if(op==D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER || op==D3D10_SB_OPCODE_DCL_RESOURCE ||
            (addPrimitive && op==D3D10_SB_OPCODE_DCL_INPUT_PS_SGV)) {
            Relocate(in,originalTemps,inputs); append(in);
        }
    }
    for(auto in:extra) {
        if(Declaration(in.Op()) || in.Op()==D3D10_SB_OPCODE_RET) continue;
        Relocate(in,originalTemps,inputs); append(in);
    }
    for(size_t i=body;i<original.size();++i) append(original[i]);
    result[1]=unsigned(result.size()); Program(chunks).words=std::move(result);
    // Reflection/statistics describe the old body. The executable declarations and
    // signatures are authoritative; don't ship stale reflection alongside them.
    chunks.erase(std::remove_if(chunks.begin(),chunks.end(),[](const Chunk& c){return c.tag==Tag('R','D','E','F') || c.tag==Tag('S','T','A','T');}),chunks.end());
    Words container(8+chunks.size(),0);
    container[0]=Tag('D','X','B','C'); container[5]=1; container[7]=unsigned(chunks.size());
    for(size_t i=0;i<chunks.size();++i) {
        container[8+i]=unsigned(container.size()*4); container.push_back(chunks[i].tag);
        container.push_back(unsigned(chunks[i].words.size()*4));
        container.insert(container.end(),chunks[i].words.begin(),chunks[i].words.end());
    }
    container[6]=unsigned(container.size()*4);
    DWORD checksum[4]; Require(CalculateDXBCChecksum((BYTE*)container.data(),container[6],checksum)!=FALSE,"checksum failed");
    memcpy(container.data()+1,checksum,16);
    std::vector<uint8_t> bytes(container.size()*4); memcpy(bytes.data(),container.data(),bytes.size()); return bytes;
}
}
