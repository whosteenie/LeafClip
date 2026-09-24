# Runtime validation checklist

All runtime cases below are **pending**. Build-only validation passed; these require the actual game. Do not use a successful .NET build as evidence that a Unity-native mesh call works.

## Diagnostic phase

- Install only LeafClip.dll in a BepInEx 5 client; confirm one startup line and no recurring tree reports with Enabled=false.
- Enable diagnostics, enter world, verify one delayed scan; change ScanRequest to repeat, with no automatic continuous scans.
- Capture naturally generated tree instances and prefab reports with NameFilter empty (increase MaxTrees or repeat scans in other areas if truncated). Verify paths with duplicate sibling names, root position, all material slots and all LOD memberships. Compare a planted tree too if available; origin must not affect eligibility.
- Verify descriptor reports against accessible mesh data. Distinguish name heuristics from visually confirmed foliage. Record tree and wall prefab names, materials, shader, stream layouts, and readability.
- Enable copy probes: confirm successful reads for readable meshes and explicit skips for unreadable or over-budget meshes. Confirm no render changes, modified shared mesh references, persistent temporary meshes, or material instances.
- Leave world mid-scan, rejoin, disable diagnostics mid-scan, and destroy a queued tree: no exceptions or retained generated meshes.
- Test on multiplayer client without server plugin; confirm no network version requirement or world state mutation.
- Record scan time and peak allocations at MaxTrees and MaxProbeVertices limits. One-tree-per-frame processing is not a hard millisecond budget.

## Clipping phase acceptance (future)

The first guarded clipping prototype is now implemented but remains untested in Valheim. Enable `Prototype.Enabled` only for a focused test with a separate leaf material and one player-built box wall. Record a `Clipped ...` line, inspect the near and far LODs, then remove the wall and disable the prototype to verify restoration. Mixed-material pine/fir and particle leaves are known gaps, not passing cases.

- With many loaded trees, confirm only trees within `MaxTreeDistance` are processed; walk across the boundary, wait five seconds, and verify meshes are generated on entry and restored on exit. Test a wall added near a previously unclipped tree, since piece notifications must queue it too.

- Automated geometry tests: full keep/remove, partial triangle split, edge/vertex on boundary, near-parallel rays, origin-in-box, rotated box, nonuniform transforms, multiple obstacle union, winding, nondegenerate output, and interpolated streams. Compare retained points with trunk-to-point segment/box intersection.
- Planted tree with adjacent wall: only obstructed portions disappear; trunk remains. Leaves behind a wall stay removed even where they extend outdoors again.
- Naturally generated tree with adjacent wall: same result. Repeat across distinct tree layouts, including multiple biomes; no tree-name or origin configuration is required.
- Thin partial intersection splits triangles, without hiding a renderer, cluster, or whole submesh.
- Roof/angled pieces and multiple walls: either explicitly supported and correct or rejected without corrupting the mesh.
- Place/remove structure after load; remote placement/removal; tree growth/replacement; save/reload; leave/rejoin.
- All LOD distances and crossfades: consistent boundary, no uncut billboard popping, original material/shader/lighting/alpha maintained.
- Wind and trunk shake at the boundary: evaluate clearance at 0.25, 0.5 and 0.75 m; document remaining penetration.
- Destruction/unload, disable/re-enable and another mod replacing a mesh: restore only owned references; no mesh leaks.
- Many trees near a large base: measure frame time, rebuild queue latency, allocations, cache memory and instancing impact. No per-frame mesh rebuilding when nothing changes.
