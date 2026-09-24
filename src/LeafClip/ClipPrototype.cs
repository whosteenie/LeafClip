using System;
using System.Collections.Generic;
using BepInEx.Configuration;
using HarmonyLib;
using UnityEngine;

namespace LeafClip
{
    // Experimental, opt-in runtime path. All changes are local MeshFilter references.
    internal sealed class ClipPrototype
    {
        private readonly Plugin plugin;
        private readonly ConfigEntry<bool> enabled;
        private readonly ConfigEntry<float> radius, clearance, treeDistance;
        private readonly ConfigEntry<string> wallName;
        private readonly ConfigEntry<int> budget;
        private readonly Queue<TreeBase> dirty = new Queue<TreeBase>();
        private readonly HashSet<int> queued = new HashSet<int>();
        private readonly HashSet<TreeClipState> states = new HashSet<TreeClipState>();
        private readonly HashSet<TreeBase> knownTrees = new HashSet<TreeBase>();
        private readonly HashSet<int> nearTrees = new HashSet<int>();
        private readonly Harmony harmony;
        private bool wasEnabled;
        private float nextHeartbeat, nextDistanceCheck;
        internal static ClipPrototype Current;

        internal ClipPrototype(Plugin plugin)
        {
            this.plugin = plugin;
            enabled = plugin.Config.Bind("Prototype", "Enabled", false, "Experimental local foliage clipping for readable meshes with separate leaf material slots. Restart after enabling if trees do not update.");
            radius = plugin.Config.Bind("Prototype", "SearchRadius", 18f, new ConfigDescription("Search radius around a tree root in metres.", new AcceptableValueRange<float>(2f, 80f)));
            treeDistance = plugin.Config.Bind("Prototype", "MaxTreeDistance", 100f, new ConfigDescription("Only process trees this close to the local player. Rechecked every five seconds.", new AcceptableValueRange<float>(20f, 300f)));
            clearance = plugin.Config.Bind("Prototype", "Clearance", 0.5f, new ConfigDescription("Expand the obstruction by this many metres before clipping.", new AcceptableValueRange<float>(0f, 2f)));
            wallName = plugin.Config.Bind("Prototype", "WallNameContains", "wall", "Only player-built Piece names containing this text count as obstructions in the first prototype.");
            budget = plugin.Config.Bind("Prototype", "TreesPerFrame", 1, new ConfigDescription("Maximum dirty trees processed per frame. Diagnostic prototype; no hard millisecond budget.", new AcceptableValueRange<int>(1, 4)));
            Current = this;
            harmony = new Harmony("local.leafclip.prototype");
            harmony.PatchAll(typeof(ClipPrototype).Assembly);
            wasEnabled = enabled.Value;
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
                try { Rebuild(tree); }
                catch (Exception ex) { plugin.LogClip("Prototype rebuild failed for " + tree.name + ": " + ex); }
            }
        }

        internal void Dispose()
        {
            harmony.UnpatchSelf();
            foreach (TreeClipState state in states) if (state) state.Restore();
            states.Clear(); knownTrees.Clear(); nearTrees.Clear(); dirty.Clear(); queued.Clear(); Current = null;
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
            if (!enabled.Value || !piece || !piece.IsPlacedByPlayer()) return;
            if (piece.name.IndexOf(wallName.Value, StringComparison.OrdinalIgnoreCase) < 0) return;
            float maxDistance = radius.Value * 2f;
            foreach (TreeBase tree in knownTrees)
                if (tree && nearTrees.Contains(tree.GetInstanceID()) && (tree.transform.position - piece.transform.position).sqrMagnitude <= maxDistance * maxDistance)
                    Enqueue(tree);
        }

        private void Rebuild(TreeBase tree)
        {
            if (!WithinPlayerDistance(tree)) return;
            TreeClipState state = tree.GetComponent<TreeClipState>();
            if (!state) { state = tree.gameObject.AddComponent<TreeClipState>(); state.Tree = tree; states.Add(state); }
            state.Restore();
            if (!tree.gameObject.activeInHierarchy) return;
            int pieceLayer = LayerMask.NameToLayer("piece");
            if (pieceLayer < 0) return;
            Collider[] hits = Physics.OverlapSphere(tree.transform.position, radius.Value, 1 << pieceLayer, QueryTriggerInteraction.Ignore);
            var volumes = new List<ShadowVolume>();
            foreach (Collider hit in hits)
            {
                BoxCollider box = hit as BoxCollider;
                if (!box || !box.enabled) continue;
                Piece piece = box.GetComponentInParent<Piece>();
                if (!piece || !piece.IsPlacedByPlayer() || piece.name.IndexOf(wallName.Value, StringComparison.OrdinalIgnoreCase) < 0) continue;
                volumes.AddRange(ShadowVolume.FromBox(box, tree.transform.position, clearance.Value));
            }
            if (volumes.Count == 0) return;
            if (volumes.Count > 40) { plugin.LogClip("Skipped " + tree.name + ": too many wall faces for prototype budget."); return; }
            foreach (MeshRenderer renderer in tree.GetComponentsInChildren<MeshRenderer>(true))
            {
                if (IsTemporaryChild(tree.transform, renderer.transform)) continue;
                MeshFilter filter = renderer.GetComponent<MeshFilter>();
                if (!filter || !filter.sharedMesh || renderer.isPartOfStaticBatch) continue;
                Mesh source = filter.sharedMesh;
                if (source.vertexCount > 20000) { plugin.LogClip("Skipped " + tree.name + "/" + renderer.name + ": source vertex budget exceeded."); continue; }
                if (!ShadowClipper.Supports(source, out string reason)) { plugin.LogClip("Skipped " + tree.name + "/" + renderer.name + ": " + reason); continue; }
                Material[] materials = renderer.sharedMaterials;
                var foliage = new List<int>();
                for (int i = 0; i < source.subMeshCount && i < materials.Length; i++)
                    if (materials[i] && IsLeafMaterial(materials[i])) foliage.Add(i);
                if (foliage.Count == 0) continue;
                Mesh generated = ShadowClipper.Clip(source, renderer.transform, foliage, volumes);
                if (!generated) continue;
                filter.sharedMesh = generated;
                state.Add(filter, source, generated);
                plugin.LogClip($"Clipped {tree.name}/{renderer.name}: {foliage.Count} foliage submesh(es), {volumes.Count} wall face volume(s)");
            }
        }

        private static bool IsLeafMaterial(Material material)
        {
            string name = material.name.ToLowerInvariant();
            return name.Contains("leaf") || name.Contains("leaves") || name.Contains("foliage");
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
        internal void Add(MeshFilter filter, Mesh original, Mesh generated) { entries.Add(new Entry { Filter = filter, Original = original, Generated = generated }); }
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
