using System;
using System.Collections.Generic;
using System.Diagnostics;
using BepInEx.Configuration;
using HarmonyLib;
using UnityEngine;

namespace LeafClip
{
    // Experimental, opt-in runtime path. All changes are local MeshFilter references.
    internal sealed class ClipPrototype
    {
        private readonly Plugin plugin;
        private readonly ConfigEntry<bool> enabled, clipWood, includeBeams, includeRoofs;
        private readonly ConfigEntry<float> radius, clearance, windGuard, treeDistance;
        private readonly ConfigEntry<string> wallName;
        private readonly ConfigEntry<int> budget;
        private readonly Queue<TreeBase> dirty = new Queue<TreeBase>();
        private readonly HashSet<int> queued = new HashSet<int>();
        private readonly HashSet<TreeClipState> states = new HashSet<TreeClipState>();
        private readonly HashSet<TreeBase> knownTrees = new HashSet<TreeBase>();
        private readonly HashSet<int> nearTrees = new HashSet<int>();
        private readonly Dictionary<Mesh, bool[]> topologyMasks = new Dictionary<Mesh, bool[]>();
        private readonly Harmony harmony;
        private bool wasEnabled, wasClippingWood, wasIncludingBeams, wasIncludingRoofs;
        private float wasClearance, wasWindGuard;
        private float nextHeartbeat, nextDistanceCheck;
        internal static ClipPrototype Current;

        internal ClipPrototype(Plugin plugin)
        {
            this.plugin = plugin;
            enabled = plugin.Config.Bind("Prototype", "Enabled", false, "Experimental local foliage clipping for readable meshes with leaf material slots or a recognizable detached canopy. Restart after enabling if trees do not update.");
            clipWood = plugin.Config.Bind("Prototype", "ClipWoodBehindWalls", false, "Experimental: also clip bark/trunk material triangles behind walls. This can cut trunk geometry as well as branches.");
            includeBeams = plugin.Config.Bind("Prototype", "IncludeBeamsAndPoles", true, "Also use player-built beam and pole box colliders as foliage obstructions, including core wood pieces.");
            includeRoofs = plugin.Config.Bind("Prototype", "IncludeRoofs", true, "Also clip against player-built roof box colliders. Angled roofs use up to four height slices.");
            radius = plugin.Config.Bind("Prototype", "SearchRadius", 18f, new ConfigDescription("Search radius around a tree root in metres.", new AcceptableValueRange<float>(2f, 80f)));
            treeDistance = plugin.Config.Bind("Prototype", "MaxTreeDistance", 64f, new ConfigDescription("Only process trees this close to the local player. Rechecked every five seconds.", new AcceptableValueRange<float>(20f, 300f)));
            clearance = plugin.Config.Bind("Prototype", "Clearance", 0.05f, new ConfigDescription("Clip this far before the trunk-facing wall surface without expanding its top or sides. Use a small value to avoid a visible gap.", new AcceptableValueRange<float>(0f, 2f)));
            windGuard = plugin.Config.Bind("Prototype", "WindGuard", 0.12f, new ConfigDescription("Static horizontal allowance in metres for shader wind near obstructions; zero adds no allowance and incurs no per-frame mesh work.", new AcceptableValueRange<float>(0f, 1f)));
            wallName = plugin.Config.Bind("Prototype", "WallNameContains", "wall", "Only player-built Piece names containing this text count as obstructions in the first prototype.");
            budget = plugin.Config.Bind("Prototype", "TreesPerFrame", 1, new ConfigDescription("Maximum dirty trees processed per frame. Diagnostic prototype; no hard millisecond budget.", new AcceptableValueRange<int>(1, 4)));
            Current = this;
            harmony = new Harmony("local.leafclip.prototype");
            harmony.PatchAll(typeof(ClipPrototype).Assembly);
            wasEnabled = enabled.Value;
            wasClippingWood = clipWood.Value;
            wasIncludingBeams = includeBeams.Value;
            wasIncludingRoofs = includeRoofs.Value;
            wasClearance = clearance.Value;
            wasWindGuard = windGuard.Value;
            if (wasEnabled) EnqueueAll();
        }

        internal void Update()
        {
            if (enabled.Value != wasEnabled)
            {
                wasEnabled = enabled.Value;
                if (wasEnabled) EnqueueAll();
                else
                {
                    foreach (TreeClipState state in states) if (state) state.Restore();
                    dirty.Clear(); queued.Clear(); nearTrees.Clear();
                }
            }
            if (!enabled.Value) return;
            if (clipWood.Value != wasClippingWood)
            {
                wasClippingWood = clipWood.Value;
                foreach (TreeBase tree in knownTrees) Enqueue(tree);
            }
            if (includeBeams.Value != wasIncludingBeams || includeRoofs.Value != wasIncludingRoofs ||
                clearance.Value != wasClearance || windGuard.Value != wasWindGuard)
            {
                wasIncludingBeams = includeBeams.Value;
                wasIncludingRoofs = includeRoofs.Value;
                wasClearance = clearance.Value;
                wasWindGuard = windGuard.Value;
                foreach (TreeBase tree in knownTrees) Enqueue(tree);
            }
            if (Time.realtimeSinceStartup >= nextDistanceCheck)
            {
                nextDistanceCheck = Time.realtimeSinceStartup + 5f;
                RefreshNearTrees();
            }
            // Reconcile trees/structures already present when the plugin became active,
            // remote creator updates, and world unload. No mesh is rebuilt unless dirty.
            if (Time.realtimeSinceStartup >= nextHeartbeat)
            {
                nextHeartbeat = Time.realtimeSinceStartup + 30f;
                states.RemoveWhere(state => !state);
                foreach (TreeClipState state in states)
                    if (state && state.Tree && state.HasChangedSource()) Enqueue(state.Tree);
            }
            for (int i = 0; i < budget.Value && dirty.Count > 0; i++)
            {
                TreeBase tree = dirty.Dequeue();
                if (tree) queued.Remove(tree.GetInstanceID());
                if (!tree) continue;
                long start = Stopwatch.GetTimestamp();
                try { Rebuild(tree); }
                catch (Exception ex) { plugin.LogClip("Prototype rebuild failed for " + tree.name + ": " + ex); }
                finally
                {
                    double milliseconds = (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;
                    if (milliseconds >= 8.0) plugin.LogClip($"Slow rebuild {tree.name} id={tree.GetInstanceID()} took {milliseconds:F1} ms");
                }
            }
        }

        internal void Dispose()
        {
            harmony.UnpatchSelf();
            foreach (TreeClipState state in states) if (state) state.Restore();
            states.Clear(); knownTrees.Clear(); nearTrees.Clear(); topologyMasks.Clear(); dirty.Clear(); queued.Clear(); Current = null;
        }

        private void EnqueueAll()
        {
            foreach (TreeBase tree in UnityEngine.Object.FindObjectsByType<TreeBase>(FindObjectsInactive.Exclude, FindObjectsSortMode.None))
                RegisterTree(tree);
            RefreshNearTrees();
        }

        private void RegisterTree(TreeBase tree)
        {
            if (!tree || !tree.gameObject.scene.IsValid() || !tree.gameObject.scene.isLoaded) return;
            // Avoid registering logs, stumps, and unsupported tree layouts that
            // this guarded prototype cannot modify.
            if (!HasFoliageSlot(tree)) return;
            knownTrees.Add(tree);
            if (enabled.Value && WithinPlayerDistance(tree) && nearTrees.Add(tree.GetInstanceID())) Enqueue(tree);
        }

        private bool WithinPlayerDistance(TreeBase tree)
        {
            Player player = Player.m_localPlayer;
            return player && tree && (tree.transform.position - player.transform.position).sqrMagnitude <= treeDistance.Value * treeDistance.Value;
        }

        private void RefreshNearTrees()
        {
            knownTrees.RemoveWhere(tree => !tree);
            var observed = new HashSet<int>();
            foreach (TreeBase tree in knownTrees)
            {
                int id = tree.GetInstanceID();
                if (!WithinPlayerDistance(tree))
                {
                    if (nearTrees.Remove(id))
                    {
                        TreeClipState state = tree.GetComponent<TreeClipState>();
                        if (state) state.Restore();
                    }
                    continue;
                }
                observed.Add(id);
                if (nearTrees.Add(id)) Enqueue(tree);
            }
            nearTrees.RemoveWhere(id => !observed.Contains(id));
        }

        private void Enqueue(TreeBase tree)
        {
            if (!enabled.Value || !WithinPlayerDistance(tree)) return;
            if (queued.Add(tree.GetInstanceID())) dirty.Enqueue(tree);
        }

        private void NotifyPiece(Piece piece)
        {
            if (!enabled.Value || !IsObstructionPiece(piece)) return;
            int pieceLayer = LayerMask.NameToLayer("piece");
            int nonsolidLayer = LayerMask.NameToLayer("piece_nonsolid");
            if (pieceLayer < 0) return;
            var boxes = new List<BoxCollider>();
            foreach (BoxCollider box in piece.GetComponentsInChildren<BoxCollider>())
                if (box && box.enabled && !box.isTrigger && (box.gameObject.layer == pieceLayer || box.gameObject.layer == nonsolidLayer)) boxes.Add(box);
            if (boxes.Count == 0) return;
            float radiusSquared = radius.Value * radius.Value;
            foreach (TreeBase tree in knownTrees)
            {
                if (!tree || !nearTrees.Contains(tree.GetInstanceID())) continue;
                foreach (BoxCollider box in boxes)
                    if (HorizontalSqrDistance(box.bounds, tree.transform.position) <= radiusSquared)
                    {
                        Enqueue(tree);
                        break;
                    }
            }
        }

        private void Rebuild(TreeBase tree)
        {
            if (!WithinPlayerDistance(tree)) return;
            TreeClipState state = tree.GetComponent<TreeClipState>();
            if (!tree.gameObject.activeInHierarchy) { if (state) state.Restore(); return; }
            if (!TryGetClipBounds(tree, state, out Bounds clipBounds)) return;
            int pieceLayer = LayerMask.NameToLayer("piece");
            int nonsolidLayer = LayerMask.NameToLayer("piece_nonsolid");
            if (pieceLayer < 0) return;
            int layerMask = 1 << pieceLayer;
            if (nonsolidLayer >= 0) layerMask |= 1 << nonsolidLayer;
            Collider[] hits = Physics.OverlapBox(clipBounds.center, clipBounds.extents, Quaternion.identity, layerMask, QueryTriggerInteraction.Ignore);
            var boxes = new List<BoxCollider>();
            foreach (Collider hit in hits)
            {
                BoxCollider box = hit as BoxCollider;
                if (!box || !box.enabled || !box.bounds.Intersects(clipBounds) ||
                    HorizontalSqrDistance(box.bounds, tree.transform.position) > radius.Value * radius.Value) continue;
                Piece piece = box.GetComponentInParent<Piece>();
                if (IsObstructionPiece(piece)) boxes.Add(box);
            }
            int wallBoxes = 0, beamBoxes = 0, poleBoxes = 0, roofBoxes = 0;
            foreach (BoxCollider box in boxes)
            {
                string name = box.GetComponentInParent<Piece>().name;
                if (IsRoofName(name)) roofBoxes++;
                else if (name.IndexOf("beam", StringComparison.OrdinalIgnoreCase) >= 0) beamBoxes++;
                else if (name.IndexOf("pole", StringComparison.OrdinalIgnoreCase) >= 0) poleBoxes++;
                else wallBoxes++;
            }
            int signature = GeometrySignature(tree, boxes);
            if (state && state.HasGenerated && state.Signature == signature && !state.HasChangedSource()) return;
            if (state) state.Restore();
            var volumes = new List<ShadowVolume>();
            var roofVolumes = new List<ShadowVolume>();
            foreach (BoxCollider box in boxes)
            {
                bool roof = IsRoofName(box.GetComponentInParent<Piece>().name);
                foreach (ShadowVolume volume in ShadowVolume.FromBox(box, tree.transform.position, clearance.Value, windGuard.Value, roof))
                    if (volume.Intersects(clipBounds)) (roof ? roofVolumes : volumes).Add(volume);
            }
            if (volumes.Count == 0 && roofVolumes.Count == 0) return;
            long estimatedWork = 0, selectedTriangles = 0;
            foreach (MeshRenderer renderer in tree.GetComponentsInChildren<MeshRenderer>(true))
            {
                if (IsTemporaryChild(tree.transform, renderer.transform)) continue;
                MeshFilter filter = renderer.GetComponent<MeshFilter>();
                if (!filter || !filter.sharedMesh || renderer.isPartOfStaticBatch) continue;
                Mesh source = filter.sharedMesh;
                if (source.vertexCount > 20000) continue;
                foreach (FoliageSelection selection in SelectFoliage(renderer, source))
                    selectedTriangles += selection.Triangles == null ? (long)source.GetIndexCount(selection.Slot) / 3L : CountSelected(selection.Triangles);
            }
            // Small single-material canopies can afford many nearby pieces once
            // individual triangle/volume misses are rejected before splitting.
            long workLimit = selectedTriangles <= 700 ? 900000 : 120000;
            int volumeLimit = selectedTriangles <= 700 ? 1800 : 160;
            bool roofsDeferred = roofVolumes.Count > 0 && volumes.Count > 0 &&
                                 (volumes.Count + roofVolumes.Count > volumeLimit ||
                                  selectedTriangles * (volumes.Count + roofVolumes.Count) > workLimit);
            if (!roofsDeferred) volumes.AddRange(roofVolumes);
            estimatedWork = selectedTriangles * volumes.Count;
            if (volumes.Count > volumeLimit || estimatedWork > workLimit)
            {
                plugin.LogClip($"Skipped {tree.name}: clipping work budget ({boxes.Count} boxes [wall={wallBoxes}, beam={beamBoxes}, pole={poleBoxes}, roof={roofBoxes}], {volumes.Count} faces, {estimatedWork}/{workLimit} triangle-face checks).");
                return;
            }
            if (roofsDeferred) plugin.LogClip($"Deferred {roofVolumes.Count} roof face volume(s) for {tree.name}: wall/beam clipping retained within work budget.");
            int generatedCount = 0, topologyCount = 0;
            foreach (MeshRenderer renderer in tree.GetComponentsInChildren<MeshRenderer>(true))
            {
                if (IsTemporaryChild(tree.transform, renderer.transform)) continue;
                MeshFilter filter = renderer.GetComponent<MeshFilter>();
                if (!filter || !filter.sharedMesh || renderer.isPartOfStaticBatch) continue;
                Mesh source = filter.sharedMesh;
                if (source.vertexCount > 20000) { plugin.LogClip("Skipped " + tree.name + "/" + renderer.name + ": source vertex budget exceeded."); continue; }
                if (!ShadowClipper.Supports(source, out string reason)) { plugin.LogClip("Skipped " + tree.name + "/" + renderer.name + ": " + reason); continue; }
                List<FoliageSelection> foliage = SelectFoliage(renderer, source);
                if (foliage.Count == 0) continue;
                Mesh generated = ShadowClipper.Clip(source, renderer.transform, foliage, volumes);
                if (!generated) continue;
                if (!state) { state = tree.gameObject.AddComponent<TreeClipState>(); state.Tree = tree; states.Add(state); }
                filter.sharedMesh = generated;
                state.Add(filter, source, generated);
                generatedCount++;
                if (foliage[0].Triangles != null) topologyCount++;
            }
            if (generatedCount > 0)
            {
                state.Signature = signature;
                plugin.LogClip($"Clipped {tree.name}: {generatedCount} mesh LOD(s), topology={topologyCount}, {boxes.Count} obstruction box(es) [wall={wallBoxes}, beam={beamBoxes}, pole={poleBoxes}, roof={roofBoxes}], {volumes.Count} face volume(s), roofDeferred={roofsDeferred}, windGuard={windGuard.Value:F2}m, wood={clipWood.Value}");
            }
        }

        private int GeometrySignature(TreeBase tree, List<BoxCollider> boxes)
        {
            boxes.Sort((a, b) => a.GetInstanceID().CompareTo(b.GetInstanceID()));
            unchecked
            {
                int hash = tree.transform.position.GetHashCode();
                hash = hash * 31 + tree.transform.rotation.GetHashCode();
                hash = hash * 31 + tree.transform.lossyScale.GetHashCode();
                hash = hash * 31 + clearance.Value.GetHashCode();
                hash = hash * 31 + windGuard.Value.GetHashCode();
                hash = hash * 31 + clipWood.Value.GetHashCode();
                foreach (BoxCollider box in boxes)
                {
                    hash = hash * 31 + box.GetInstanceID();
                    hash = hash * 31 + box.transform.position.GetHashCode();
                    hash = hash * 31 + box.transform.rotation.GetHashCode();
                    hash = hash * 31 + box.transform.lossyScale.GetHashCode();
                    hash = hash * 31 + box.center.GetHashCode();
                    hash = hash * 31 + box.size.GetHashCode();
                }
                return hash;
            }
        }

        private bool IsObstructionPiece(Piece piece)
        {
            if (!piece || !piece.IsPlacedByPlayer()) return false;
            string name = piece.name;
            if (IsRoofName(name)) return includeRoofs.Value;
            return name.IndexOf(wallName.Value, StringComparison.OrdinalIgnoreCase) >= 0 ||
                   includeBeams.Value && (name.IndexOf("beam", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                          name.IndexOf("pole", StringComparison.OrdinalIgnoreCase) >= 0);
        }

        private static bool IsRoofName(string name) { return name.IndexOf("roof", StringComparison.OrdinalIgnoreCase) >= 0; }

        private static float HorizontalSqrDistance(Bounds bounds, Vector3 point)
        {
            float dx = Mathf.Max(0f, Mathf.Max(bounds.min.x - point.x, point.x - bounds.max.x));
            float dz = Mathf.Max(0f, Mathf.Max(bounds.min.z - point.z, point.z - bounds.max.z));
            return dx * dx + dz * dz;
        }

        private bool TryGetClipBounds(TreeBase tree, TreeClipState state, out Bounds bounds)
        {
            bounds = new Bounds(tree.transform.position, Vector3.zero);
            bool found = false;
            foreach (MeshRenderer renderer in tree.GetComponentsInChildren<MeshRenderer>(true))
            {
                if (IsTemporaryChild(tree.transform, renderer.transform)) continue;
                MeshFilter filter = renderer.GetComponent<MeshFilter>();
                if (!filter || renderer.isPartOfStaticBatch) continue;
                Mesh source = state ? state.SourceFor(filter) : filter.sharedMesh;
                if (!source || SelectFoliage(renderer, source).Count == 0) continue;
                Bounds local = source.bounds;
                for (int i = 0; i < 8; i++)
                    bounds.Encapsulate(renderer.transform.TransformPoint(local.center + new Vector3(
                        (i & 1) == 0 ? -local.extents.x : local.extents.x,
                        (i & 2) == 0 ? -local.extents.y : local.extents.y,
                        (i & 4) == 0 ? -local.extents.z : local.extents.z)));
                found = true;
            }
            if (found) bounds.Expand(Mathf.Max(0.5f, 2f * (clearance.Value + windGuard.Value)));
            return found;
        }

        private static int CountSelected(bool[] selected)
        {
            int count = 0;
            foreach (bool value in selected) if (value) count++;
            return count;
        }

        private static bool IsLeafMaterial(Material material)
        {
            string name = material.name.ToLowerInvariant();
            return name.Contains("leaf") || name.Contains("leaves") || name.Contains("foliage");
        }

        private static bool IsWoodMaterial(Material material)
        {
            string name = material.name.ToLowerInvariant();
            return name.Contains("bark") || name.Contains("trunk");
        }

        private List<FoliageSelection> SelectFoliage(MeshRenderer renderer, Mesh mesh)
        {
            Material[] materials = renderer.sharedMaterials;
            var selected = new List<FoliageSelection>();
            for (int i = 0; i < mesh.subMeshCount && i < materials.Length; i++)
                if (materials[i] && (IsLeafMaterial(materials[i]) || clipWood.Value && IsWoodMaterial(materials[i])))
                    selected.Add(new FoliageSelection(i, null));
            if (selected.Count != 0 || mesh.subMeshCount != 1 || materials.Length != 1 || !materials[0]) return selected;
            Material material = materials[0];
            if (!material.shader || material.shader.name.IndexOf("Vegetation", StringComparison.OrdinalIgnoreCase) < 0 || !material.HasProperty("_Cutoff") || material.GetFloat("_Cutoff") <= 0f)
                return selected;
            if (!topologyMasks.TryGetValue(mesh, out bool[] mask))
            {
                TopologyFoliage.TryClassify(mesh, out mask);
                topologyMasks[mesh] = mask;
            }
            if (mask != null) selected.Add(new FoliageSelection(0, mask));
            return selected;
        }

        private bool HasFoliageSlot(TreeBase tree)
        {
            foreach (MeshRenderer renderer in tree.GetComponentsInChildren<MeshRenderer>(true))
            {
                if (IsTemporaryChild(tree.transform, renderer.transform)) continue;
                MeshFilter filter = renderer.GetComponent<MeshFilter>();
                if (!filter || !filter.sharedMesh || renderer.isPartOfStaticBatch) continue;
                if (SelectFoliage(renderer, filter.sharedMesh).Count != 0) return true;
            }
            return false;
        }

        private static bool IsTemporaryChild(Transform root, Transform child)
        {
            for (Transform current = child; current && current != root; current = current.parent)
                if (current.name.EndsWith("(Clone)", StringComparison.Ordinal)) return true;
            return false;
        }

        [HarmonyPatch(typeof(TreeBase), "Awake")]
        private static class TreeAwakePatch
        {
            private static void Postfix(TreeBase __instance) { Current?.RegisterTree(__instance); }
        }

        [HarmonyPatch(typeof(Piece), "Awake")]
        private static class PieceAwakePatch
        {
            private static void Postfix(Piece __instance) { Current?.NotifyPiece(__instance); }
        }

        [HarmonyPatch(typeof(Piece), "SetCreator")]
        private static class PieceCreatorPatch
        {
            private static void Postfix(Piece __instance) { Current?.NotifyPiece(__instance); }
        }

        [HarmonyPatch(typeof(Piece), "OnDestroy")]
        private static class PieceDestroyPatch
        {
            private static void Prefix(Piece __instance) { Current?.NotifyPiece(__instance); }
        }
    }

    internal sealed class TreeClipState : MonoBehaviour
    {
        private struct Entry { internal MeshFilter Filter; internal Mesh Original, Generated; }
        private readonly List<Entry> entries = new List<Entry>();
        internal TreeBase Tree;
        internal int Signature;
        internal bool HasGenerated => entries.Count > 0;
        internal void Add(MeshFilter filter, Mesh original, Mesh generated) { entries.Add(new Entry { Filter = filter, Original = original, Generated = generated }); }
        internal Mesh SourceFor(MeshFilter filter)
        {
            foreach (Entry entry in entries)
                if (entry.Filter == filter && filter.sharedMesh == entry.Generated) return entry.Original;
            return filter.sharedMesh;
        }
        internal bool HasChangedSource()
        {
            foreach (Entry entry in entries) if (!entry.Filter || entry.Filter.sharedMesh != entry.Generated) return true;
            return false;
        }
        internal void Restore()
        {
            foreach (Entry entry in entries)
            {
                if (entry.Filter && entry.Filter.sharedMesh == entry.Generated) entry.Filter.sharedMesh = entry.Original;
                if (entry.Generated) UnityEngine.Object.Destroy(entry.Generated);
            }
            entries.Clear();
        }
        private void OnDestroy() { Restore(); }
    }
}
