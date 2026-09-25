using System;
using System.Collections.Generic;
using UnityEngine;

namespace LeafClip
{
    // A guarded fallback for single-material vegetation meshes. Preserve the
    // vertically spanning root component; detached canopy cards and branches are
    // eligible for per-triangle clipping. No prefab or species name is involved.
    internal static class TopologyFoliage
    {
        internal static int MarkBranches(Mesh mesh, Material[] materials, uint[] mask, out float branchRadius)
        {
            branchRadius = 0f;
            Vector3[] vertices = mesh.vertices;
            var bark = new List<int>();
            var barkSlots = new List<int>();
            for (int slot = 0; slot < mesh.subMeshCount && slot < materials.Length; slot++)
            {
                Material material = materials[slot];
                if (!material || !material.shader || material.shader.name.IndexOf("Vegetation", StringComparison.OrdinalIgnoreCase) < 0) continue;
                string name = material.name.ToLowerInvariant();
                if (!name.Contains("bark") && !name.Contains("branch") && !name.Contains("wood")) continue;
                barkSlots.Add(slot);
                bark.AddRange(mesh.GetIndices(slot));
            }
            if (bark.Count < 36) return 0;
            float low = float.PositiveInfinity, high = float.NegativeInfinity;
            foreach (int index in bark) { low = Mathf.Min(low, vertices[index].y); high = Mathf.Max(high, vertices[index].y); }
            float height = high - low;
            if (height < 2f) return 0;
            float baseTop = low + height * 0.12f;
            Vector2 center = Vector2.zero; int baseCount = 0;
            foreach (int index in bark)
                if (vertices[index].y <= baseTop) { center += new Vector2(vertices[index].x, vertices[index].z); baseCount++; }
            if (baseCount < 6) return 0;
            center /= baseCount;
            var baseDistances = new List<float>(baseCount);
            foreach (int index in bark)
                if (vertices[index].y <= baseTop)
                    baseDistances.Add(Vector2.Distance(center, new Vector2(vertices[index].x, vertices[index].z)));
            baseDistances.Sort();
            float trunkRadius = baseDistances[(int)((baseDistances.Count - 1) * 0.85f)];
            branchRadius = Mathf.Max(0.45f, trunkRadius * 1.35f);
            int marked = 0;
            foreach (int slot in barkSlots)
            {
                int start = checked((int)mesh.GetIndexStart(slot));
                int[] indices = mesh.GetIndices(slot);
                for (int i = 0; i < indices.Length; i += 3)
                {
                    bool reachesBranch = false;
                    for (int j = 0; j < 3; j++)
                    {
                        Vector3 vertex = vertices[indices[i + j]];
                        if (vertex.y >= low + height * 0.12f &&
                            Vector2.Distance(center, new Vector2(vertex.x, vertex.z)) >= branchRadius)
                            reachesBranch = true;
                    }
                    if (!reachesBranch) continue;
                    // The pixel shader uses the animated world position to keep
                    // the central trunk even on triangles spanning this border.
                    mask[(start + i) / 3] = 2;
                    marked++;
                }
            }
            return marked;
        }

        internal static bool TryClassify(Mesh mesh, out bool[] canopy)
        {
            canopy = null;
            if (!mesh || !mesh.isReadable || mesh.subMeshCount != 1 || mesh.vertexCount < 32 || mesh.GetTopology(0) != MeshTopology.Triangles) return false;
            Vector3[] vertices = mesh.vertices;
            int[] indices = mesh.GetIndices(0);
            if (indices.Length < 60 || indices.Length % 3 != 0) return false;
            int[] parent = new int[vertices.Length];
            for (int i = 0; i < parent.Length; i++) parent[i] = i;
            for (int i = 0; i < indices.Length; i += 3)
            {
                Union(parent, indices[i], indices[i + 1]);
                Union(parent, indices[i], indices[i + 2]);
            }

            float[] lowest = new float[vertices.Length], highest = new float[vertices.Length];
            int[] triangleCount = new int[vertices.Length];
            float meshBottom = float.PositiveInfinity, meshTop = float.NegativeInfinity;
            for (int i = 0; i < vertices.Length; i++) { lowest[i] = float.PositiveInfinity; highest[i] = float.NegativeInfinity; }
            for (int i = 0; i < vertices.Length; i++)
            {
                int root = Find(parent, i);
                lowest[root] = Mathf.Min(lowest[root], vertices[i].y);
                highest[root] = Mathf.Max(highest[root], vertices[i].y);
                meshBottom = Mathf.Min(meshBottom, vertices[i].y);
                meshTop = Mathf.Max(meshTop, vertices[i].y);
            }
            for (int i = 0; i < indices.Length; i += 3) triangleCount[Find(parent, indices[i])]++;

            float totalHeight = meshTop - meshBottom;
            if (totalHeight < 1f) return false;
            int trunk = -1;
            float bestSpan = 0f;
            for (int i = 0; i < vertices.Length; i++)
            {
                if (triangleCount[i] < 12) continue;
                float span = highest[i] - lowest[i];
                if (span > bestSpan) { bestSpan = span; trunk = i; }
            }
            if (trunk < 0 || bestSpan < totalHeight * 0.5f || lowest[trunk] > meshBottom + totalHeight * 0.1f) return false;

            bool[] selected = new bool[indices.Length / 3];
            int selectedCount = 0;
            for (int i = 0; i < selected.Length; i++)
            {
                selected[i] = Find(parent, indices[i * 3]) != trunk;
                if (selected[i]) selectedCount++;
            }
            if (selectedCount < 10 || selectedCount >= selected.Length) return false;
            canopy = selected;
            return true;
        }

        private static int Find(int[] parent, int node)
        {
            while (parent[node] != node)
            {
                parent[node] = parent[parent[node]];
                node = parent[node];
            }
            return node;
        }

        private static void Union(int[] parent, int a, int b)
        {
            int rootA = Find(parent, a), rootB = Find(parent, b);
            if (rootA != rootB) parent[rootB] = rootA;
        }
    }
}
