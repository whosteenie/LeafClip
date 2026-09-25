# Runtime validation checklist

LeafClip clips in the GPU pixel shader after Valheim animates tree vertices. Test both DLLs from the same staged build on a BepInEx 5 client; the native backend is selected automatically for D3D11 or D3D12.

## Clipping

- Place a wall near naturally generated and planted fir, pine, birch, and beech trees. Check leaves, branches, and upper trunk during several wind cycles. Foliage above the wall should remain visible; foliage intersecting or beyond it at the wall's height should be hidden.
- Test two snapped, side-by-side walls. No foliage should appear between them. Inspect both ends of each wall from several angles for overcut or edge flicker.
- Test core-wood beams, ordinary walls, and roofs. Place and remove each while in the world, reload, and check the near and distant tree LODs.
- Check that a branch crossing a wall does not leave visibly detached foliage beyond its cut point, and that the branch becomes visible again when wind moves it clear.
- Compare color, depth, and shadows. Record any leaf-shaped shadows, reflection mismatches, particles, or unsupported vegetation renderers separately from visible foliage clipping.

## Performance and lifecycle

- Face a dense fir/core-wood building scene, then look away. Compare sustained FPS and frame-time spikes with and without LeafClip, after startup settles.
- With many loaded trees, confirm only trees within `GpuClip.MaxTreeDistance` and near player structures are processed. Walk across the range boundary and verify restoration on exit.
- Place and remove structures, grow or destroy trees, leave and rejoin the world, and change LOD distance. Check that private mesh/material references are restored on unload and no recurring rebuild occurs when geometry is unchanged.
- Test a multiplayer client without a server plugin; confirm no world-state mutation or network version requirement.

## Optional diagnostics

- Enable `[Diagnostics]` only when inspecting tree assets. Use `ScanRequest` to repeat a scan; `NameFilter`, `MaxTrees`, and `MaxProbeVertices` limit diagnostic output and do not select trees for clipping.
- Verify reported materials, LODs, and readable mesh streams against live prefabs. The copy probe never assigns its clone to a renderer.
