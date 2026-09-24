# LeafClip diagnostic and clipping prototype

Client-side investigation for Valheim **1.0.15**, BepInEx 5. Diagnostics are opt-in. An experimental foliage clipping path is also included and **disabled by default**. It changes only local tree instance MeshFilter references; it does not change RPCs, world data, shared assets, or the server. Each player needs the mod for their own visuals.

The supplied DLLs were decompiled and in-game diagnostic logs were inspected. The clipping code applies the same rules to naturally generated and planted trees without a species or origin setting. It edits readable meshes with either a separate leaf material or a guarded topology-based canopy selection for single-material vegetation meshes, including the sampled pine and fir layouts. Particle leaves are outside this prototype. See [assembly findings and prototype design](docs/inspection.md).

## Build

Requires a .NET SDK and .NET Framework 4.7.2 targeting pack. Tested with SDK 5.0.416 on Windows. Place the supplied game/loader DLLs in `lib/` (already present locally; ignored by git).

```powershell
dotnet build src/LeafClip/LeafClip.csproj --configuration Debug
```

Output: `src/LeafClip/bin/Debug/net472/LeafClip.dll`. Install only that DLL under `BepInEx/plugins/LeafClip/` in your client. Do not deploy reference DLLs from `lib/`. This is an experimental build, not a release.

## Test experimental clipping

Start with a test world and a normal player-built box-collider wall close to a pine or fir. Also compare a beech or birch, which uses a separate leaf material. Set `Prototype.Enabled = true` in `BepInEx/config/local.leafclip.diagnostics.cfg`. `Prototype.WallNameContains = wall` selects wall names; `Prototype.IncludeBeamsAndPoles = true` and `Prototype.IncludeRoofs = true` (defaults) also select player-built beams, poles, and roofs with box colliders. `Prototype.Clearance` defaults to 0.05 m, `Prototype.WindGuard` to 0.12 m, `Prototype.SearchRadius` to 18 m horizontally from the tree root, and `Prototype.MaxTreeDistance` to 64 m from the local player. **Existing config files retain their old `Clearance` value; set it to `0.05` manually to test the tighter edge.** The player distance is a prototype work limit, not a confirmed Valheim render-distance setting. Trees entering or leaving that range are checked every five seconds; leaving restores their original mesh. Restarting the client after enabling gives lifecycle hooks a clean run.

Look for `Clipped ...` or `Skipped ...` entries and check both close and distant LODs. Expected result for supported meshes: foliage inside a piece and farther behind it disappears only within that piece's height; foliage above it and on the trunk side remains. Shadows extend horizontally from the trunk at foliage height. Angled roof box colliders are divided into at most four height slices, reducing cuts beyond the roof's sloped surface. Each `Clipped ...` line reports the tree's mesh LOD count, `topology` count, detected wall/beam/pole/roof box counts, and wind allowance. Pine/fir should show `topology=1` or more when their detached canopy is clipped. Nearby pieces and foliage triangles that cannot intersect a shadow are rejected before mesh splitting. If roof slices would exceed the work budget, a `Deferred ... roof face volume(s)` line appears and wall/beam clipping still runs; a remaining whole-tree budget skip reports box, face, and estimated triangle-face counts. Rebuilds taking at least 8 ms get a `Slow rebuild ...` entry. Add/remove a wall, beam, or roof after load; the affected tree should rebuild. With `Prototype.Enabled = false`, generated meshes are restored/released. Roofs represented only by mesh colliders remain unsupported. The roof and wind changes still need an in-game visual and frame-time test.

`Prototype.ClipWoodBehindWalls` is a separate experiment, off by default. Enabling it also clips birch/beech bark or trunk material triangles behind a piece, including branch triangles, but the mesh does not distinguish branches from the main trunk. The pine/fir topology fallback already includes detached branches while preserving the connected main trunk. Review the whole tree for unintended cuts. LeafClip keeps the original vegetation shader and wind attributes. `WindGuard` expands only the horizontal cut by a small, fixed amount when a mesh is rebuilt, avoiding per-frame CPU clipping; it reduces wind penetration but cannot exactly track the shader's animation. A larger value may create a visible gap, so compare 0, 0.12, and 0.25 m in-game. It does not expand the piece's top or bottom.

## Capture a tree

1. Launch once to generate `BepInEx/config/local.leafclip.diagnostics.cfg`.
2. Set `Enabled = true`, leave `NameFilter` empty, and set `IncludePrefabs = true` to inspect registered prefabs as well. The name filter is only a diagnostic aid; it is not a clipping rule.
3. Enter a world with naturally generated trees nearby. An automatic scan is scheduled ten seconds after detecting world entry and waits until the local player exists. If a tree loads later, increment `ScanRequest` and save the config to rescan. Config changes are read every five seconds on the Unity main thread.
4. Start with `ProbeMeshCopy = false`. Enable it and increment `ScanRequest` to test detached copies of readable meshes. `MaxProbeVertices` bounds each copy; each shared mesh is probed once per scan. No clones are assigned to renderers, and temporary copies are destroyed in `finally`.
5. Read `[LeafClip Diagnostics]` entries in `BepInEx/LogOutput.log`. Send the complete scan, including any truncation line. A planted tree sample is useful later for comparison, but is not required. Set `Enabled = false` when finished.

`MaxTrees` defaults to 12 per category (live trees and prefabs). The log reports counts and truncation for each; narrow the filter or raise the cap if needed. Loaded instances are queued before prefabs. One tree is inspected per frame, but discovery, an individual complex tree, log output, or copying a large mesh can still cause a diagnostic spike. No meshes are rebuilt each frame. The disabled plugin only checks the configuration timestamp every five seconds.

Reports include all child renderers (including inactive children), LOD memberships, material/shader names, material name classification hints, mesh readability, submeshes, vertex attribute formats/dimensions/streams, and CPU-copy results. Name-based hints are **not** authoritative foliage masks. Instantiation plus successful reads does not establish the shader meaning of vertex colors/UVs or verify packed stream reconstruction.

Run the [manual validation checklist](tests/validation.md) before treating this as game-tested. The CLI build cannot exercise Unity native Mesh APIs.
