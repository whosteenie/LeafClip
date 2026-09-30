#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>

namespace leafclip {
using Microsoft::WRL::ComPtr;
constexpr unsigned MaxBoxes = 85;
struct ClipConstants {
    float inverseViewProjection[16]; // row-major; multiplied by a column vector
    float viewport[4];
    float tree[4];
    uint32_t boxCount, triangleBase, enabled, reserved; // reserved=1: boxCount is BVH node count
    float inverseBoxes[MaxBoxes][12]; // world to unit box, three rows
};
static_assert(sizeof(ClipConstants) % 16 == 0, "constant buffer alignment");
ComPtr<ID3DBlob> Compile(const std::string& source, const char* profile);
// The original shader's executable body is copied verbatim. Only the helper's
// registers are relocated. Unsupported layouts throw and must render unmodified.
std::vector<uint8_t> PatchPixelShader(const void* data, size_t size, bool useAnimatedWorldPosition=false,
    int visualDiagnostic=0);
}
