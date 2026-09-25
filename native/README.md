# Renderer implementation and validation

LeafClip loads `LeafClip.Native.dll` beside its BepInEx plugin DLL on Windows x64. GPU clipping runs automatically on Direct3D 11 and Direct3D 12. Both DLLs must come from the same build; the native API version is checked at startup.

The native companion captures pixel shaders created after initialization, patches a copy of their bytecode with an additional discard predicate, and keeps the original vegetation shader body and vertex shader. Unsupported shader layouts render unchanged. Selected tree meshes are cloned with identical vertices and indices for private draw identity; materials are cloned with instancing disabled. Their original references are restored when a tree leaves the processing area or the world unloads.

Animated world position is reconstructed from `SV_Position` and the current camera projection. A horizontal segment from the tree root to that position is tested against a stackless hierarchy of structure surfaces. Readable visual meshes define the boundary for structural pieces; their collision geometry is used when the render mesh cannot be read. Boxes use exact oriented slab tests, and mesh faces use two-sided segment/triangle tests. Geometry is shared across a tree's LOD meshes. Changed collider/transform data is captured on the Unity main thread and built on a worker; stale jobs are discarded. Two tree meshes are captured per frame to avoid a single large selection spike. The main-camera depth, depth-normals, G-buffer, forward opaque and forward alpha events are scoped with matching projection conventions. Other draws keep their original D3D11 state. Depth draws with a compatible pixel shader use the same discard predicate; draws without one remain an open case.

## Build and tests

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-gpu-test.ps1
```

This configures/builds the native library, runs the D3D11 and D3D12 WARP rendering tests, builds the BepInEx plugin and stages both DLLs plus license notices in `artifacts/gpu-test/BepInEx/plugins/LeafClip`. Nothing is installed or published. The D3D12 test intercepts a registered indexed draw, patches its PSO on a worker, copies live descriptors, binds the expanded root signature, checks the resulting pixels against the direct patched reference, checks restoration on a following draw, and verifies fence-based resource retirement. The D3D11 tests compare original and patched pixel/depth outputs under animated wind, three projection setups, 257 boxes, a sloped mesh surface, primitive masks, hook state restoration, and teardown. All 408 distinct SM4 pixel shader variants extracted from the locally installed Valheim `Custom/Vegetation` shader also accepted patching and D3D11 shader creation. Those game assets are not shipped. These tests do not substitute for a Valheim runtime check.

## In-game check

Install both staged DLLs together, launch with Direct3D 11, then approach a tree intersecting a player-built wall or roof. Look for `D3D11 clipping active`, `GpuClip target`, queued geometry and increasing `matchedDraws`/`clippedDraws` in `BepInEx/LogOutput.log`. Test a fir first, then other tree layouts. Inspect the visible edge at several angles during strong wind, place/remove a structure, change LOD, and reload the world. `GpuClip.MaxTreeDistance` controls the processing radius; an existing config keeps its previous value. Send the full LeafClip log and images of any remaining overcut, gap, or wind penetration.

For D3D12, install the same staged DLL pair, launch on D3D12, and load a world with a tree intersecting a player-built wall or roof. The log should say `D3D12 clipping active`. Check for `GpuClip target` and increasing `matchedPrivateDraws`, `preparedPSOs`, `clippedDraws` and `retiredDraws`. Inspect the wind-animated edge and record FPS when facing and looking away from a dense tree/building scene.

## Remaining work

- Main-camera depth and depth-normals events are scoped, but depth draws without a patchable pixel shader, shadow maps and reflections can disagree with clipped color and still create artifacts. Their actual Valheim draw paths and projection conventions require runtime validation.
- Visible render meshes provide a tighter boundary, but unreadable, static-batched or oversized meshes fall back to colliders; the log reports fallback box and mesh triangle counts. Surface alpha cutouts and transparent materials are not interpreted.
- Mesh surfaces do not detect a ray whose start and end both lie within a closed structure, or one coplanar with a face. Analytic boxes handle solid containment. Sloped roofs are tested by horizontal crossings and may leave some below-roof intrusions.
- Main trunk classification is heuristic for single-material trees. Branch and trunk cuts have been tested on birch, but exposed branch ends, particles, and shadow consistency still need work.
- Tree selection, main-thread geometry snapshots, private meshes/materials and GPU hierarchy traversal need profiling in a large base. Obstruction snapshots have a 131072-primitive safety budget per tree; an oversized tree's structures are left uncut.
- Native shader capture begins on startup. Pixel shaders created earlier by another loader, unsupported graphics wrappers, instanced draws, geometry/tessellation stages, and non-D3D11/D3D12 graphics APIs are not yet supported. D3D12 performance has improved in the tested fir/core-wood scene, but broader scenes still need profiling. The native module remains pinned after shutdown because another graphics mod may have chained a detour.
