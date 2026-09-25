#pragma once
#include <cstdint>
#ifdef LEAFCLIP_EXPORTS
#define LC_API extern "C" __declspec(dllexport)
#else
#define LC_API extern "C" __declspec(dllimport)
#endif
// All functions use the x64 Windows ABI. Unity objects stay owned by managed code;
// the native side retains its own COM references until unregister/shutdown.
LC_API int LC_Initialize(void* d3dResource);
// D3D12 capture probe; never modifies a draw until PSO/root bindings are validated.
LC_API int LC_Initialize12(void* d3dResource);
LC_API int LC_Enable12(int enabled);
LC_API int LC_SetFallbackCamera12(const float* backbufferInverse,const float* textureInverse,int width,int height);
LC_API void LC_GetStats12(uint64_t* values,int count);
LC_API int LC_GetApiVersion();
LC_API int LC_Register(void* indexBuffer,const uint32_t* eligible,int triangleCount);
LC_API void LC_Unregister(void* indexBuffer);
LC_API int LC_SetObstructions(void* indexBuffer,const float* tree,const float* inverseBoxes,int count);
LC_API uint64_t LC_CreateGeometry(const float* boxes,int boxCount,const float* triangles,int triangleCount);
LC_API int LC_BindGeometry(void* indexBuffer,const float* tree,uint64_t geometry);
LC_API void LC_ReleaseGeometry(uint64_t geometry);
LC_API int LC_QueueCamera(const float* inverseViewProjection);
LC_API void* LC_GetRenderEvent();
LC_API int LC_PollLog(char* output,int capacity);
LC_API void LC_GetStats(uint64_t* values,int count);
LC_API void LC_Shutdown();
