# LeafClip diagnostic prototype

Client-side investigation for Valheim **1.0.15**, BepInEx 5. This phase inspects trees; **it does not clip foliage yet**. No RPCs, world writes, renderer assignments, shared-asset edits, or server dependency. Each player will eventually need the mod for their own visuals.

The supplied DLLs have been decompiled and the diagnostic plugin builds. The intended clipping behavior applies to both naturally generated and planted trees, without a species or origin setting. No game asset bundles, tree prefabs/meshes, shader source, or runnable game installation were supplied in this repository. In-game evidence is required to learn which foliage streams and LODs can be safely edited across tree layouts. See [assembly findings and prototype design](docs/inspection.md).

## Build

Requires a .NET SDK and .NET Framework 4.7.2 targeting pack. Tested with SDK 5.0.416 on Windows. Place the supplied game/loader DLLs in `lib/` (already present locally; ignored by git).

```powershell
dotnet build src/LeafClip/LeafClip.csproj --configuration Debug
```

Output: `src/LeafClip/bin/Debug/net472/LeafClip.dll`. Install only that DLL under `BepInEx/plugins/LeafClip/` in your client. Do not deploy reference DLLs from `lib/`. This is a diagnostic build, not a release.

## Capture a tree

1. Launch once to generate `BepInEx/config/local.leafclip.diagnostics.cfg`.
2. Set `Enabled = true`, leave `NameFilter` empty, and set `IncludePrefabs = true` to inspect registered prefabs as well. The name filter is only a diagnostic aid; it is not a clipping rule.
3. Enter a world with naturally generated trees nearby. An automatic scan is scheduled ten seconds after detecting world entry and waits until the local player exists. If a tree loads later, increment `ScanRequest` and save the config to rescan. Config changes are read every five seconds on the Unity main thread.
4. Start with `ProbeMeshCopy = false`. Enable it and increment `ScanRequest` to test detached copies of readable meshes. `MaxProbeVertices` bounds each copy; each shared mesh is probed once per scan. No clones are assigned to renderers, and temporary copies are destroyed in `finally`.
5. Read `[LeafClip Diagnostics]` entries in `BepInEx/LogOutput.log`. Send the complete scan, including any truncation line. A planted tree sample is useful later for comparison, but is not required. Set `Enabled = false` when finished.

`MaxTrees` defaults to 12 per category (live trees and prefabs). The log reports counts and truncation for each; narrow the filter or raise the cap if needed. Loaded instances are queued before prefabs. One tree is inspected per frame, but discovery, an individual complex tree, log output, or copying a large mesh can still cause a diagnostic spike. No meshes are rebuilt each frame. The disabled plugin only checks the configuration timestamp every five seconds.

Reports include all child renderers (including inactive children), LOD memberships, material/shader names, material name classification hints, mesh readability, submeshes, vertex attribute formats/dimensions/streams, and CPU-copy results. Name-based hints are **not** authoritative foliage masks. Instantiation plus successful reads does not establish the shader meaning of vertex colors/UVs or verify packed stream reconstruction.

Run the [manual validation checklist](tests/validation.md) before treating this as game-tested. The CLI build cannot exercise Unity native Mesh APIs.
