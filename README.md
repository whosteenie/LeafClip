# LeafClip diagnostic and clipping prototype

Client-side investigation for Valheim **1.0.15**, BepInEx 5. Diagnostics are opt-in. An experimental foliage clipping path is also included and **disabled by default**. It changes only local tree instance MeshFilter references; it does not change RPCs, world data, shared assets, or the server. Each player needs the mod for their own visuals.

The supplied DLLs were decompiled and two in-game diagnostic logs were inspected. The clipping code applies the same rules to naturally generated and planted trees without a species or origin setting. For now, it only edits readable meshes with a separate material whose name contains `leaf`, `leaves`, or `foliage`. Mixed trunk/foliage submeshes such as the sampled pine and fir remain unchanged. Particle leaves are outside this prototype. See [assembly findings and prototype design](docs/inspection.md).

## Build

Requires a .NET SDK and .NET Framework 4.7.2 targeting pack. Tested with SDK 5.0.416 on Windows. Place the supplied game/loader DLLs in `lib/` (already present locally; ignored by git).

```powershell
dotnet build src/LeafClip/LeafClip.csproj --configuration Debug
```

Output: `src/LeafClip/bin/Debug/net472/LeafClip.dll`. Install only that DLL under `BepInEx/plugins/LeafClip/` in your client. Do not deploy reference DLLs from `lib/`. This is an experimental build, not a release.

## Test experimental clipping

The new build has not yet been run in Valheim. Start with a test world and a normal player-built box-collider wall close to a beech, birch, or YggaShoot tree. The diagnostic logs show separate leaf material slots for these trees. Set `Prototype.Enabled = true` in `BepInEx/config/local.leafclip.diagnostics.cfg`. `Prototype.WallNameContains = wall` restricts obstruction pieces to names containing `wall`; set it to a different substring if your wall prefab is named differently. `Prototype.Clearance` starts at 0.5 m, `Prototype.SearchRadius` at 18 m, and `Prototype.MaxTreeDistance` at 100 m from the local player. The last value is a prototype work limit, not a confirmed Valheim render-distance setting. Trees entering or leaving that range are checked every five seconds; leaving restores their original mesh. Restarting the client after enabling gives lifecycle hooks a clean run.

Look for `Clipped ...` or `Skipped ...` entries and check both close and distant LODs. Expected result for supported meshes: leaf triangles behind the wall disappear while trunk geometry and leaf triangles on the trunk side remain. Add/remove a wall after load; the affected tree should rebuild. With `Prototype.Enabled = false`, generated meshes are restored/released. Pine and fir in the current log should show no clipping because their sampled mesh has a single mixed material. No `Clipped ...` line for a supported tree means the tree, wall, or foliage-slot filter did not match, or no triangles crossed the computed obstruction. A diagnostic log and a short description of what changed visually are needed before broadening support. This prototype is deliberately guarded: unknown layouts are not clipped, and a successful build is not a visual test.

## Capture a tree

1. Launch once to generate `BepInEx/config/local.leafclip.diagnostics.cfg`.
2. Set `Enabled = true`, leave `NameFilter` empty, and set `IncludePrefabs = true` to inspect registered prefabs as well. The name filter is only a diagnostic aid; it is not a clipping rule.
3. Enter a world with naturally generated trees nearby. An automatic scan is scheduled ten seconds after detecting world entry and waits until the local player exists. If a tree loads later, increment `ScanRequest` and save the config to rescan. Config changes are read every five seconds on the Unity main thread.
4. Start with `ProbeMeshCopy = false`. Enable it and increment `ScanRequest` to test detached copies of readable meshes. `MaxProbeVertices` bounds each copy; each shared mesh is probed once per scan. No clones are assigned to renderers, and temporary copies are destroyed in `finally`.
5. Read `[LeafClip Diagnostics]` entries in `BepInEx/LogOutput.log`. Send the complete scan, including any truncation line. A planted tree sample is useful later for comparison, but is not required. Set `Enabled = false` when finished.

`MaxTrees` defaults to 12 per category (live trees and prefabs). The log reports counts and truncation for each; narrow the filter or raise the cap if needed. Loaded instances are queued before prefabs. One tree is inspected per frame, but discovery, an individual complex tree, log output, or copying a large mesh can still cause a diagnostic spike. No meshes are rebuilt each frame. The disabled plugin only checks the configuration timestamp every five seconds.

Reports include all child renderers (including inactive children), LOD memberships, material/shader names, material name classification hints, mesh readability, submeshes, vertex attribute formats/dimensions/streams, and CPU-copy results. Name-based hints are **not** authoritative foliage masks. Instantiation plus successful reads does not establish the shader meaning of vertex colors/UVs or verify packed stream reconstruction.

Run the [manual validation checklist](tests/validation.md) before treating this as game-tested. The CLI build cannot exercise Unity native Mesh APIs.
