using System;
using System.Collections.Generic;
using System.IO;
using BepInEx;
using BepInEx.Configuration;
using HarmonyLib;
using UnityEngine;

namespace LeafClip
{
    [BepInPlugin("local.leafclip", "LeafClip", "0.3.0")]
    public sealed class Plugin : BaseUnityPlugin
    {
        private ConfigEntry<bool> diagnostics, prefabs, probe;
        private ConfigEntry<string> filter;
        private ConfigEntry<int> request, limit, vertexLimit;
        private readonly Queue<TreeBase> pending = new Queue<TreeBase>();
        private readonly HashSet<int> probed = new HashSet<int>();
        private DateTime configStamp;
        private int handledRequest;
        private float nextPoll, scanAt;
        private ZNetScene scene;
        private bool scheduled;
        private MeshInspector inspector;
        private GpuClip gpu;
        private Harmony harmony;

        private void Awake()
        {
            diagnostics = Config.Bind("Diagnostics", "Enabled", false, "Opt in to one scan after world entry and each ScanRequest change. No visual changes.");
            prefabs = Config.Bind("Diagnostics", "IncludePrefabs", false, "Also inspect registered tree prefabs and Plant grown-prefab references, without instantiating them.");
            probe = Config.Bind("Diagnostics", "ProbeMeshCopy", false, "Temporarily clone readable meshes and read their streams. Potentially expensive; never assigns the clone to a renderer.");
            filter = Config.Bind("Diagnostics", "NameFilter", "", "Case-insensitive tree name substring; empty includes all species.");
            request = Config.Bind("Diagnostics", "ScanRequest", 0, "Change this integer and save while in a world to request another scan. Config changes are read every 5 seconds.");
            limit = Config.Bind("Diagnostics", "MaxTrees", 12, new ConfigDescription("Maximum loaded trees and maximum prefabs per scan, counted separately; a scan may be incomplete.", new AcceptableValueRange<int>(1, 500)));
            vertexLimit = Config.Bind("Diagnostics", "MaxProbeVertices", 100000, new ConfigDescription("Skip mesh-copy probes above this vertex count.", new AcceptableValueRange<int>(1, 1000000)));
            handledRequest = request.Value;
            inspector = new MeshInspector(s => Logger.LogInfo(s));
            gpu = new GpuClip(this);
            // Persist every bound entry and the current plugin-version header
            // on first launch, including the settings bound by GpuClip.
            Config.Save();
            configStamp = File.GetLastWriteTimeUtc(Config.ConfigFilePath);
            harmony = new Harmony("local.leafclip.runtime");
            harmony.PatchAll(typeof(Plugin).Assembly);
            Logger.LogInfo("LeafClip loaded. GPU clipping initializes automatically on supported renderers.");
        }

        private void Update()
        {
            gpu.Update();
            if (Time.realtimeSinceStartup >= nextPoll)
            {
                nextPoll = Time.realtimeSinceStartup + 5f;
                try
                {
                    DateTime stamp = File.GetLastWriteTimeUtc(Config.ConfigFilePath);
                    if (stamp != configStamp) { Config.Reload(); configStamp = stamp; }
                }
                catch (Exception ex) { Logger.LogWarning("Config reload failed: " + ex.Message); }

                if (!diagnostics.Value) { pending.Clear(); probed.Clear(); scheduled = false; scene = null; return; }
                if (ZNetScene.instance != scene)
                {
                    pending.Clear(); probed.Clear();
                    scene = ZNetScene.instance;
                    scheduled = scene;
                    scanAt = Time.realtimeSinceStartup + 10f;
                }
                if (scene && request.Value != handledRequest)
                {
                    handledRequest = request.Value;
                    scheduled = true;
                    scanAt = Time.realtimeSinceStartup;
                }
            }
            if (!diagnostics.Value) return;
            if (scheduled && Time.realtimeSinceStartup >= scanAt && Player.m_localPlayer)
            {
                scheduled = false;
                try { Collect(); }
                catch (Exception ex) { Logger.LogWarning("Scan collection failed: " + ex); }
            }
            // At most one tree per frame. This is a count budget, not a hard time budget.
            if (pending.Count == 0) return;
            TreeBase tree = pending.Dequeue();
            if (tree)
            {
                try { inspector.Inspect(tree, probe.Value, vertexLimit.Value, probed); }
                catch (Exception ex) { Logger.LogWarning("Tree inspection failed for " + tree.name + ": " + ex); }
            }
            if (pending.Count == 0) { probed.Clear(); Logger.LogInfo("Diagnostic scan complete."); }
        }

        private void Collect()
        {
            pending.Clear(); probed.Clear();
            var seen = new HashSet<int>();
            int liveMatches = 0, prefabMatches = 0;
            Action<TreeBase, bool> add = (tree, isPrefab) =>
            {
                if (!tree || tree.name.IndexOf(filter.Value, StringComparison.OrdinalIgnoreCase) < 0 || !seen.Add(tree.GetInstanceID())) return;
                if (isPrefab)
                {
                    prefabMatches++;
                    if (prefabMatches <= limit.Value) pending.Enqueue(tree);
                }
                else
                {
                    liveMatches++;
                    if (liveMatches <= limit.Value) pending.Enqueue(tree);
                }
            };
            foreach (TreeBase tree in UnityEngine.Object.FindObjectsByType<TreeBase>(FindObjectsInactive.Include, FindObjectsSortMode.None))
                if (tree && tree.gameObject.scene.IsValid() && tree.gameObject.scene.isLoaded) add(tree, false);
            if (prefabs.Value && scene)
            {
                foreach (GameObject prefab in scene.m_prefabs)
                {
                    if (!prefab) continue;
                    foreach (TreeBase tree in prefab.GetComponentsInChildren<TreeBase>(true)) add(tree, true);
                    foreach (Plant plant in prefab.GetComponentsInChildren<Plant>(true))
                        foreach (GameObject grown in plant.m_grownPrefabs)
                        {
                            if (!grown) continue;
                            foreach (TreeBase tree in grown.GetComponentsInChildren<TreeBase>(true))
                            {
                                if (tree.name.IndexOf(filter.Value, StringComparison.OrdinalIgnoreCase) < 0) continue;
                                Logger.LogInfo("Plant mapping: " + prefab.name + " -> " + grown.name + " (plantable species; not proof of instance origin)");
                                add(tree, true);
                            }
                        }
                }
            }
            Logger.LogInfo($"Scan: Unity={Application.unityVersion}, game={(global::Version.CurrentVersion)}, liveMatches={liveMatches}, prefabMatches={prefabMatches}, queued={pending.Count}, liveTruncated={liveMatches > limit.Value}, prefabTruncated={prefabMatches > limit.Value}; piece layer={LayerMask.NameToLayer("piece")}, piece_nonsolid={LayerMask.NameToLayer("piece_nonsolid")}, ghost={LayerMask.NameToLayer("ghost")}");
        }

        internal void LogClip(string message) { Logger.LogInfo(message); }

        private void OnDestroy() { harmony?.UnpatchSelf(); gpu?.Dispose(); pending.Clear(); probed.Clear(); }
    }
}
