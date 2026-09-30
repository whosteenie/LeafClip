# LeafClip

Client-side foliage clipping for Valheim 1.0.15 and BepInEx 5. The plugin keeps the game's vegetation vertex shader and evaluates clipping from the animated pixel position. Unassigned foliage uses a horizontal ray from the tree to each pixel. Leaf groups with a bark attachment follow a bounded path along their parent branches and then a local attachment-to-pixel segment. This keeps oak foliage when its supporting branch clears a wall, while clipping a child when its parent crosses the wall or its own leaves intersect a roof. Eligible branches retain per-pixel clipping. Foliage above a structure remains visible.

This is active development. GPU clipping runs automatically on Windows x64 with Direct3D 11 or Direct3D 12. `GpuClip.MaxTreeDistance` limits processing near the player; it is 64 m by default, while existing config files retain their saved value. Trees are selected only when a player structure is near their canopy. Species and planted/generated origin do not select a code path.

The structure boundary uses readable render meshes where available, so leaves are not cut back to a wider physics collider. Box and mesh colliders provide a fallback. The GPU tests cover original wind motion, a 257-box hierarchy, and a sloped mesh surface. D3D12 clipping has also been tested in Valheim with wind-animated fir and birch. Main-camera color and compatible depth shader passes are covered; shadow and reflection passes, particle leaves, and unreadable structural render meshes remain open work. Depth-only draws without a pixel shader still need runtime investigation. See [technical and test details](native/README.md).

## Build

Requires VS 2022 C++ tools, CMake, a .NET SDK, and the ignored Valheim/BepInEx references in `lib/`. Build and stage a development package with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

Both DLLs and third-party license notices are staged under `artifacts/package/BepInEx/plugins/LeafClip`. The script does not install into the game. Install both DLLs together for a test. No server mod or world-data change is needed.

## Testing

Enter a world with a fir, pine, birch, or beech intersecting a player-built wall or roof. Check the edge beside the visible structure and the tree's motion during wind. The console/log should report `D3D11 clipping active` or `D3D12 clipping active`, a `GpuClip target` for the tree, and queued structure geometry. On D3D11, `matchedDraws` and `clippedDraws` should increase; on D3D12, check `matchedPrivateDraws`, `preparedPSOs`, `clippedDraws` and `retiredDraws`. If these lines do not appear, the tree is still unprocessed. Add and remove a structure, then compare close and distant LODs. Send the complete LeafClip log along with any remaining gap or overcut case.

LeafClip creates `BepInEx/config/local.leafclip.cfg` on first launch with the current plugin version in its header. Optional tree scans and two D3D12 visual diagnostics are configured in its `[Diagnostics]` section. `VisualizeClipping` colors clipping decisions; `VisualizeBranchAssignment` colors candidate bark branches and their assigned leaf clusters. Both default to `false` and require a restart when changed. If both are enabled, branch assignment takes priority.
