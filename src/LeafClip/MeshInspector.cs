using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using Object = UnityEngine.Object;

namespace LeafClip
{
    internal sealed class MeshInspector
    {
        private readonly Action<string> log;
        public MeshInspector(Action<string> log) { this.log = log; }

        internal void Inspect(TreeBase tree, bool probe, int maxVertices, HashSet<int> probed)
        {
            Transform root = tree.transform;
            log($"TREE {tree.name} kind={(tree.gameObject.scene.IsValid() && tree.gameObject.scene.isLoaded ? "live" : "prefab")} id={tree.GetInstanceID()} scene={tree.gameObject.scene.name} root={root.position} scale={root.lossyScale} trunk={(tree.m_trunk ? Path(root, tree.m_trunk.transform) : "missing")} origin=unknown");
            foreach (LODGroup group in tree.GetComponentsInChildren<LODGroup>(true))
            {
                LOD[] lods = group.GetLODs();
                log($" LODGroup {Path(root, group.transform)} levels={lods.Length} fade={group.fadeMode} animatedCrossFade={group.animateCrossFading}");
                for (int i = 0; i < lods.Length; i++)
                    foreach (Renderer renderer in lods[i].renderers)
                        log($"  LOD {i} height={lods[i].screenRelativeTransitionHeight} fadeWidth={lods[i].fadeTransitionWidth} renderer={(renderer ? Path(root, renderer.transform) : "null")}");
            }
            foreach (Renderer renderer in tree.GetComponentsInChildren<Renderer>(true))
            {
                try
                {
                    Mesh mesh = null;
                    if (renderer is SkinnedMeshRenderer skinned) mesh = skinned.sharedMesh;
                    else if (renderer is MeshRenderer)
                    {
                        MeshFilter mf = renderer.GetComponent<MeshFilter>();
                        if (mf) mesh = mf.sharedMesh; // Never use .mesh or .material: those silently instantiate assets.
                    }
                    log($" RENDERER {Path(root, renderer.transform)} type={renderer.GetType().Name} enabled={renderer.enabled} active={renderer.gameObject.activeInHierarchy} staticBatch={renderer.isPartOfStaticBatch} shadow={renderer.shadowCastingMode} receiveShadows={renderer.receiveShadows}");
                    Material[] materials = renderer.sharedMaterials;
                    for (int i = 0; i < materials.Length; i++)
                    {
                        Material m = materials[i];
                        if (!m) { log($"  material[{i}]=null"); continue; }
                        string shader = m.shader ? m.shader.name : "missing";
                        log($"  material[{i}]={m.name} shader={shader} instancing={m.enableInstancing} keywords={string.Join(",", m.shaderKeywords)} hint={Hint(renderer.name + " " + m.name + " " + shader)} (name heuristic only)");
                        foreach (string property in new[] { "_Cutoff", "_AlphaCutoff", "_WindStrength", "_WindScale" })
                            if (m.HasProperty(property)) log($"   {property}={m.GetFloat(property)}");
                    }
                    if (!mesh) { log("  No MeshFilter/SkinnedMesh mesh; billboard/other renderer requires separate investigation."); continue; }
                    log($"  MESH {mesh.name} id={mesh.GetInstanceID()} readable={mesh.isReadable} vertices={mesh.vertexCount} submeshes={mesh.subMeshCount} indexFormat={mesh.indexFormat} blendShapes={mesh.blendShapeCount} bounds={mesh.bounds}");
                    foreach (VertexAttributeDescriptor attribute in mesh.GetVertexAttributes())
                        log($"   attribute={attribute.attribute} format={attribute.format} dimension={attribute.dimension} stream={attribute.stream}");
                    for (int i = 0; i < mesh.subMeshCount; i++)
                        log($"   submesh[{i}] topology={mesh.GetTopology(i)} indices={mesh.GetIndexCount(i)} baseVertex={mesh.GetBaseVertex(i)} materialSlot={(i < materials.Length ? i.ToString() : "MISSING")} hint={(i < materials.Length && materials[i] ? Hint(materials[i].name) : "unknown")}");
                    if (probe && probed.Add(mesh.GetInstanceID())) Probe(mesh, maxVertices);
                }
                catch (Exception ex) { log("  Renderer inspection failed: " + ex.Message); }
            }
        }

        private void Probe(Mesh source, int maxVertices)
        {
            if (!source.isReadable) { log("   COPY skipped: source unreadable; Instantiate is not a read/write workaround."); return; }
            if (source.vertexCount > maxVertices) { log("   COPY skipped: vertex budget exceeded."); return; }
            Mesh copy = null;
            try
            {
                copy = Object.Instantiate(source);
                copy.name = "LeafClip diagnostic temporary copy";
                if (!copy.isReadable || copy.vertexCount != source.vertexCount || copy.subMeshCount != source.subMeshCount)
                    throw new InvalidOperationException("Clone metadata mismatch or unreadable clone.");
                int positions = copy.vertices.Length;
                if (positions != source.vertexCount) throw new InvalidOperationException("Position read count mismatch.");
                log($"   COPY streams: positions={positions}, normals={copy.normals.Length}, tangents={copy.tangents.Length}, colors={copy.colors.Length}, legacyBoneWeights={copy.boneWeights.Length}, bindposes={copy.bindposes.Length}");
                var uv = new List<Vector4>();
                for (int channel = 0; channel < 8; channel++)
                {
                    copy.GetUVs(channel, uv);
                    log($"    UV{channel} count={uv.Count}");
                }
                for (int i = 0; i < copy.subMeshCount; i++)
                    if ((uint)copy.GetIndices(i).Length != copy.GetIndexCount(i)) throw new InvalidOperationException("Index read count mismatch.");
                log("   COPY success: detached Unity clone and CPU stream/index reads; no renderer assignment. Does not prove wind semantics or lossless reconstruction of packed/skinned streams.");
            }
            catch (Exception ex) { log("   COPY failed: " + ex.Message); }
            finally { if (copy) Object.Destroy(copy); }
        }

        private static string Hint(string name)
        {
            string lower = name.ToLowerInvariant();
            bool leaves = lower.Contains("leaf") || lower.Contains("leaves") || lower.Contains("foliage") || lower.Contains("canopy");
            bool trunk = lower.Contains("trunk") || lower.Contains("bark") || lower.Contains("wood");
            return leaves && trunk ? "mixed/ambiguous" : leaves ? "likely foliage" : trunk ? "likely trunk" : "unknown";
        }

        private static string Path(Transform root, Transform target)
        {
            var parts = new List<string>();
            for (Transform current = target; current; current = current.parent)
            {
                parts.Add(current.name + "[" + current.GetSiblingIndex() + "]");
                if (current == root) break;
            }
            parts.Reverse();
            return string.Join("/", parts);
        }
    }
}
