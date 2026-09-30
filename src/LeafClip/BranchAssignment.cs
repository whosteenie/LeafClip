using System;
using System.Collections.Generic;
using UnityEngine;

namespace LeafClip
{
    // Assign foliage to bark and retain a rooted support path for each assigned
    // leaf group. The path lets the shader carry a parent-branch cut to its
    // children without removing unaffected pixels of the same branch.
    internal static class BranchAssignment
    {
        internal struct Summary
        {
            internal int Branches, LeafGroups, Assigned, Unassigned, Guided;
            internal int Supported, SupportFallback, GuidedBark, BarkGuides;
        }

        private sealed class KdNode
        {
            internal int Vertex, Axis, Left = -1, Right = -1;
        }

        private struct Edge
        {
            internal int To;
            internal float Length;
            internal Edge(int to, float length) { To = to; Length = length; }
        }

        private struct HeapEntry
        {
            internal int Vertex;
            internal float Distance;
            internal HeapEntry(int vertex, float distance) { Vertex = vertex; Distance = distance; }
        }

        internal static Summary Assign(Mesh mesh, uint[] mask, float branchRadius,
            Matrix4x4 world, out float[] guides)
        {
            guides = Array.Empty<float>();
            var summary = new Summary();
            Vector3[] vertices = mesh.vertices;
            int count = vertices.Length;
            var bark = new bool[count];
            var leaf = new bool[count];
            var triangles = new List<(int a, int b, int c, int slot)>();
            for (int slot = 0; slot < mesh.subMeshCount; slot++)
            {
                int start = checked((int)mesh.GetIndexStart(slot));
                int[] indices = mesh.GetIndices(slot);
                for (int i = 0; i < indices.Length; i += 3)
                {
                    int triangle = (start + i) / 3;
                    if (triangle >= mask.Length) continue;
                    uint type = mask[triangle] & 3u;
                    if (type != 1 && type != 2 && type != 3) continue;
                    int a = indices[i], b = indices[i + 1], c = indices[i + 2];
                    triangles.Add((a, b, c, triangle));
                    bool[] used = type == 1 ? leaf : bark;
                    used[a] = used[b] = used[c] = true;
                }
            }
            float low = float.PositiveInfinity, high = float.NegativeInfinity;
            for (int i = 0; i < count; i++) if (bark[i])
            {
                low = Mathf.Min(low, vertices[i].y);
                high = Mathf.Max(high, vertices[i].y);
            }
            if (float.IsInfinity(low) || float.IsNaN(low) || high - low < 1f || branchRadius <= 0) return summary;
            float baseTop = low + (high - low) * 0.12f;
            Vector2 center = Vector2.zero; int baseCount = 0;
            for (int i = 0; i < count; i++) if (bark[i] && vertices[i].y <= baseTop)
            {
                center += new Vector2(vertices[i].x, vertices[i].z);
                baseCount++;
            }
            if (baseCount == 0) return summary;
            center /= baseCount;
            var outside = new bool[count];
            var barkParent = Parents(count);
            var leafParent = Parents(count);
            var weld = new Dictionary<Vector3Int, int>();
            for (int i = 0; i < count; i++) if (bark[i])
            {
                Vector3 point = vertices[i];
                outside[i] = point.y >= baseTop &&
                    Vector2.Distance(new Vector2(point.x, point.z), center) >= branchRadius;
                if (!outside[i]) continue;
                // Import seams often duplicate a position with different UVs.
                var key = new Vector3Int(Mathf.RoundToInt(point.x * 1000f),
                    Mathf.RoundToInt(point.y * 1000f), Mathf.RoundToInt(point.z * 1000f));
                if (weld.TryGetValue(key, out int peer)) Union(barkParent, i, peer);
                else weld[key] = i;
            }
            foreach (var tri in triangles)
            {
                uint type = mask[tri.slot] & 3u;
                if (type == 1)
                {
                    Union(leafParent, tri.a, tri.b);
                    Union(leafParent, tri.a, tri.c);
                }
                else
                {
                    if (outside[tri.a] && outside[tri.b]) Union(barkParent, tri.a, tri.b);
                    if (outside[tri.a] && outside[tri.c]) Union(barkParent, tri.a, tri.c);
                    if (outside[tri.b] && outside[tri.c]) Union(barkParent, tri.b, tri.c);
                }
            }
            var sizes = new Dictionary<int, int>();
            for (int i = 0; i < count; i++) if (outside[i])
            {
                int root = Find(barkParent, i);
                sizes[root] = sizes.TryGetValue(root, out int size) ? size + 1 : 1;
            }
            var ordered = new List<KeyValuePair<int, int>>(sizes);
            ordered.Sort((a, b) => b.Value.CompareTo(a.Value));
            var ids = new Dictionary<int, uint>();
            foreach (var pair in ordered)
                if (pair.Value >= 3 && summary.Branches < 1023)
                    ids[pair.Key] = (uint)++summary.Branches;
            var vertexId = new uint[count];
            var branchPoints = new List<int>();
            for (int i = 0; i < count; i++) if (outside[i] && ids.TryGetValue(Find(barkParent, i), out uint id))
            {
                vertexId[i] = id;
                branchPoints.Add(i);
            }
            foreach (var tri in triangles) if ((mask[tri.slot] & 3u) == 2)
            {
                uint id = vertexId[tri.a] != 0 ? vertexId[tri.a] :
                    vertexId[tri.b] != 0 ? vertexId[tri.b] : vertexId[tri.c];
                mask[tri.slot] |= id << 2;
            }
            if (branchPoints.Count == 0) return summary;
            var kdNodes = new List<KdNode>(branchPoints.Count);
            int kdRoot = BuildKd(branchPoints, 0, branchPoints.Count, 0, vertices, kdNodes);
            var leafGroups = new Dictionary<int, HashSet<int>>();
            foreach (var tri in triangles) if ((mask[tri.slot] & 3u) == 1)
            {
                int root = Find(leafParent, tri.a);
                if (!leafGroups.TryGetValue(root, out HashSet<int> group))
                    leafGroups[root] = group = new HashSet<int>();
                group.Add(tri.a); group.Add(tri.b); group.Add(tri.c);
            }
            var leafIds = new Dictionary<int, uint>();
            var guideVertices = new Dictionary<int, int>();
            var guideData = new float[255 * 4];
            int guideCount = 0;
            float maxDistance = Mathf.Max(2.5f, branchRadius * 2f);
            foreach (var group in leafGroups)
            {
                float best = maxDistance * maxDistance;
                uint id = 0;
                int nearestVertex = -1;
                foreach (int vertex in group.Value)
                    Nearest(kdRoot, vertices[vertex], vertices, vertexId, kdNodes,
                        ref best, ref id, ref nearestVertex);
                leafIds[group.Key] = id;
                if (id == 0) summary.Unassigned++; else summary.Assigned++;
                // An assigned leaf group follows its owning bark route, then
                // tests its own animated pixels locally. The horizontal trunk
                // ray remains the fallback for unassigned groups.
                if (id != 0 && nearestVertex >= 0)
                    guideVertices[group.Key] = nearestVertex;
            }
            summary.LeafGroups = leafGroups.Count;
            foreach (var tri in triangles) if ((mask[tri.slot] & 3u) == 1)
            {
                int root = Find(leafParent, tri.a);
                mask[tri.slot] |= leafIds[root] << 2;
            }
            if (summary.Branches != 0)
            {
                // Branch support is traced on the undeformed bark mesh. All
                // trunk-side bark vertices are roots, so a child follows its
                // actual parent branch instead of an imagined horizontal ray.
                int[] supportParent = BuildSupportPaths(vertices, bark, triangles, mask,
                    center, baseTop, branchRadius, out float[] supportDistance);
                // A waypoint can have different predecessors on two simplified
                // paths. Include the predecessor in the key so reusing a node
                // never silently drops one child's parent branch.
                var pathNodes = new Dictionary<(int vertex, uint parent), uint>();
                uint AllocateBarkGuide(int vertex, int limit = 254)
                {
                    if (guideCount >= limit) { summary.SupportFallback++; return 0; }
                    List<int> path = SimplifySupportPath(vertex, supportParent,
                        vertices, world, center, vertexId);
                    if (path == null) { summary.SupportFallback++; return 0; }
                    uint barkGuide = (uint)(guideCount + 1);
                    uint parentGuide = 0;
                    var pending = new List<(int vertex, uint parent, uint id)>();
                    foreach (int waypoint in path)
                    {
                        var key = (waypoint, parentGuide);
                        if (!pathNodes.TryGetValue(key, out uint node))
                        {
                            if (guideCount + 1 + pending.Count >= limit)
                            { summary.SupportFallback++; return 0; }
                            node = (uint)(guideCount + 2 + pending.Count);
                            pending.Add((waypoint, parentGuide, node));
                        }
                        parentGuide = node;
                    }
                    guideCount++;
                    foreach (var step in pending)
                    {
                        pathNodes[(step.vertex, step.parent)] = step.id;
                        Vector3 position = world.MultiplyPoint3x4(vertices[step.vertex]);
                        int at = (int)step.id * 4;
                        guideData[at] = position.x;
                        guideData[at + 1] = position.y;
                        guideData[at + 2] = position.z;
                        guideData[at + 3] = step.parent;
                    }
                    guideCount += pending.Count;
                    Vector3 anchor = world.MultiplyPoint3x4(vertices[vertex]);
                    int barkAt = (int)barkGuide * 4;
                    guideData[barkAt] = anchor.x;
                    guideData[barkAt + 1] = anchor.y;
                    guideData[barkAt + 2] = anchor.z;
                    guideData[barkAt + 3] = parentGuide;
                    summary.BarkGuides++;
                    return barkGuide;
                }
                // A leaf attachment alone does not cover the child bark. Give
                // each bark region a support at its trunk-side root, then test
                // that rooted ancestry plus the actual animated bark pixel.
                // The root is the earliest vertex in the region reachable from
                // the trunk in the bark graph, so proximal bark stays visible.
                var regionRoots = new Dictionary<uint, int>();
                var regionVertices = new Dictionary<uint, List<int>>();
                foreach (int vertex in branchPoints)
                {
                    uint region = vertexId[vertex];
                    if (float.IsInfinity(supportDistance[vertex])) continue;
                    if (!regionVertices.TryGetValue(region, out List<int> members))
                        regionVertices[region] = members = new List<int>();
                    members.Add(vertex);
                    if (!regionRoots.TryGetValue(region, out int prior) ||
                        supportDistance[vertex] < supportDistance[prior])
                        regionRoots[region] = vertex;
                }
                var regionGuides = new Dictionary<uint, List<(int vertex, uint guide)>>();
                foreach (var region in regionRoots)
                {
                    uint guide = AllocateBarkGuide(region.Value);
                    if (guide != 0)
                        regionGuides[region.Key] = new List<(int vertex, uint guide)> { (region.Value, guide) };
                }
                // A connected region can contain several forks. Add spaced
                // anchors after every region has its root; each anchor follows
                // its own route through the bark graph to the trunk.
                int extraLimit = 254 - Math.Min(guideVertices.Count, 128);
                foreach (var region in regionGuides)
                {
                    var anchors = region.Value;
                    for (int extra = 0; extra < 5 && guideCount < extraLimit; extra++)
                    {
                        int farthest = -1;
                        float bestDistance = 1.5f * 1.5f;
                        foreach (int candidate in regionVertices[region.Key])
                        {
                            Vector3 point = world.MultiplyPoint3x4(vertices[candidate]);
                            float nearest = float.PositiveInfinity;
                            foreach (var anchor in anchors)
                                nearest = Mathf.Min(nearest, (point - world.MultiplyPoint3x4(vertices[anchor.vertex])).sqrMagnitude);
                            if (nearest > bestDistance) { bestDistance = nearest; farthest = candidate; }
                        }
                        if (farthest < 0) break;
                        uint guide = AllocateBarkGuide(farthest, extraLimit);
                        if (guide == 0) break;
                        anchors.Add((farthest, guide));
                    }
                }
                foreach (var tri in triangles) if ((mask[tri.slot] & 3u) == 2)
                {
                    uint region = (mask[tri.slot] >> 2) & 1023u;
                    if (!regionGuides.TryGetValue(region, out var anchors)) continue;
                    Vector3 point = world.MultiplyPoint3x4((vertices[tri.a] + vertices[tri.b] + vertices[tri.c]) / 3f);
                    float best = float.PositiveInfinity;
                    uint guide = 0;
                    foreach (var anchor in anchors)
                    {
                        float distance = (point - world.MultiplyPoint3x4(vertices[anchor.vertex])).sqrMagnitude;
                        if (distance < best) { best = distance; guide = anchor.guide; }
                    }
                    mask[tri.slot] |= guide << 12;
                    summary.GuidedBark++;
                }

                // Bark gets the limited guide slots first. A child branch must
                // retain its parent path even on a mesh with many leaf cards.
                var leafGuides = new Dictionary<int, uint>();
                foreach (var pair in guideVertices)
                {
                    if (guideCount >= 254) { summary.SupportFallback++; continue; }
                    int anchorVertex = pair.Value;
                    uint leafGuide = (uint)(guideCount + 1);
                    uint parentGuide = 0;
                    var pending = new List<(int vertex, uint parent, uint id)>();
                    if (supportParent[anchorVertex] >= 0)
                    {
                        List<int> path = SimplifySupportPath(anchorVertex, supportParent,
                            vertices, world, center, vertexId);
                        bool full = path == null;
                        if (!full) foreach (int waypoint in path)
                        {
                            var key = (waypoint, parentGuide);
                            if (!pathNodes.TryGetValue(key, out uint node))
                            {
                                if (guideCount + 1 + pending.Count >= 254) { full = true; break; }
                                node = (uint)(guideCount + 2 + pending.Count);
                                pending.Add((waypoint, parentGuide, node));
                            }
                            parentGuide = node;
                        }
                        if (full) { pending.Clear(); parentGuide = 0; summary.SupportFallback++; }
                        else summary.Supported++;
                    }
                    else summary.SupportFallback++;
                    guideCount++;
                    foreach (var step in pending)
                    {
                        pathNodes[(step.vertex, step.parent)] = step.id;
                        Vector3 position = world.MultiplyPoint3x4(vertices[step.vertex]);
                        int at = (int)step.id * 4;
                        guideData[at] = position.x;
                        guideData[at + 1] = position.y;
                        guideData[at + 2] = position.z;
                        guideData[at + 3] = step.parent;
                    }
                    guideCount += pending.Count;
                    Vector3 anchor = world.MultiplyPoint3x4(vertices[anchorVertex]);
                    int leafAt = (int)leafGuide * 4;
                    guideData[leafAt] = anchor.x;
                    guideData[leafAt + 1] = anchor.y;
                    guideData[leafAt + 2] = anchor.z;
                    guideData[leafAt + 3] = parentGuide;
                    leafGuides[pair.Key] = leafGuide;
                    summary.Guided++;
                }
                foreach (var tri in triangles) if ((mask[tri.slot] & 3u) == 1)
                {
                    int root = Find(leafParent, tri.a);
                    if (leafGuides.TryGetValue(root, out uint guide)) mask[tri.slot] |= guide << 12;
                }
            }
            if (guideCount != 0) guides = guideData;
            return summary;
        }

        private static int[] Parents(int count)
        {
            var result = new int[count];
            for (int i = 0; i < count; i++) result[i] = i;
            return result;
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
            int x = Find(parent, a), y = Find(parent, b);
            if (x != y) parent[y] = x;
        }
        private static int BuildKd(List<int> points, int start, int end, int depth,
            Vector3[] vertices, List<KdNode> nodes)
        {
            if (start == end) return -1;
            int axis = depth % 3;
            points.Sort(start, end - start, Comparer<int>.Create((a, b) =>
                vertices[a][axis].CompareTo(vertices[b][axis])));
            int middle = (start + end) / 2;
            int node = nodes.Count;
            nodes.Add(new KdNode { Vertex = points[middle], Axis = axis });
            nodes[node].Left = BuildKd(points, start, middle, depth + 1, vertices, nodes);
            nodes[node].Right = BuildKd(points, middle + 1, end, depth + 1, vertices, nodes);
            return node;
        }
        private static void Nearest(int node, Vector3 point, Vector3[] vertices, uint[] ids,
            List<KdNode> nodes, ref float best, ref uint id, ref int nearestVertex)
        {
            if (node < 0) return;
            KdNode current = nodes[node];
            Vector3 candidate = vertices[current.Vertex];
            float distance = (point - candidate).sqrMagnitude;
            if (distance < best) { best = distance; id = ids[current.Vertex]; nearestVertex = current.Vertex; }
            float delta = point[current.Axis] - candidate[current.Axis];
            int near = delta < 0 ? current.Left : current.Right;
            int far = delta < 0 ? current.Right : current.Left;
            Nearest(near, point, vertices, ids, nodes, ref best, ref id, ref nearestVertex);
            if (delta * delta < best) Nearest(far, point, vertices, ids, nodes, ref best, ref id, ref nearestVertex);
        }

        private static int[] BuildSupportPaths(Vector3[] vertices, bool[] bark,
            List<(int a, int b, int c, int slot)> triangles, uint[] mask, Vector2 center,
            float baseTop, float branchRadius, out float[] supportDistance)
        {
            int count = vertices.Length;
            var canonical = new int[count];
            var weld = new Dictionary<Vector3Int, int>();
            for (int i = 0; i < count; i++)
            {
                canonical[i] = i;
                if (!bark[i]) continue;
                Vector3 point = vertices[i];
                var key = new Vector3Int(Mathf.RoundToInt(point.x * 1000f),
                    Mathf.RoundToInt(point.y * 1000f), Mathf.RoundToInt(point.z * 1000f));
                if (weld.TryGetValue(key, out int peer)) canonical[i] = peer;
                else weld[key] = i;
            }
            var edges = new List<Edge>[count];
            var components = Parents(count);
            for (int i = 0; i < count; i++) if (bark[i] && canonical[i] == i) edges[i] = new List<Edge>();
            foreach (var tri in triangles) if ((mask[tri.slot] & 3u) != 1 && bark[tri.a] && bark[tri.b] && bark[tri.c])
            {
                // Foliage cards may share a vertex with bark. Only actual bark
                // triangles can establish a support connection.
                int a = canonical[tri.a], b = canonical[tri.b], c = canonical[tri.c];
                if (edges[a] == null || edges[b] == null || edges[c] == null) continue;
                AddEdge(a, b, vertices, edges);
                AddEdge(a, c, vertices, edges);
                AddEdge(b, c, vertices, edges);
                Union(components, a, b);
                Union(components, a, c);
            }
            // Imported child branches often overlap their parent without
            // sharing mesh vertices. Join only the nearest pair of surfaces
            // for each touching component pair, never every nearby vertex.
            var cells = new Dictionary<Vector3Int, List<int>>();
            var bridges = new Dictionary<long, (int a, int b, float distance)>();
            const float cellSize = 0.65f;
            for (int i = 0; i < count; i++) if (edges[i] != null)
            {
                Vector3 point = vertices[i];
                int x = Mathf.FloorToInt(point.x / cellSize);
                int y = Mathf.FloorToInt(point.y / cellSize);
                int z = Mathf.FloorToInt(point.z / cellSize);
                for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++)
                {
                    if (!cells.TryGetValue(new Vector3Int(x + dx, y + dy, z + dz), out List<int> nearby)) continue;
                    foreach (int peer in nearby)
                    {
                        int a = Find(components, i), b = Find(components, peer);
                        if (a == b) continue;
                        float gap = Vector3.Distance(point, vertices[peer]);
                        if (gap > cellSize) continue;
                        long key = ((long)Math.Min(a, b) << 32) | (uint)Math.Max(a, b);
                        if (!bridges.TryGetValue(key, out var old) || gap < old.distance)
                            bridges[key] = (i, peer, gap);
                    }
                }
                var cell = new Vector3Int(x, y, z);
                if (!cells.TryGetValue(cell, out List<int> stored)) cells[cell] = stored = new List<int>();
                stored.Add(i);
            }
            foreach (var bridge in bridges.Values)
                AddEdge(bridge.a, bridge.b, vertices, edges);
            var parent = new int[count];
            var distance = new float[count];
            supportDistance = distance;
            var heap = new List<HeapEntry>();
            for (int i = 0; i < count; i++) { parent[i] = -1; distance[i] = float.PositiveInfinity; }
            for (int i = 0; i < count; i++) if (edges[i] != null)
            {
                Vector3 point = vertices[i];
                if (point.y < baseTop ||
                    Vector2.Distance(new Vector2(point.x, point.z), center) >= branchRadius) continue;
                distance[i] = 0f;
                Push(heap, new HeapEntry(i, 0f));
            }
            while (heap.Count != 0)
            {
                HeapEntry current = Pop(heap);
                if (current.Distance > distance[current.Vertex]) continue;
                foreach (Edge edge in edges[current.Vertex])
                {
                    float candidate = current.Distance + edge.Length;
                    if (candidate >= distance[edge.To]) continue;
                    distance[edge.To] = candidate;
                    parent[edge.To] = current.Vertex;
                    Push(heap, new HeapEntry(edge.To, candidate));
                }
            }
            for (int i = 0; i < count; i++) if (canonical[i] != i)
            {
                parent[i] = canonical[i];
                distance[i] = distance[canonical[i]];
            }
            return parent;
        }

        private static void AddEdge(int a, int b, Vector3[] vertices, List<Edge>[] edges)
        {
            if (a == b) return;
            float length = Vector3.Distance(vertices[a], vertices[b]);
            if (length < 1e-5f) return;
            edges[a].Add(new Edge(b, length));
            edges[b].Add(new Edge(a, length));
        }

        private static List<int> SimplifySupportPath(int vertex, int[] parent,
            Vector3[] vertices, Matrix4x4 world, Vector2 center, uint[] branchIds)
        {
            var path = new List<int>();
            int current = vertex;
            for (int steps = 0; steps < parent.Length && parent[current] >= 0; steps++)
            {
                current = parent[current];
                if (parent[current] < 0) break;
                path.Add(current);
            }
            path.Reverse();
            var selected = new List<int>();
            // Keep both sides of every branch junction. An otherwise straight
            // shortcut from trunk to child can miss a wall that cuts the parent.
            uint previous = 0;
            for (int i = 0; i < path.Count; i++)
            {
                uint branch = branchIds[path[i]];
                if (branch == previous) continue;
                if (i > 0 && !selected.Contains(i - 1)) selected.Add(i - 1);
                if (!selected.Contains(i)) selected.Add(i);
                previous = branch;
            }
            if (selected.Count > 10) return null;
            Vector3 attachment = world.MultiplyPoint3x4(vertices[vertex]);
            Vector3 trunk = world.MultiplyPoint3x4(new Vector3(center.x, vertices[vertex].y, center.y));
            // Add bends where the supported route differs from a straight
            // segment. Ten waypoints plus the local and trunk segments fit the
            // shader's twelve-segment limit; never truncate a branch junction.
            while (selected.Count < 10)
            {
                float largest = 0.25f * 0.25f;
                int best = -1;
                for (int i = 0; i < path.Count; i++)
                {
                    if (selected.Contains(i)) continue;
                    Vector3 point = world.MultiplyPoint3x4(vertices[path[i]]);
                    Vector3 before = trunk, after = attachment;
                    foreach (int position in selected)
                    {
                        Vector3 support = world.MultiplyPoint3x4(vertices[path[position]]);
                        if (position < i) before = support;
                        else { after = support; break; }
                    }
                    Vector3 direction = after - before;
                    float fraction = direction.sqrMagnitude > 1e-6f ?
                        Mathf.Clamp01(Vector3.Dot(point - before, direction) / direction.sqrMagnitude) : 0f;
                    float error = (point - (before + fraction * direction)).sqrMagnitude;
                    if (error > largest) { largest = error; best = i; }
                }
                if (best < 0) break;
                selected.Add(best);
                selected.Sort();
            }
            var result = new List<int>(selected.Count);
            foreach (int position in selected) result.Add(path[position]);
            return result;
        }

        private static void Push(List<HeapEntry> heap, HeapEntry entry)
        {
            int index = heap.Count;
            heap.Add(entry);
            while (index > 0)
            {
                int parent = (index - 1) / 2;
                if (heap[parent].Distance <= entry.Distance) break;
                heap[index] = heap[parent];
                index = parent;
            }
            heap[index] = entry;
        }

        private static HeapEntry Pop(List<HeapEntry> heap)
        {
            HeapEntry result = heap[0];
            HeapEntry last = heap[heap.Count - 1];
            heap.RemoveAt(heap.Count - 1);
            int index = 0;
            while (index * 2 + 1 < heap.Count)
            {
                int child = index * 2 + 1;
                if (child + 1 < heap.Count && heap[child + 1].Distance < heap[child].Distance) child++;
                if (heap[child].Distance >= last.Distance) break;
                heap[index] = heap[child];
                index = child;
            }
            if (heap.Count != 0) heap[index] = last;
            return result;
        }
    }
}
