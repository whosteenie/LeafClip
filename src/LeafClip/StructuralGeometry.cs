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
        private readonly Dictionary<Mesh, Source> sources = new Dictionary<Mesh, Source>();
        internal void Clear() => sources.Clear();
        internal void Snapshot(List<Collider> colliders, out float[] boxes, out float[] triangles, out int visualPieceCount, out int fallbackPieceCount, out int skipped)
        {
            var boxData = new List<float>();
            var triangleData = new List<float>();
            var renderedPieces = new HashSet<Piece>();
            var colliderPieces = new HashSet<Piece>();
            var fallbackPieces = new HashSet<Piece>();
            skipped = 0;
            foreach (Collider collider in colliders)
            {
                if (!collider || !collider.enabled || !collider.gameObject.activeInHierarchy) continue;
                Piece piece = collider.GetComponentInParent<Piece>();
                if (piece && !colliderPieces.Add(piece)) continue;
                if (piece && AppendVisualSurfaces(piece, triangleData)) renderedPieces.Add(piece);
            }
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
                    if (boxData.Count / 18 + triangleData.Count / 9 + source.Indices.Length / 3 > 131072)
                        throw new InvalidOperationException("Obstruction geometry exceeds 131072 primitives; original rendering retained.");
                    Matrix4x4 world = meshCollider.transform.localToWorldMatrix;
                    foreach (int index in source.Indices) Add(triangleData, world.MultiplyPoint3x4(source.Vertices[index]));
                }
                else skipped++;
                // Bound snapshot memory before a pathological modded asset can
                // stall the main thread or allocate an unbounded hierarchy.
                if (boxData.Count / 18 + triangleData.Count / 9 > 131072)
                    throw new InvalidOperationException("Obstruction geometry exceeds 131072 primitives; original rendering retained.");
            }
            boxes = boxData.ToArray(); triangles = triangleData.ToArray();
            visualPieceCount = renderedPieces.Count; fallbackPieceCount = fallbackPieces.Count;
        }
        private bool AppendVisualSurfaces(Piece piece, List<float> triangles)
        {
            var surfaces = new List<float>();
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
                if (triangles.Count / 9 + surfaces.Count / 9 + source.Indices.Length / 3 > 131072) return false;
                Matrix4x4 world = renderer.transform.localToWorldMatrix;
                foreach (int index in source.Indices) Add(surfaces, world.MultiplyPoint3x4(source.Vertices[index]));
            }
            if (surfaces.Count == 0) return false;
            triangles.AddRange(surfaces);
            return true;
        }
        private static void Add(List<float> values, Vector3 point) { values.Add(point.x); values.Add(point.y); values.Add(point.z); }
    }
}
