# Supplied assembly inspection — 2026-09-24

## Evidence and limits

Inspected this new repository only; ItemStandFix was not read or reused. No `AGENTS.md` was present in the repository or checked ancestor directories. Initial repository contents were empty `src`, `tests`, and `docs` folders, five DLLs in `lib`, LICENSE, and .gitignore. No serialized game assets were present.

ILSpy CLI 7.2.1.6856 was installed locally into ignored `.tools/`. Decompiled source is local-only under ignored `artifacts/inspection/`; proprietary source and DLLs are not redistributed. Reproduce individual inspections with:

```powershell
.\.tools\ilspycmd.exe -t TreeBase -r lib lib\assembly_valheim.dll
```

Inspected types: `TreeBase`, `Plant`, `Piece`, `WearNTear`, `ZNetScene`, `ZoneSystem`, `Version`, `UnityEngine.Mesh`, `BepInEx.BaseUnityPlugin`, and `HarmonyLib.Harmony`. The initial decompile lacked PhysicsModule and some other transitive dependencies, causing incomplete physics type rendering; do not mistake generated `get_*` syntax for original source. The matching PhysicsModule was later copied from the local Valheim installation into ignored `lib/` for the prototype build.

`Version.CurrentVersion` initializes `new GameVersion(1, 0, 15)`. Loader assembly identity is BepInEx 5.4.23.5; Harmony assembly identity is 2.9.0.0. Harmony exposes `Patch`, `PatchAll`, and `UnpatchSelf`. The clipping prototype uses Harmony; diagnostics themselves do not need its patches.

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

First obstruction filter: enabled non-trigger BoxCollider on the runtime `piece` layer, valid parent Piece, IsPlacedByPlayer true, and a configurable wall-name substring. The first log confirmed runtime piece layer 10, piece_nonsolid 16, and ghost 13. This is not evidence that all roofs/walls have box colliders or share this layer. Collider layouts require actual runtime inspection. Exclude `piece_nonsolid`, ghosts, furniture and decorative colliders unless separately validated.

## Mesh access, foliage, LODs

Confirmed in supplied CoreModule: Mesh exposes attribute descriptors (attribute, format, dimension, stream), submesh topology/indices/base vertices, UV0–UV7 access, and read-only MeshData acquisition. MeshData acquisition checks `canAccess` and throws for inaccessible meshes. Conventional getters likewise guard access and may return empty arrays with an engine error. `Instantiate(mesh)` is not a documented read/write unlock; diagnostics skip unreadable meshes.

Safe first behavior is to leave an unreadable mesh untouched. A legitimately supplied readable asset/export could support an offline conversion. GPU buffer readback would require further API/reference inspection, exact buffer layout handling, staging allocations and synchronization; it is a separate experiment, not implemented or guaranteed here. Do not patch Unity access flags or modify importer/shared prefab settings in a live client.

**Unknown until runtime inspection:** species names, renderer hierarchy, foliage material slots, whether trunk and foliage share a submesh, mesh readability, actual UV/color wind encoding, skinning/blend shapes, LOD count, and billboard use. TreeBase's `m_trunk` is a hierarchy reference, not a reliable trunk-only geometry label. DLL code alone cannot confirm these asset details. Material names are logged as hints only. The copy probe exercises common CPU streams and indices, not every raw byte or variable bone influence.

Risks to evaluate:

- Static batches may not reflect a MeshFilter replacement; reject static-batched candidates until experimentally validated. Per-instance copies reduce geometry sharing and may reduce instancing/batching benefits.
- Keep original materials, material property blocks, shader keywords, alpha cutoff and lighting. Never access Renderer.material or MeshFilter.mesh during inspection.
- Every supported mesh LOD must use matching clipping logic. BillboardRenderer and impostor textures need separate support; do not silently hide them. Bounds and LOD crossfades must be checked at distance.
- Clipping changes shadows as well as visible geometry. Check shadow casting and two-sided foliage cards.
- Shader wind, trunk shake and growth can move vertices across a statically computed boundary. Clearance now defaults to 0.05 m and shifts only the trunk-facing entry plane. Existing config files retain their previous value until edited. Tune it against both visible wind penetration and gaps without enlarging the wall silhouette.
- Seasonal/biome material or mesh swaps are not established by this inspection. Validate them in the installed game; invalidate caches if source meshes change, and preserve material changes rather than holding stale copies.

## Second-phase implementation and remaining work

The second in-game log confirmed 843 live TreeBase instances and 24 tree prefabs in the scene. The scanner queued 12 live trees plus 12 prefabs; both groups were truncated. Sampled live pine, fir and YggaShoot meshes were readable, and detached Mesh API copies succeeded. YggaShoot has distinct trunk and leaf submeshes on two mesh LODs. Sampled pine and fir use single-material meshes, so their foliage could not be isolated from those logs alone. Local game assets were subsequently inspected: the sampled pine and fir meshes each have a vertically spanning connected trunk component and numerous disconnected foliage/branch components. This supports a guarded topology fallback without species names, but does not prove every tree layout is supported.

`ClipPrototype` uses TreeBase/Piece Harmony lifecycle notifications and a bounded dirty-tree queue. It gathers nearby box colliders on the `piece` or `piece_nonsolid` layer with a player-placed Piece and a matching wall, beam, or pole name. It selects foliage slots by material name, or for single-material vegetation meshes, selects triangles outside the connected trunk component when that component passes height/size checks. Neither path uses tree species or origin. The `ShadowClipper` clips triangles and interpolates Float32 position, normal, tangent, color and UV streams (only streams actually present), preserving submesh slots and shared materials. It skips unreadable meshes, unsupported stream formats, skinning, blend shapes, static batches, and large or complex jobs. Per-instance mesh references are restored and clones destroyed on rebuild, disable and unload. The earlier prototype worked in the game; the revised fir/beam path still awaits an in-game visual test.

The earlier model cast a three-dimensional shadow from the tree's ground-level origin. The latest in-game fir screenshot showed needles behind an upper wall surviving even though foliage beside the wall was clipped: fir branches extend outward from the trunk at their own height. The revised model projects each collider box to a convex horizontal footprint and casts a shadow from the trunk's horizontal position, then clips only between that box's actual bottom and top. This extends the cut behind an elevated wall while leaving foliage above and below it untouched. The configured clearance shifts only the entry edge toward the trunk. The geometry still needs visual validation, especially around plane boundaries and scaled or tilted pieces; the height span and footprint of an angled box are conservative axis-aligned/projection approximations.

The prototype subtracts each convex obstruction from each selected foliage triangle, splitting polygons at planes and retaining outside fragments. It repeats the subtraction for multiple wall faces, triangulates retained polygons, interpolates supported continuous streams, and chooses an index format supporting the expanded vertex count. Normal and tangent directions are normalized; tangent handedness remains discrete. Untouched submeshes keep their original triangle indices. Authored foliage normals are not recalculated.

Each affected tree instance owns its generated meshes. A companion component retains original references and restores only if the filter still references LeafClip's mesh. The present cache is the current generated mesh on an unchanged instance; changed tree/wall events rebuild it. A full obstruction-geometry cache key, transform change detection, elapsed-time update budget, and broad structure coverage remain future work.

TreeBase.Awake and Piece.Awake/SetCreator/OnDestroy are patched. Dirty-tree work is capped by tree count and geometry limits. The manager registers only trees with identifiable foliage, by material or topology, and queues those within configurable `MaxTreeDistance` of the local player (default 64 m). A five-second pass over registered trees detects range entry/exit; leaving restores generated meshes. This distance is a prototype work limit, not a confirmed match to Valheim's structure render distance. Piece notifications use actual box bounds and the configured tree search radius; unchanged obstruction geometry avoids mesh regeneration. Per-triangle quick rejection and cached world-space vertices reduce clipping work. The user log before these changes showed 285 Beech clipping lines plus 62 budget skips across sampled pine, fir and Beech, with up to 39 wall-face volumes per tree, which explains the stutter more directly than stumps or felled logs. Slow rebuilds of 8 ms or more are logged. A large base can still cause a spike; elapsed-time work budgeting remains future work. Transform changes, dynamic doors, remote creator ordering, growth timing, and settings changes other than enable/disable need more handling. No ZDO writes or custom RPCs. Debug volume drawing remains future work.

## Validation status

The first in-game log from 2026-09-24 confirms BepInEx loaded the diagnostic plugin and reports Unity 6000.0.75f1 / Valheim 1.0.15. It found 24 matching trees and inspected 12; all 12 logged roots had an empty scene name and inactive renderers, consistent with prefab samples while the player was still respawning. The original scan cap mixed live instances and prefabs. The scanner now waits for `Player.m_localPlayer`, restricts live discovery to loaded scenes, reports live/prefab counts separately, and budgets both groups independently. This fix has compiled but awaits an in-game rerun.

The sampled Beech1 prefab has three mesh LODs, two submeshes per LOD, and likely leaf/bark material slots. Birch prefabs have two mesh LODs, but the leaf and bark slot order changes between LODs. All sampled meshes were readable and detached Mesh API copy/read probes succeeded. The sampled vertex descriptors were Float32 position, normal, tangent and UV0; colors and additional UVs were absent. These are runtime observations for sampled prefabs, not a claim about every tree. Beech and autumn birch also have leaf particle renderers outside MeshFilter/SkinnedMeshRenderer handling. Ashlands and Fir samples include single-material meshes whose foliage/trunk split is not identified by the log. Thus a universal implementation must inspect material slots per renderer and handle mixed geometry/particle cases explicitly.

Debug compilation against the supplied references plus the matching local PhysicsModule passed. Live-tree diagnostic scanning and earlier pine/fir clipping were confirmed by user logs. An independent numerical check compared horizontal footprint shadow membership with segment/rotated-footprint intersection for 100,000 sampled cases. The topology classifier was checked against extracted pine and fir mesh components. Local prefab inspection confirmed that the sampled wood beam and core-wood pole have enabled box colliders on the `piece` layer. The revised branch-height shadow model has not yet been run in the game. See `tests/validation.md` for acceptance checks.
