using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace LeafClip
{
    internal sealed class FoliageSelection
    {
        internal readonly int Slot;
        // Null selects the entire submesh; otherwise one bit per source triangle.
        internal readonly bool[] Triangles;
        internal FoliageSelection(int slot, bool[] triangles) { Slot = slot; Triangles = triangles; }
    }

    // Cast each wall footprint edge horizontally from the trunk axis. Fir foliage
    // grows outward at branch height, so a ray from the ground-level root misses
    // foliage behind upper walls. The wall's real vertical span caps each shadow.
    internal sealed class ShadowVolume
    {
        internal readonly Plane[] Planes;
        private ShadowVolume(Plane[] planes) { Planes = planes; }

        // An AABB entirely outside any clipping plane cannot contain foliage
        // affected by this face. This only rejects definite misses.
        internal bool Intersects(Bounds bounds)
        {
            Vector3 center = bounds.center, extents = bounds.extents;
            foreach (Plane plane in Planes)
            {
                Vector3 normal = plane.normal;
                float reach = Mathf.Abs(normal.x) * extents.x + Mathf.Abs(normal.y) * extents.y + Mathf.Abs(normal.z) * extents.z;
                if (plane.GetDistanceToPoint(center) + reach < -0.0001f) return false;
            }
            return true;
        }

        // A triangle is convex: when all three vertices lie outside one plane,
        // neither it nor any fragment of it can meet this shadow volume.
        internal bool MayIntersectTriangle(Vector3 a, Vector3 b, Vector3 c)
        {
            // Height caps reject most unrelated pieces in tall, dense bases.
            for (int i = Planes.Length - 2; i < Planes.Length; i++)
            {
                Plane plane = Planes[i];
                if (plane.GetDistanceToPoint(a) < -0.0001f &&
                    plane.GetDistanceToPoint(b) < -0.0001f &&
                    plane.GetDistanceToPoint(c) < -0.0001f) return false;
            }
            for (int i = 0; i < Planes.Length - 2; i++)
            {
                Plane plane = Planes[i];
                if (plane.GetDistanceToPoint(a) < -0.0001f &&
                    plane.GetDistanceToPoint(b) < -0.0001f &&
                    plane.GetDistanceToPoint(c) < -0.0001f) return false;
            }
            return true;
        }

        internal static IEnumerable<ShadowVolume> FromBox(BoxCollider box, Vector3 origin, float clearance)
        {
            Vector3 scale = box.transform.lossyScale;
            if (Mathf.Abs(scale.x) < 0.001f || Mathf.Abs(scale.y) < 0.001f || Mathf.Abs(scale.z) < 0.001f) yield break;
            Vector3 half = box.size * 0.5f;
            Vector3[] corners = new Vector3[8];
            for (int i = 0; i < 8; i++)
                corners[i] = box.transform.TransformPoint(box.center + new Vector3((i & 1) != 0 ? half.x : -half.x, (i & 2) != 0 ? half.y : -half.y, (i & 4) != 0 ? half.z : -half.z));
            float bottom = corners[0].y, top = corners[0].y;
            var projected = new List<Vector2>(8);
            foreach (Vector3 corner in corners)
            {
                projected.Add(new Vector2(corner.x, corner.z));
                bottom = Mathf.Min(bottom, corner.y);
                top = Mathf.Max(top, corner.y);
            }
            List<Vector2> hull = ConvexHull(projected);
            if (hull.Count < 3) yield break;
            Vector2 trunk = new Vector2(origin.x, origin.z);
            for (int i = 0; i < hull.Count; i++)
            {
                Vector2 a = hull[i], b = hull[(i + 1) % hull.Count];
                Vector2 edge = b - a;
                Vector2 outward = new Vector2(edge.y, -edge.x).normalized;
                Vector2 middle = (a + b) * 0.5f;
                if (Vector2.Dot(outward, trunk - middle) <= 0.001f) continue;
                if ((a - trunk).sqrMagnitude < 1e-8f || (b - trunk).sqrMagnitude < 1e-8f) continue;
                Vector2 inward = -outward;
                Vector2 sideA = SideNormal(trunk, a, middle), sideB = SideNormal(trunk, b, middle);
                Plane[] planes = new Plane[5];
                Vector2 entry = a + outward * clearance;
                planes[0] = new Plane(new Vector3(inward.x, 0f, inward.y), new Vector3(entry.x, 0f, entry.y));
                planes[1] = new Plane(new Vector3(sideA.x, 0f, sideA.y), origin);
                planes[2] = new Plane(new Vector3(sideB.x, 0f, sideB.y), origin);
                planes[3] = new Plane(Vector3.up, new Vector3(0f, bottom, 0f));
                planes[4] = new Plane(Vector3.down, new Vector3(0f, top, 0f));
                yield return new ShadowVolume(planes);
            }
        }

        private static Vector2 SideNormal(Vector2 origin, Vector2 endpoint, Vector2 inside)
        {
            Vector2 ray = endpoint - origin;
            Vector2 normal = new Vector2(-ray.y, ray.x).normalized;
            return Vector2.Dot(normal, inside - origin) >= 0f ? normal : -normal;
        }

        private static float Cross(Vector2 a, Vector2 b) { return a.x * b.y - a.y * b.x; }

        private static List<Vector2> ConvexHull(List<Vector2> points)
        {
            points.Sort((a, b) => a.x == b.x ? a.y.CompareTo(b.y) : a.x.CompareTo(b.x));
            var unique = new List<Vector2>(points.Count);
            foreach (Vector2 point in points)
                if (unique.Count == 0 || (point - unique[unique.Count - 1]).sqrMagnitude > 1e-10f) unique.Add(point);
            if (unique.Count < 3) return unique;
            var hull = new List<Vector2>(unique.Count * 2);
            foreach (Vector2 point in unique)
            {
                while (hull.Count >= 2 && Cross(hull[hull.Count - 1] - hull[hull.Count - 2], point - hull[hull.Count - 1]) <= 1e-8f)
                    hull.RemoveAt(hull.Count - 1);
                hull.Add(point);
            }
            int lowerCount = hull.Count;
            for (int i = unique.Count - 2; i >= 0; i--)
            {
                Vector2 point = unique[i];
                while (hull.Count > lowerCount && Cross(hull[hull.Count - 1] - hull[hull.Count - 2], point - hull[hull.Count - 1]) <= 1e-8f)
                    hull.RemoveAt(hull.Count - 1);
                hull.Add(point);
            }
            hull.RemoveAt(hull.Count - 1);
            return hull;
        }
    }

    internal sealed class ShadowClipper
    {
        private readonly Mesh source;
        private readonly List<Vector3> positions, worldPositions, normals;
        private readonly List<Vector4> tangents;
        private readonly List<Vector4>[] uvs = new List<Vector4>[8];
        private readonly int[] uvDimensions = new int[8];
        private readonly List<Color> colors;
        private readonly List<int>[] indices;
        private readonly List<ShadowVolume> volumes;
        private readonly Transform meshTransform;
        private bool changed;

        private ShadowClipper(Mesh source, Transform meshTransform, List<ShadowVolume> volumes)
        {
            this.source = source;
            this.meshTransform = meshTransform;
            this.volumes = volumes;
            positions = new List<Vector3>(source.vertices);
            worldPositions = new List<Vector3>(positions.Count);
            foreach (Vector3 position in positions) worldPositions.Add(meshTransform.TransformPoint(position));
            normals = source.HasVertexAttribute(VertexAttribute.Normal) ? new List<Vector3>(source.normals) : null;
            tangents = source.HasVertexAttribute(VertexAttribute.Tangent) ? new List<Vector4>(source.tangents) : null;
            colors = source.HasVertexAttribute(VertexAttribute.Color) ? new List<Color>(source.colors) : null;
            for (int i = 0; i < 8; i++)
            {
                VertexAttribute attribute = (VertexAttribute)((int)VertexAttribute.TexCoord0 + i);
                if (!source.HasVertexAttribute(attribute)) continue;
                uvDimensions[i] = source.GetVertexAttributeDimension(attribute);
                uvs[i] = new List<Vector4>();
                source.GetUVs(i, uvs[i]);
            }
            indices = new List<int>[source.subMeshCount];
            for (int i = 0; i < indices.Length; i++) indices[i] = new List<int>(source.GetIndices(i));
        }

        internal static bool Supports(Mesh mesh, out string reason)
        {
            reason = null;
            if (!mesh.isReadable) { reason = "unreadable"; return false; }
            if (mesh.blendShapeCount != 0 || mesh.bindposeCount != 0 || mesh.HasVertexAttribute(VertexAttribute.BlendWeight) || mesh.HasVertexAttribute(VertexAttribute.BlendIndices)) { reason = "skinned or blend-shape streams"; return false; }
            foreach (VertexAttributeDescriptor a in mesh.GetVertexAttributes())
            {
                bool supported = a.attribute == VertexAttribute.Position && a.dimension == 3 ||
                                 a.attribute == VertexAttribute.Normal && a.dimension == 3 ||
                                 a.attribute == VertexAttribute.Tangent && a.dimension == 4 ||
                                 a.attribute == VertexAttribute.Color && a.dimension == 4 ||
                                 a.attribute >= VertexAttribute.TexCoord0 && a.attribute <= VertexAttribute.TexCoord7 && a.dimension >= 2 && a.dimension <= 4;
                if (!supported || a.format != VertexAttributeFormat.Float32) { reason = "unsupported attribute " + a; return false; }
            }
            for (int i = 0; i < mesh.subMeshCount; i++) if (mesh.GetTopology(i) != MeshTopology.Triangles) { reason = "non-triangle submesh"; return false; }
            return true;
        }

        internal static Mesh Clip(Mesh source, Transform meshTransform, IList<FoliageSelection> foliageSlots, List<ShadowVolume> volumes)
        {
            var clipper = new ShadowClipper(source, meshTransform, volumes);
            foreach (FoliageSelection selection in foliageSlots)
            {
                int slot = selection.Slot;
                List<int> input = clipper.indices[slot];
                if (selection.Triangles != null && selection.Triangles.Length != input.Count / 3)
                    throw new InvalidOperationException("Foliage triangle selection no longer matches the source mesh.");
                var output = new List<int>(input.Count);
                for (int i = 0; i < input.Count; i += 3)
                {
                    if (selection.Triangles != null && !selection.Triangles[i / 3])
                    {
                        output.Add(input[i]); output.Add(input[i + 1]); output.Add(input[i + 2]);
                        continue;
                    }
                    var fragments = new List<List<int>> { new List<int> { input[i], input[i + 1], input[i + 2] } };
                    Vector3 originalA = clipper.worldPositions[input[i]], originalB = clipper.worldPositions[input[i + 1]], originalC = clipper.worldPositions[input[i + 2]];
                    foreach (ShadowVolume volume in volumes)
                    {
                        if (!volume.MayIntersectTriangle(originalA, originalB, originalC)) continue;
                        var next = new List<List<int>>();
                        foreach (List<int> fragment in fragments) clipper.Subtract(fragment, volume, next);
                        fragments = next;
                        if (clipper.positions.Count > 100000 || fragments.Count > 10000) throw new InvalidOperationException("Clipped mesh exceeds prototype geometry limit.");
                        if (fragments.Count == 0) break;
                    }
                    foreach (List<int> fragment in fragments)
                        for (int k = 1; k + 1 < fragment.Count; k++)
                        {
                            Vector3 a = clipper.positions[fragment[0]], b = clipper.positions[fragment[k]], c = clipper.positions[fragment[k + 1]];
                            if (Vector3.Cross(b - a, c - a).sqrMagnitude < 1e-12f) continue;
                            output.Add(fragment[0]); output.Add(fragment[k]); output.Add(fragment[k + 1]);
                        }
                    if (fragments.Count != 1 || fragments[0].Count != 3 || fragments[0][0] != input[i] || fragments[0][1] != input[i + 1] || fragments[0][2] != input[i + 2]) clipper.changed = true;
                }
                clipper.indices[slot] = output;
            }
            if (!clipper.changed) return null;
            Mesh result = UnityEngine.Object.Instantiate(source);
            try
            {
                result.name = "LeafClip instance: " + source.name;
                result.indexFormat = clipper.positions.Count > 65535 ? IndexFormat.UInt32 : source.indexFormat;
                result.SetVertices(clipper.positions);
                if (clipper.normals != null) result.SetNormals(clipper.normals);
                if (clipper.tangents != null) result.SetTangents(clipper.tangents);
                if (clipper.colors != null) result.SetColors(clipper.colors);
                for (int i = 0; i < 8; i++)
                {
                    if (clipper.uvs[i] == null) continue;
                    if (clipper.uvDimensions[i] == 2)
                    {
                        var data = new List<Vector2>(clipper.uvs[i].Count);
                        foreach (Vector4 v in clipper.uvs[i]) data.Add(new Vector2(v.x, v.y));
                        result.SetUVs(i, data);
                    }
                    else if (clipper.uvDimensions[i] == 3)
                    {
                        var data = new List<Vector3>(clipper.uvs[i].Count);
                        foreach (Vector4 v in clipper.uvs[i]) data.Add(new Vector3(v.x, v.y, v.z));
                        result.SetUVs(i, data);
                    }
                    else result.SetUVs(i, clipper.uvs[i]);
                }
                for (int i = 0; i < clipper.indices.Length; i++) result.SetTriangles(clipper.indices[i], i, false);
                result.RecalculateBounds();
                return result;
            }
            catch { UnityEngine.Object.Destroy(result); throw; }
        }

        private void Subtract(List<int> polygon, ShadowVolume volume, List<List<int>> keep)
        {
            bool whollyInside = true;
            foreach (Plane plane in volume.Planes)
            {
                bool anyInside = false;
                foreach (int vertex in polygon)
                {
                    bool vertexInside = plane.GetDistanceToPoint(worldPositions[vertex]) >= -1e-4f;
                    anyInside |= vertexInside;
                    whollyInside &= vertexInside;
                }
                if (!anyInside) { keep.Add(polygon); return; }
            }
            if (whollyInside) return;
            List<int> inside = polygon;
            foreach (Plane plane in volume.Planes)
            {
                if (inside.Count < 3) return;
                Split(inside, plane, out List<int> positive, out List<int> negative);
                if (negative.Count >= 3) keep.Add(negative);
                inside = positive;
            }
            // Remaining polygon is behind the wall and is discarded.
        }

        private void Split(List<int> polygon, Plane plane, out List<int> positive, out List<int> negative)
        {
            positive = new List<int>(); negative = new List<int>();
            for (int i = 0; i < polygon.Count; i++)
            {
                int a = polygon[i], b = polygon[(i + 1) % polygon.Count];
                float da = plane.GetDistanceToPoint(worldPositions[a]);
                float db = plane.GetDistanceToPoint(worldPositions[b]);
                if (da >= -1e-4f) positive.Add(a);
                if (da < -1e-4f) negative.Add(a);
                if ((da < -1e-4f && db >= -1e-4f) || (da >= -1e-4f && db < -1e-4f))
                {
                    int cut = AddInterpolated(a, b, da / (da - db));
                    positive.Add(cut); negative.Add(cut);
                }
            }
            RemoveAdjacentDuplicates(positive);
            RemoveAdjacentDuplicates(negative);
        }

        private static void RemoveAdjacentDuplicates(List<int> polygon)
        {
            for (int i = polygon.Count - 1; i > 0; i--)
                if (polygon[i] == polygon[i - 1]) polygon.RemoveAt(i);
            if (polygon.Count > 1 && polygon[0] == polygon[polygon.Count - 1]) polygon.RemoveAt(polygon.Count - 1);
        }

        private int AddInterpolated(int a, int b, float t)
        {
            int index = positions.Count;
            positions.Add(Vector3.LerpUnclamped(positions[a], positions[b], t));
            worldPositions.Add(Vector3.LerpUnclamped(worldPositions[a], worldPositions[b], t));
            if (normals != null) normals.Add(Vector3.LerpUnclamped(normals[a], normals[b], t).normalized);
            if (tangents != null)
            {
                Vector4 v = Vector4.LerpUnclamped(tangents[a], tangents[b], t);
                Vector3 direction = new Vector3(v.x, v.y, v.z).normalized;
                tangents.Add(new Vector4(direction.x, direction.y, direction.z, tangents[a].w == tangents[b].w ? tangents[a].w : (t < 0.5f ? tangents[a].w : tangents[b].w)));
            }
            if (colors != null) colors.Add(Color.LerpUnclamped(colors[a], colors[b], t));
            for (int i = 0; i < 8; i++) if (uvs[i] != null) uvs[i].Add(Vector4.LerpUnclamped(uvs[i][a], uvs[i][b], t));
            return index;
        }
    }
}
