# Runtime validation checklist

The earlier prototype has been exercised in Valheim, including pine/fir topology selection and fir clipping. The roof and wind allowance changes are **pending in-game validation**. A successful .NET build does not establish that the visuals work.

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

Enable `Prototype.Enabled` for a focused test with one player-built box wall and a pine or fir. Record a `Clipped ... topology=...` line, inspect the near and far LODs, then remove the wall and disable the prototype to verify restoration. Repeat with a beech or birch for the separate-material path. Particle leaves remain outside the prototype.

- With many loaded trees, confirm only trees within `MaxTreeDistance` are processed; walk across the boundary, wait five seconds, and verify meshes are generated on entry and restored on exit. Test a wall added near a previously unclipped tree, since piece notifications must queue it too.

- Automated geometry tests: full keep/remove, partial triangle split, edge/vertex on boundary, near-parallel rays, origin-in-footprint, rotated box, nonuniform transforms, multiple obstacle union, winding, nondegenerate output, and interpolated streams. Compare retained points with a horizontal trunk-to-point segment crossing the obstruction footprint **and** the wall's vertical span.
- Planted tree with adjacent wall: only obstructed portions disappear; trunk remains. Leaves behind a wall stay removed even where they extend outdoors again.
- Short wall below a high leaf: foliage above the wall must remain intact, even when the trunk-to-leaf segment passes through the wall. Foliage inside and farther behind the wall at its height should disappear. The clearance moves only the trunk-facing entry plane and must not raise the top/side silhouette.
- Pine and fir: check `topology` in the clipping log, inspect needles and detached branches at every LOD, and verify that the connected main trunk stays intact. Repeat on naturally generated and planted trees where available; report an unsupported layout as a skip rather than assuming every tree asset is identical.
- Fir beside a large base: first try a normal wood wall, then a core-wood beam or pole. Confirm a `Clipped FirTree... topology=1` entry and inspect the new `wall=`, `beam=`, and `pole=` counts. Check foliage both at the structure and farther behind it within the structure's height. If a skip remains, record its box, face, and triangle-face counts. Watch for placement stutter, especially with hundreds of nearby pieces.
- Naturally generated tree with adjacent wall: same result. Repeat across distinct tree layouts, including multiple biomes; no tree-name or origin configuration is required.
- Thin partial intersection splits triangles, without hiding a renderer, cluster, or whole submesh.
- Wood and darkwood sloped roof pieces: confirm the `roof=` collider count, clip through and behind the roof surface, and inspect along the slope for overcut. Compare a flat roof and a corner piece. Roofs with mesh colliders should remain unchanged for now. Test multiple walls plus roofs near the clipping work budget; if `roofDeferred=True`, wall/beam clipping should still remain active.
- Place/remove structure after load; remote placement/removal; tree growth/replacement; save/reload; leave/rejoin.
- All LOD distances and crossfades: consistent boundary, no uncut billboard popping, original material/shader/lighting/alpha maintained.
- Wind and trunk shake at the boundary: keep `Clearance = 0.05` and compare `WindGuard = 0`, `0.12`, and `0.25` m during calm and strong wind. Record penetration, visible gap, frame time, and whether changing the config queues one rebuild rather than recurring rebuilds. Above-wall foliage must remain visible. The static guard is an approximation of shader movement.
- Toggle `ClipWoodBehindWalls` beside a birch: compare branch intersections and inspect the main trunk at both LODs. Report any trunk segments removed unexpectedly.
- Destruction/unload, disable/re-enable and another mod replacing a mesh: restore only owned references; no mesh leaks.
- Many trees near a large base: measure frame time, rebuild queue latency, allocations, cache memory and instancing impact. No per-frame mesh rebuilding when nothing changes.
