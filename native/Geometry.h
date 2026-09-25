#pragma once
#include "ShaderPatch.h"
#include <array>
namespace leafclip {
// Stackless preorder hierarchy. Escape is the first node beyond this subtree.
// Node storage is uploaded as uint4: xyz contains float bits, w contains integers.
// A leaf payload has three float4s: inverse box rows or triangle vertices.
struct GeometryData {
    std::vector<std::array<float,4>> nodes, primitives;
};
// Boxes: 12 inverse-transform floats followed by world AABB min/max (18 total).
// Triangles: three world-space positions (9 floats). Inputs are copied.
GeometryData BuildGeometry(const float* boxes, unsigned boxCount, const float* triangles, unsigned triangleCount);
}
