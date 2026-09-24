using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace LeafClip
{
    // One face of a convex wall contributes a convex perspective shadow. The union
    // of shadows from faces visible to the trunk equals the shadow of the whole box.
    internal sealed class ShadowVolume
    {
        internal readonly Plane[] Planes;
        private ShadowVolume(Plane[] planes) { Planes = planes; }

        internal static IEnumerable<ShadowVolume> FromBox(BoxCollider box, Vector3 origin, float clearance)
        {
            Vector3 scale = box.transform.lossyScale;
            if (Mathf.Abs(scale.x) < 0.001f || Mathf.Abs(scale.y) < 0.001f || Mathf.Abs(scale.z) < 0.001f) yield break;
            Vector3 half = box.size * 0.5f + new Vector3(clearance / Mathf.Abs(scale.x), clearance / Mathf.Abs(scale.y), clearance / Mathf.Abs(scale.z));
            Vector3[] corners = new Vector3[8];
            for (int i = 0; i < 8; i++)
                corners[i] = box.transform.TransformPoint(box.center + new Vector3((i & 1) != 0 ? half.x : -half.x, (i & 2) != 0 ? half.y : -half.y, (i & 4) != 0 ? half.z : -half.z));
            Vector3 center = box.transform.TransformPoint(box.center);
            // A face is specified in winding order, though normals are reoriented below.
            int[,] faces = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
            for (int f = 0; f < 6; f++)
            {
                Vector3[] face = new Vector3[4];
                Vector3 faceCenter = Vector3.zero;
                for (int j = 0; j < 4; j++) { face[j] = corners[faces[f, j]]; faceCenter += face[j] * 0.25f; }
                Vector3 outward = Vector3.Cross(face[1] - face[0], face[2] - face[1]).normalized;
                if (Vector3.Dot(outward, faceCenter - center) < 0f) outward = -outward;
                // Origin inside/at the expanded box has no useful trunk-facing half-space.
                if (Vector3.Dot(outward, origin - faceCenter) <= 0.001f) continue;
                Plane[] planes = new Plane[5];
                planes[0] = new Plane(-outward, faceCenter);
                bool valid = true;
                for (int j = 0; j < 4; j++)
                {
                    Vector3 sideNormal = Vector3.Cross(face[j] - origin, face[(j + 1) % 4] - origin);
                    if (sideNormal.sqrMagnitude < 1e-8f) { valid = false; break; }
                    sideNormal.Normalize();
                    if (Vector3.Dot(sideNormal, faceCenter - origin) < 0f) sideNormal = -sideNormal;
                    planes[j + 1] = new Plane(sideNormal, origin);
                }
                if (valid) yield return new ShadowVolume(planes);
            }
        }
    }

    internal sealed class ShadowClipper
    {
        private readonly Mesh source;
        private readonly List<Vector3> positions, normals;
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

        internal static Mesh Clip(Mesh source, Transform meshTransform, IList<int> foliageSlots, List<ShadowVolume> volumes)
        {
            var clipper = new ShadowClipper(source, meshTransform, volumes);
            foreach (int slot in foliageSlots)
            {
                List<int> input = clipper.indices[slot];
                var output = new List<int>(input.Count);
                for (int i = 0; i < input.Count; i += 3)
                {
                    var fragments = new List<List<int>> { new List<int> { input[i], input[i + 1], input[i + 2] } };
                    foreach (ShadowVolume volume in volumes)
                    {
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
                float da = plane.GetDistanceToPoint(meshTransform.TransformPoint(positions[a]));
                float db = plane.GetDistanceToPoint(meshTransform.TransformPoint(positions[b]));
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
