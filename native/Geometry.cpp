#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <numeric>
#include <stdexcept>
namespace leafclip {
namespace {
float Bits(uint32_t bits) {float f;memcpy(&f,&bits,4);return f;}
struct Primitive {std::array<float,3> low,high;uint32_t payload;};
}
GeometryData BuildGeometry(const float* boxes,unsigned boxCount,const float* triangles,unsigned triangleCount) {
    if(boxCount+uint64_t(triangleCount)>131072 || (boxCount&&!boxes) || (triangleCount&&!triangles))
        throw std::runtime_error("invalid or oversized obstruction geometry");
    GeometryData result;std::vector<Primitive> items;
    auto finite=[](const float* data,unsigned count){for(unsigned i=0;i<count;++i)if(!std::isfinite(data[i]))throw std::runtime_error("nonfinite obstruction geometry");};
    for(unsigned i=0;i<boxCount;++i) {
        const float* b=boxes+i*18;finite(b,18);Primitive item{};
        item.payload=uint32_t(result.primitives.size()/3)|0x80000000u;
        for(unsigned axis=0;axis<3;++axis) {
            item.low[axis]=b[12+axis];item.high[axis]=b[15+axis];
            if(item.low[axis]>item.high[axis])throw std::runtime_error("inverted obstruction bounds");
            result.primitives.push_back({b[axis*4],b[axis*4+1],b[axis*4+2],b[axis*4+3]});
        }
        items.push_back(item);
    }
    for(unsigned i=0;i<triangleCount;++i) {
        const float* t=triangles+i*11;finite(t,11);Primitive item{};item.payload=uint32_t(result.primitives.size()/3);
        for(unsigned axis=0;axis<3;++axis) {
            item.low[axis]=std::min({t[axis],t[axis+3],t[axis+6]});
            item.high[axis]=std::max({t[axis],t[axis+3],t[axis+6]});
        }
        for(unsigned v=0;v<3;++v)result.primitives.push_back({t[v*3],t[v*3+1],t[v*3+2],v==0?t[9]:v==1?t[10]:0});
        items.push_back(item);
    }
    if(items.empty())return result;
    result.nodes.reserve(items.size()*4);
    std::function<void(size_t,size_t)> build=[&](size_t begin,size_t end) {
        auto low=items[begin].low,high=items[begin].high;
        for(size_t i=begin+1;i<end;++i)for(unsigned a=0;a<3;++a){low[a]=std::min(low[a],items[i].low[a]);high[a]=std::max(high[a],items[i].high[a]);}
        size_t offset=result.nodes.size();
        result.nodes.push_back({low[0],low[1],low[2],0});result.nodes.push_back({high[0],high[1],high[2],Bits(0xffffffffu)});
        if(end-begin==1)result.nodes[offset+1][3]=Bits(items[begin].payload);
        else {
            unsigned axis=0;for(unsigned a=1;a<3;++a)if(high[a]-low[a]>high[axis]-low[axis])axis=a;
            size_t middle=begin+(end-begin)/2;
            std::nth_element(items.begin()+begin,items.begin()+middle,items.begin()+end,[axis](const Primitive& a,const Primitive& b){return a.low[axis]+a.high[axis]<b.low[axis]+b.high[axis];});
            build(begin,middle);build(middle,end);
        }
        result.nodes[offset][3]=Bits(uint32_t(result.nodes.size()/2));
    };
    build(0,items.size());return result;
}
}
