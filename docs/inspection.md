# Supplied assembly inspection — 2026-09-24

## Evidence and limits

Inspected this new repository only; ItemStandFix was not read or reused. No `AGENTS.md` was present in the repository or checked ancestor directories. Initial repository contents were empty `src`, `tests`, and `docs` folders, five DLLs in `lib`, LICENSE, and .gitignore. No serialized game assets were present.

ILSpy CLI 7.2.1.6856 was installed locally into ignored `.tools/`. Decompiled source is local-only under ignored `artifacts/inspection/`; proprietary source and DLLs are not redistributed. Reproduce individual inspections with:

```powershell
.\.tools\ilspycmd.exe -t TreeBase -r lib lib\assembly_valheim.dll
```

Inspected types: `TreeBase`, `Plant`, `Piece`, `WearNTear`, `ZNetScene`, `ZoneSystem`, `Version`, `UnityEngine.Mesh`, `BepInEx.BaseUnityPlugin`, and `HarmonyLib.Harmony`. The missing PhysicsModule and other transitive dependencies cause incomplete physics type rendering in decompiled output; do not mistake generated `get_*` syntax for original source.

`Version.CurrentVersion` initializes `new GameVersion(1, 0, 15)`. Loader assembly identity is BepInEx 5.4.23.5; Harmony assembly identity is 2.9.0.0. Harmony exposes `Patch`, `PatchAll`, and `UnpatchSelf`. Diagnostics do not need patches and do not reference Harmony at runtime.

SHA-256 provenance:

| File | Hash |
| --- | --- |
| assembly_valheim.dll | 59F53FB55D99D22A33E8ED094EEC8D21E9F133543BCE92BC3D80DCE44033ADB1 |
| BepInEx.dll | F09821B2A7B990C6F50C5EF23229635303CE675374B70EDC6F5A9B960CB818E3 |
| 0Harmony.dll | 1A21CC03424FC82C3DD1346905D16494536B9595AE4162228D99FB7C285C1031 |
| UnityEngine.CoreModule.dll | FBA3821AFB6867FE4D471DC50BF04BA21E6982B33AB6B5336B08A5C55119D990 |
| UnityEngine.dll | 2F43705EBD2E3726BA06FB054BEA23C265F9168BF6D16CAD21789CDBE0E1B179 |

## Confirmed lifecycle behavior

| Code | Finding and consequence |
| --- | --- |
| `TreeBase.Awake` | Acquires ZNetView and registers damage/grow/shake RPC handlers. An owner can immediately destroy an already-dead tree. A postfix can enqueue valid live instances; defer processing until scale and growth settle. |
| `TreeBase.Grow`, `RPC_Grow`, `GrowAnimation` | Growth is broadcast through an existing RPC. A temporary `m_trunk` hierarchy clone animates for 0.3 seconds; the original is disabled and reenabled. Root LOD fade is changed to None. Do not clip that temporary clone or assume Awake-time renderer state is final. |
| `TreeBase.ShakeAnimation` | Changes trunk rotation for one second and toggles static state. Static clipping can cross a wall while shaking as well as during shader wind. |
| `TreeBase.RPC_Damage` | On lethal damage spawns log/stub prefabs, disables the tree GameObject, and destroys its ZNetView. There is no `TreeBase.OnDestroy` implementation to patch in this DLL. Use an instance-owned companion component's OnDestroy for clone cleanup. |
| `Plant.Grow` | Instantiates a selected `m_grownPrefabs` entry, applies randomized scale through ZNetView, calls TreeBase.Grow if present, destroys the sapling, and returns the grown GameObject. |
| `ZoneSystem.PlaceVegetation` | Instantiates vegetation prefabs and applies scale for networked vegetation. Natural generation and planting can therefore lead to TreeBase instances, but prefab equivalence is an asset question. |
| `Piece.Awake` | Registers the piece and reads creator identity from its ZDO. Good load notification, but new placement can precede creator assignment. |
| `Piece.SetCreator`, `IsPlacedByPlayer` | SetCreator writes creator data for a locally owned piece; IsPlacedByPlayer tests `GetCreator() != 0`. Use creator/valid ZNetView checks, not layer alone, to exclude natural ruins and placement ghosts. Remote ordering needs runtime validation/retry. |
| `Piece.OnDestroy`, `WearNTear.OnDestroy` | Remove instance registrations. Piece lifecycle catches removal/unload; notify nearby trees using previously recorded bounds, before the collider disappears. |
| `WearNTear.Destroy` | Routes removal through ZNetScene.Destroy. |
| `ZNetScene.CreateObject`, `RemoveObjects`, `OnZDODestroyed`, `Shutdown` | Instantiate network objects; unload distant instances after ResetZDO; destroy remotely deleted objects; destroy all scene instances on shutdown. Cleanup cannot depend on a still-valid ZDO in OnDestroy. |

The inspected mature-tree path has no explicit persistent planted-origin flag. Observing Plant.Grow can identify local growth during the session, but is not proof of origin for remote growth or after reload. The clipping algorithm must not depend on origin. A new synchronized marker would exceed this visual-only scope.

## Colliders and layers

Confirmed string masks: Plant's attachment query uses `piece`; its roof query uses `Default`, `static_solid`, `piece`; Piece ray queries use `piece` and `piece_nonsolid`; placement ghosts use `ghost`. Runtime layer numbers are not encoded in these calls; the diagnostic reports resolved IDs.

Proposed first obstruction filter: enabled non-trigger BoxCollider on the runtime `piece` layer, valid parent Piece, IsPlacedByPlayer true, and an explicitly confirmed solid wall prefab. This is a proposed filter, not evidence that all roofs/walls have box colliders or share this layer. Exclude `piece_nonsolid`, ghosts, furniture and decorative colliders unless separately validated. Collider layouts require actual prefabs. `UnityEngine.PhysicsModule.dll` must be supplied for a directly typed physics prototype; it is absent here.

## Mesh access, foliage, LODs

Confirmed in supplied CoreModule: Mesh exposes attribute descriptors (attribute, format, dimension, stream), submesh topology/indices/base vertices, UV0–UV7 access, and read-only MeshData acquisition. MeshData acquisition checks `canAccess` and throws for inaccessible meshes. Conventional getters likewise guard access and may return empty arrays with an engine error. `Instantiate(mesh)` is not a documented read/write unlock; diagnostics skip unreadable meshes.

Safe first behavior is to leave an unreadable mesh untouched. A legitimately supplied readable asset/export could support an offline conversion. GPU buffer readback would require further API/reference inspection, exact buffer layout handling, staging allocations and synchronization; it is a separate experiment, not implemented or guaranteed here. Do not patch Unity access flags or modify importer/shared prefab settings in a live client.

**Unknown until runtime inspection:** species names, renderer hierarchy, foliage material slots, whether trunk and foliage share a submesh, mesh readability, actual UV/color wind encoding, skinning/blend shapes, LOD count, and billboard use. TreeBase's `m_trunk` is a hierarchy reference, not a reliable trunk-only geometry label. DLL code alone cannot confirm these asset details. Material names are logged as hints only. The copy probe exercises common CPU streams and indices, not every raw byte or variable bone influence.

Risks to evaluate:

- Static batches may not reflect a MeshFilter replacement; reject static-batched candidates until experimentally validated. Per-instance copies reduce geometry sharing and may reduce instancing/batching benefits.
- Keep original materials, material property blocks, shader keywords, alpha cutoff and lighting. Never access Renderer.material or MeshFilter.mesh during inspection.
- Every supported mesh LOD must use matching clipping logic. BillboardRenderer and impostor textures need separate support; do not silently hide them. Bounds and LOD crossfades must be checked at distance.
- Clipping changes shadows as well as visible geometry. Check shadow casting and two-sided foliage cards.
- Shader wind, trunk shake and growth can move vertices across a statically computed boundary. Start clearance at 0.5 m, tune within roughly 0.25–0.75 m, and explicitly accept this limitation for the first prototype.
- Seasonal/biome material or mesh swaps are not established by this inspection. Validate them in the installed game; invalidate caches if source meshes change, and preserve material changes rather than holding stale copies.

## Proposed second-phase implementation (not implemented)

Proceed after diagnostics reveal the foliage layouts and streams present in the game, plus one solid player-built box wall. Select foliage geometry from inspected renderer/material/mesh characteristics, without a tree species or planted-origin gate. A single representative tree is sufficient to exercise the geometry algorithm, but broader runtime coverage across distinct layouts is required before claiming that every tree works. Provide matching PhysicsModule references and actual runtime logs/assets. Leave unsupported skinning, blend shapes, packed attributes, mixed trunk/leaf submeshes, or billboard LODs unchanged until their preservation is proven, and report those gaps explicitly.

For a point trunk origin and an oriented box, the exact shadow volume is generally a **diverging convex volume**, not a parallel extrusion. Construct silhouette side planes through the trunk and box silhouette edges, with appropriate trunk-facing entry planes; bound extension beyond the canopy. Validate the equivalent segment-from-trunk intersection predicate. Handle origin-in-box and degenerate configurations explicitly. Define clearance as a conservative expansion of the wall obstacle (including side boundaries) before constructing the shadow; confirm that this intentional extra removal looks acceptable.

Subtract each convex obstruction from each allowed foliage triangle. Split polygons at planes, retain outside fragments, continue testing the still-inside fragment, and discard only the final fragment inside the volume. Repeat subtraction for the union of multiple walls. Triangulate retained polygons without changing winding. Interpolate all confirmed continuous streams using the same edge parameter, normalize normals and tangent directions appropriately, handle tangent handedness explicitly, and preserve UV dimensions and color precision. Do not reconstruct unsupported streams by guessing. Preserve all untouched submeshes/material slots and choose an index format supporting the expanded vertex count. Never recalculate normals in a way that destroys authored foliage shading.

Clone only affected meshes on the individual instance and retain originals. Cache by live tree identity, source mesh/LOD, tree transform, obstruction transforms/shapes, clearance and settings. Keep an instance-owned cleanup component; restore only if the renderer still references our generated mesh, then destroy that mesh. Do not overwrite another mod's replacement.

Harmony postfixes on TreeBase.Awake and structure lifecycle methods can populate a spatial index and a deduplicated dirty queue. Defer new trees through growth; seed trees already loaded. Notify on Piece.Awake/SetCreator/OnDestroy, supplemented by bounded checks for transforms, dynamic doors, remote creator changes and renderer swaps. Use both per-frame work and elapsed-time limits, vertex/allocation caps, and cancellable work for large meshes; restore originals when the mod is disabled. No ZDO writes or custom RPCs. Debug volume drawing is optional and disabled by default.

## Validation status

The first in-game log from 2026-09-24 confirms BepInEx loaded the diagnostic plugin and reports Unity 6000.0.75f1 / Valheim 1.0.15. It found 24 matching trees and inspected 12; all 12 logged roots had an empty scene name and inactive renderers, consistent with prefab samples while the player was still respawning. The original scan cap mixed live instances and prefabs. The scanner now waits for `Player.m_localPlayer`, restricts live discovery to loaded scenes, reports live/prefab counts separately, and budgets both groups independently. This fix has compiled but awaits an in-game rerun.

The sampled Beech1 prefab has three mesh LODs, two submeshes per LOD, and likely leaf/bark material slots. Birch prefabs have two mesh LODs, but the leaf and bark slot order changes between LODs. All sampled meshes were readable and detached Mesh API copy/read probes succeeded. The sampled vertex descriptors were Float32 position, normal, tangent and UV0; colors and additional UVs were absent. These are runtime observations for sampled prefabs, not a claim about every tree. Beech and autumn birch also have leaf particle renderers outside MeshFilter/SkinnedMeshRenderer handling. Ashlands and Fir samples include single-material meshes whose foliage/trunk split is not identified by the log. Thus a universal implementation must inspect material slots per renderer and handle mixed geometry/particle cases explicitly.

Debug compilation against the supplied references passed with zero warnings and errors. Clipping and live-tree scan behavior have not been validated in the game. See `tests/validation.md` for capture and geometry acceptance checks.
