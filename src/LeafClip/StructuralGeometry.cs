using System;
using System.Collections.Generic;
using UnityEngine;

namespace LeafClip
{
    // Unity access stays on the main thread. Only packed float arrays cross to
    // the native geometry worker. Shared collider meshes are read once per world.
    internal sealed class StructuralGeometry
    {
        private sealed class Source
        {
            internal Vector3[] Vertices;
            internal int[] Indices;
        }
        private sealed class CachedPiece
        {
            internal float[] Triangles;
            internal Bounds Bounds;
        }
        private sealed class WallRange
        {
            internal Bounds Bounds;
            internal int First, End, Parent;
            internal bool AlongX;
        }
        private readonly Dictionary<Mesh, Source> sources = new Dictionary<Mesh, Source>();
        private readonly Dictionary<Piece, CachedPiece> pieces = new Dictionary<Piece, CachedPiece>();
        internal void Clear() { sources.Clear(); pieces.Clear(); }
        internal void Invalidate(Piece piece) { if (piece) pieces.Remove(piece); }
        internal void Snapshot(List<Collider> colliders, Bounds influence, out float[] boxes, out float[] triangles, out int visualPieceCount, out int fallbackPieceCount, out int wallCandidates, out int joinedWalls, out int skipped)
        {
            var boxData = new List<float>();
            var triangleData = new List<float>();
            var renderedPieces = new HashSet<Piece>();
            var wallRanges = new List<WallRange>();
            var colliderPieces = new HashSet<Piece>();
            var fallbackPieces = new HashSet<Piece>();
            skipped = 0;
            foreach (Collider collider in colliders)
            {
                if (!collider || !collider.enabled || !collider.gameObject.activeInHierarchy) continue;
                Piece piece = collider.GetComponentInParent<Piece>();
                if (piece && !colliderPieces.Add(piece)) continue;
                if (piece)
                {
                    int first = triangleData.Count;
                    if (AppendVisualSurfaces(piece, influence, triangleData))
                    {
                        renderedPieces.Add(piece);
                        if (triangleData.Count > first && (piece.m_usage & Piece.UsageTagFlags.Wall) != 0)
                        {
                            Bounds bounds = pieces[piece].Bounds;
                            bool alongX = bounds.size.x > bounds.size.z * 1.5f;
                            bool alongZ = bounds.size.z > bounds.size.x * 1.5f;
                            if (alongX || alongZ)
                                wallRanges.Add(new WallRange { Bounds = bounds, First = first, End = triangleData.Count,
                                    Parent = wallRanges.Count, AlongX = alongX });
                        }
                    }
                }
            }
            wallCandidates = wallRanges.Count;
            joinedWalls = MergeWallSpans(wallRanges, triangleData);
            foreach (Collider collider in colliders)
            {
                if (!collider || !collider.enabled || !collider.gameObject.activeInHierarchy) continue;
                Piece piece = collider.GetComponentInParent<Piece>();
                if (piece && renderedPieces.Contains(piece)) continue;
                if (piece) fallbackPieces.Add(piece);
                if (collider is BoxCollider box)
                {
                    Vector3 half = box.size * 0.5f;
                    if (half.x <= 0 || half.y <= 0 || half.z <= 0) continue;
                    Matrix4x4 inverse = Matrix4x4.Scale(new Vector3(1f / half.x, 1f / half.y, 1f / half.z)) * Matrix4x4.Translate(-box.center) * box.transform.worldToLocalMatrix;
                    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) boxData.Add(inverse[r, c]);
                    // Derive bounds from exactly the same transform as the narrow
                    // phase, avoiding a mismatch with physics' scale handling.
                    Matrix4x4 world = inverse.inverse;
                    Bounds bounds = new Bounds(world.MultiplyPoint3x4(Vector3.one), Vector3.zero);
                    for (int corner = 0; corner < 8; corner++) bounds.Encapsulate(world.MultiplyPoint3x4(new Vector3(
                        (corner & 1) == 0 ? -1 : 1, (corner & 2) == 0 ? -1 : 1, (corner & 4) == 0 ? -1 : 1)));
                    Add(boxData, bounds.min); Add(boxData, bounds.max);
                }
                else if (collider is MeshCollider meshCollider && meshCollider.sharedMesh)
                {
                    Mesh mesh = meshCollider.sharedMesh;
                    if (!sources.TryGetValue(mesh, out Source source))
                    {
                        if (mesh.vertexCount > 400000) throw new InvalidOperationException("Collider mesh exceeds the geometry snapshot budget.");
                        source = mesh.isReadable ? new Source { Vertices = mesh.vertices, Indices = mesh.triangles } : null;
                        sources[mesh] = source;
                    }
                    if (source == null) { skipped++; continue; }
                    if (boxData.Count / 18 + triangleData.Count / 11 + source.Indices.Length / 3 > 131072)
                        throw new InvalidOperationException("Obstruction geometry exceeds 131072 primitives; original rendering retained.");
                    Matrix4x4 world = meshCollider.transform.localToWorldMatrix;
                    for (int i = 0; i < source.Indices.Length; i += 3)
                        AddTriangle(triangleData,
                            world.MultiplyPoint3x4(source.Vertices[source.Indices[i]]),
                            world.MultiplyPoint3x4(source.Vertices[source.Indices[i + 1]]),
                            world.MultiplyPoint3x4(source.Vertices[source.Indices[i + 2]]), collider.bounds);
                }
                else skipped++;
                // Bound snapshot memory before a pathological modded asset can
                // stall the main thread or allocate an unbounded hierarchy.
                if (boxData.Count / 18 + triangleData.Count / 11 > 131072)
                    throw new InvalidOperationException("Obstruction geometry exceeds 131072 primitives; original rendering retained.");
            }
            boxes = boxData.ToArray(); triangles = triangleData.ToArray();
            visualPieceCount = renderedPieces.Count; fallbackPieceCount = fallbackPieces.Count;
        }
        private static int Root(List<WallRange> walls, int index)
        {
            while (walls[index].Parent != index)
            {
                walls[index].Parent = walls[walls[index].Parent].Parent;
                index = walls[index].Parent;
            }
            return index;
        }
        private static int MergeWallSpans(List<WallRange> walls, List<float> triangles)
        {
            if (walls.Count < 2) return 0;
            int joined = 0;
            var order = new List<int>(walls.Count);
            for (int i = 0; i < walls.Count; i++) order.Add(i);
            order.Sort((left, right) =>
            {
                WallRange a = walls[left], b = walls[right];
                if (a.AlongX != b.AlongX) return a.AlongX ? -1 : 1;
                return (a.AlongX ? a.Bounds.min.x : a.Bounds.min.z)
                    .CompareTo(b.AlongX ? b.Bounds.min.x : b.Bounds.min.z);
            });
            for (int sorted = 0; sorted < order.Count; sorted++)
            {
                int i = order[sorted]; WallRange a = walls[i];
                float aEnd = a.AlongX ? a.Bounds.max.x : a.Bounds.max.z;
                for (int next = sorted + 1; next < order.Count; next++)
                {
                    int j = order[next]; WallRange b = walls[j];
                    if (a.AlongX != b.AlongX) break;
                    float bStart = b.AlongX ? b.Bounds.min.x : b.Bounds.min.z;
                    if (bStart > aEnd + 0.12f) break;
                    // Only join snapped peers on the same plane and at the same
                    // height; corners, staggered walls and openings stay separate.
                    if (Mathf.Abs(bStart - aEnd) > 0.12f ||
                        Mathf.Abs(a.Bounds.min.y - b.Bounds.min.y) > 0.1f ||
                        Mathf.Abs(a.Bounds.max.y - b.Bounds.max.y) > 0.1f) continue;
                    float aThinMin = a.AlongX ? a.Bounds.min.z : a.Bounds.min.x;
                    float aThinMax = a.AlongX ? a.Bounds.max.z : a.Bounds.max.x;
                    float bThinMin = b.AlongX ? b.Bounds.min.z : b.Bounds.min.x;
                    float bThinMax = b.AlongX ? b.Bounds.max.z : b.Bounds.max.x;
                    if (Mathf.Min(aThinMax, bThinMax) - Mathf.Max(aThinMin, bThinMin) < 0.03f) continue;
                    int rootA = Root(walls, i), rootB = Root(walls, j);
                    if (rootA != rootB) { walls[rootB].Parent = rootA; joined++; }
                }
            }
            var lows = new float[walls.Count]; var highs = new float[walls.Count];
            for (int i = 0; i < walls.Count; i++) { lows[i] = float.PositiveInfinity; highs[i] = float.NegativeInfinity; }
            for (int i = 0; i < walls.Count; i++)
            {
                int root = Root(walls, i); Bounds bounds = walls[i].Bounds;
                lows[root] = Mathf.Min(lows[root], walls[i].AlongX ? bounds.min.x : bounds.min.z);
                highs[root] = Mathf.Max(highs[root], walls[i].AlongX ? bounds.max.x : bounds.max.z);
            }
            for (int i = 0; i < walls.Count; i++)
            {
                WallRange wall = walls[i]; int root = Root(walls, i);
                for (int at = wall.First; at < wall.End; at += 11)
                {
                    Vector3 a = new Vector3(triangles[at], triangles[at + 1], triangles[at + 2]);
                    Vector3 b = new Vector3(triangles[at + 3], triangles[at + 4], triangles[at + 5]);
                    Vector3 c = new Vector3(triangles[at + 6], triangles[at + 7], triangles[at + 8]);
                    Vector3 normal = Vector3.Cross(b - a, c - a);
                    float facing = wall.AlongX ? Mathf.Abs(normal.z) : Mathf.Abs(normal.x);
                    if (facing < Mathf.Abs(normal.y) || facing < (wall.AlongX ? Mathf.Abs(normal.x) : Mathf.Abs(normal.z))) continue;
                    triangles[at + 9] = lows[root]; triangles[at + 10] = highs[root];
                }
            }
            return joined;
        }
        private bool AppendVisualSurfaces(Piece piece, Bounds influence, List<float> triangles)
        {
            if (!pieces.TryGetValue(piece, out CachedPiece cached))
            {
            var worldVertices = new List<Vector3>();
            Bounds visualBounds = default(Bounds);
            bool hasBounds = false;
            foreach (MeshRenderer renderer in piece.GetComponentsInChildren<MeshRenderer>())
            {
                if (!renderer || !renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                if (renderer.isPartOfStaticBatch) return false;
                MeshFilter filter = renderer.GetComponent<MeshFilter>();
                if (!filter || !filter.sharedMesh) continue;
                Mesh mesh = filter.sharedMesh;
                if (mesh.vertexCount > 400000 || !mesh.isReadable) return false;
                for (int slot = 0; slot < mesh.subMeshCount; slot++)
                    if (mesh.GetTopology(slot) != MeshTopology.Triangles) return false;
                if (!sources.TryGetValue(mesh, out Source source))
                {
                    source = new Source { Vertices = mesh.vertices, Indices = mesh.triangles };
                    sources[mesh] = source;
                }
                if (worldVertices.Count / 3 + source.Indices.Length / 3 > 131072) return false;
                Matrix4x4 world = renderer.transform.localToWorldMatrix;
                foreach (int index in source.Indices) worldVertices.Add(world.MultiplyPoint3x4(source.Vertices[index]));
                if (!hasBounds) { visualBounds = renderer.bounds; hasBounds = true; }
                else visualBounds.Encapsulate(renderer.bounds);
            }
            if (worldVertices.Count == 0) return false;
            var surfaces = new List<float>(worldVertices.Count / 3 * 11);
            for (int i = 0; i < worldVertices.Count; i += 3)
                AddTriangle(surfaces, worldVertices[i], worldVertices[i + 1], worldVertices[i + 2], visualBounds);
            cached = new CachedPiece { Triangles = surfaces.ToArray(), Bounds = visualBounds };
            pieces[piece] = cached;
            }
            if (!cached.Bounds.Intersects(influence)) return true;
            if (triangles.Count / 11 + cached.Triangles.Length / 11 > 131072) return false;
            triangles.AddRange(cached.Triangles);
            return true;
        }
        private static void Add(List<float> values, Vector3 point) { values.Add(point.x); values.Add(point.y); values.Add(point.z); }
        private static void AddTriangle(List<float> values, Vector3 a, Vector3 b, Vector3 c, Bounds pieceBounds)
        {
            Add(values, a); Add(values, b); Add(values, c);
            Vector3 normal = Vector3.Cross(b - a, c - a);
            if (Mathf.Abs(normal.x) >= Mathf.Abs(normal.z)) { values.Add(pieceBounds.min.z); values.Add(pieceBounds.max.z); }
            else { values.Add(pieceBounds.min.x); values.Add(pieceBounds.max.x); }
        }
    }
}
