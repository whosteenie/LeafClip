using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using BepInEx.Configuration;
using UnityEngine;
using UnityEngine.Rendering;

namespace LeafClip
{
    // Post-wind clipping. Original topology/vertex streams are
    // cloned only to obtain private draw identities; no vertices are cut or moved.
    internal sealed class GpuClipPrototype : IDisposable
    {
        private sealed class MeshEntry
        {
            internal MeshFilter Filter;
            internal MeshRenderer Renderer;
            internal Mesh Original, Private;
            internal Material[] OriginalMaterials, PrivateMaterials;
            internal IntPtr Buffer;
        }
        private sealed class TreeEntry
        {
            internal TreeBase Tree;
            internal Bounds Bounds;
            internal readonly List<MeshEntry> Meshes = new List<MeshEntry>();
            internal readonly List<Collider> Colliders = new List<Collider>();
            internal bool Dirty = true;
            internal int Signature;
            internal bool HasSignature;
            internal Task<ulong> Pending;
            internal int PendingSignature;
            internal int Revision, PendingRevision;
            internal float[] PendingOrigin;
        }
        private sealed class CameraScope
        {
            internal CameraEvent BeginAt, EndAt;
            internal CommandBuffer Begin, End;
            internal bool RenderToTexture;
        }
        internal static GpuClipPrototype Current;
        private readonly Plugin plugin;
        private readonly ConfigEntry<float> distance, boundsPadding;
        private readonly HashSet<TreeBase> known = new HashSet<TreeBase>();
        private readonly List<TreeEntry> active = new List<TreeEntry>();
        private readonly Queue<TreeBase> captures = new Queue<TreeBase>();
        private readonly Dictionary<Mesh, uint[]> masks = new Dictionary<Mesh, uint[]>();
        private readonly StructuralGeometry geometry = new StructuralGeometry();
        private readonly HashSet<int> unsupported = new HashSet<int>();
        private NativeClipBridge native;
        private Texture2D marker;
        private bool initialized;
        private bool probing12;
        private float nextSelection, nextGeometry, nextReport;
        private ZNetScene scene;
        private Camera camera;
        private readonly List<CameraScope> cameraScopes = new List<CameraScope>();
        private readonly HashSet<int> loggedCameras = new HashSet<int>();
        private bool cameraDeferred;
        private readonly StringBuilder message = new StringBuilder(2048);
        internal bool Active => initialized;

        internal GpuClipPrototype(Plugin plugin)
        {
            this.plugin = plugin;
            distance = plugin.Config.Bind("GpuClip", "MaxTreeDistance", 64f, new ConfigDescription("Process trees this close to the player when they intersect player-built structures.", new AcceptableValueRange<float>(10f, 150f)));
            boundsPadding = plugin.Config.Bind("GpuClip", "WindBoundsPadding", 3f, new ConfigDescription("Broad-phase/culling envelope only; never expands the actual clipping boundary.", new AcceptableValueRange<float>(0f, 20f)));
            Current = this;
            Camera.onPreCull += BeforeCamera;
            Initialize();
        }
        private void Initialize()
        {
            try
            {
                if (IntPtr.Size != 8 || Application.platform != RuntimePlatform.WindowsPlayer)
                    throw new NotSupportedException("LeafClip currently requires Windows x64.");
                GraphicsDeviceType backend = SystemInfo.graphicsDeviceType;
                if (backend != GraphicsDeviceType.Direct3D11 && backend != GraphicsDeviceType.Direct3D12)
                    throw new NotSupportedException("LeafClip renderer unsupported: " + backend);
                native = new NativeClipBridge();
                marker = new Texture2D(1, 1, TextureFormat.RGBA32, false);
                marker.Apply();
                if (backend == GraphicsDeviceType.Direct3D12)
                {
                    probing12 = native.Initialize12(marker.GetNativeTexturePtr()) != 0;
                    if (!probing12) throw new InvalidOperationException("D3D12 initialization failed; inspect native messages.");
                    initialized = native.Enable12(1) != 0;
                    if (!initialized) throw new InvalidOperationException("D3D12 clipping activation failed; inspect native messages.");
                    plugin.LogClip("D3D12 clipping active using the vegetation shader's animated world position.");
                    return;
                }
                initialized = native.Initialize(marker.GetNativeTexturePtr()) != 0;
                if (!initialized) throw new InvalidOperationException("Native initialization failed; inspect native messages.");
                plugin.LogClip("D3D11 clipping active. Existing Prototype/GpuClip Enabled, MaxTrees and TreeNameContains settings are ignored.");
            }
            catch (Exception ex) { plugin.LogClip("LeafClip renderer unavailable: " + ex.Message); }
        }
        internal void Register(TreeBase tree)
        {
            if (tree && tree.gameObject.scene.IsValid() && tree.gameObject.scene.isLoaded) known.Add(tree);
        }
        internal void Notify(Piece piece)
        {
            if (!Active || !StructuralPieces.Includes(piece)) return;
            foreach (TreeEntry tree in active)
                foreach (Collider collider in piece.GetComponentsInChildren<Collider>())
                    if (collider && tree.Bounds.Intersects(collider.bounds)) { tree.Dirty = true; break; }
        }
        internal void Update()
        {
            if (native != null)
                while (native.PollLog(message, message.Capacity) > 0) plugin.LogClip("GpuClip native: " + message);
            if (probing12 && Time.realtimeSinceStartup >= nextReport)
            {
                nextReport = Time.realtimeSinceStartup + 15f;
                var result = new ulong[29]; native.GetStats12(result, result.Length);
                plugin.LogClip($"D3D12: graphicsPSOs={result[0]}, DXBCPixelStages={result[1]}, rootSignatures={result[2]}, indexedDraws={result[4]}, extendedRoots={result[5]}, capturedPSOs={result[6]}, matchedPrivateDraws={result[7]}, preparedPSOs={result[8]}, rejectedPSOs={result[9]}, queueSubmissions={result[10]}, retiredDraws={result[11]}, clippedDraws={result[12]}, failedDraws={result[13]}, privateDraws={result[14]}, outsideScope={result[15]}, scopedWithoutGeometry={result[17]}, viewport2048Outside={result[18]}, otherViewportOutside={result[19]}, multiInstance={result[20]}, frameFallbackClipped={result[22]}, fallbackAspectRejected={result[23]}, fallbackDepthRejected={result[24]}, uncapturedPSO={result[25]}, uncapturedRoot={result[26]}, rootMismatch={result[27]}, pipelineNotReady={result[28]}.");
            }
            if (!Active) return;
            if (scene != ZNetScene.instance)
            {
                RestoreAll(); captures.Clear(); known.Clear(); unsupported.Clear(); masks.Clear(); geometry.Clear(); scene = ZNetScene.instance;
                if (scene) foreach (TreeBase tree in UnityEngine.Object.FindObjectsByType<TreeBase>(FindObjectsInactive.Exclude, FindObjectsSortMode.None)) Register(tree);
                nextSelection = 0f;
            }
            if (!scene || !Player.m_localPlayer) return;
            if (Time.realtimeSinceStartup >= nextSelection)
            {
                nextSelection = Time.realtimeSinceStartup + 5f;
                SelectTrees();
            }
            for (int i = 0; i < 2 && captures.Count > 0; i++) CaptureNextTree();
            if (Time.realtimeSinceStartup >= nextGeometry)
            {
                nextGeometry = Time.realtimeSinceStartup + 0.25f;
                foreach (TreeEntry tree in active)
                {
                    try { UpdateBoxes(tree); }
                    catch (Exception ex) { plugin.LogClip("GpuClip obstruction update failed; retrying when geometry changes: " + ex.Message); ClearBoxes(tree); }
                }
            }
            if (!probing12 && Time.realtimeSinceStartup >= nextReport)
            {
                nextReport = Time.realtimeSinceStartup + 15f;
                var stats = new ulong[8]; native.GetStats(stats, stats.Length);
                plugin.LogClip($"GpuClip trees={active.Count}, shadersCaptured={stats[0]}, patched={stats[1]}, rejected={stats[2]}, matchedDraws={stats[3]}, clippedDraws={stats[4]}, uncapturedDraws={stats[5]}, unsupportedDraws={stats[6]}, errors={stats[7]}.");
            }
        }
        private void SelectTrees()
        {
            captures.Clear();
            known.RemoveWhere(tree => !tree);
            var candidates = new List<TreeBase>();
            foreach (TreeBase tree in known)
                if (tree.gameObject.activeInHierarchy && !unsupported.Contains(tree.GetInstanceID()) &&
                    (tree.transform.position - Player.m_localPlayer.transform.position).sqrMagnitude <= distance.Value * distance.Value &&
                    HasStructureNearby(tree))
                    candidates.Add(tree);
            candidates.Sort((a, b) => (a.transform.position - Player.m_localPlayer.transform.position).sqrMagnitude.CompareTo((b.transform.position - Player.m_localPlayer.transform.position).sqrMagnitude));
            for (int i = active.Count - 1; i >= 0; i--)
            {
                TreeEntry entry = active[i];
                int rank = entry.Tree ? candidates.IndexOf(entry.Tree) : -1;
                bool changed = entry.Meshes.Exists(m => !m.Filter || m.Filter.sharedMesh != m.Private);
                if (rank < 0 || changed) { Restore(entry); active.RemoveAt(i); }
            }
            foreach (TreeBase tree in candidates)
            {
                if (!active.Exists(item => item.Tree == tree)) captures.Enqueue(tree);
            }
        }
        private void CaptureNextTree()
        {
                TreeBase tree = captures.Dequeue();
                if (!tree || active.Exists(item => item.Tree == tree) || !HasStructureNearby(tree)) return;
                TreeEntry entry = null;
                try
                {
                    entry = Capture(tree);
                    if (entry.Meshes.Count == 0) { unsupported.Add(tree.GetInstanceID()); return; }
                    active.Add(entry);
                    plugin.LogClip($"GpuClip target {tree.name} id={tree.GetInstanceID()}, meshes={entry.Meshes.Count}. Original vertex data and triangle topology retained.");
                }
                catch (Exception ex) { if (entry != null) Restore(entry); unsupported.Add(tree.GetInstanceID()); plugin.LogClip("GpuClip target failed: " + ex.Message); }
        }
        private bool HasStructureNearby(TreeBase tree)
        {
            Bounds bounds = new Bounds(tree.transform.position, Vector3.zero);
            foreach (MeshRenderer renderer in tree.GetComponentsInChildren<MeshRenderer>(true))
                if (renderer && renderer.GetComponent<MeshFilter>()) bounds.Encapsulate(renderer.bounds);
            bounds.Expand(2f * boundsPadding.Value);
            int layers = LayerMask.GetMask("piece", "piece_nonsolid");
            foreach (Collider hit in Physics.OverlapBox(bounds.center, bounds.extents, Quaternion.identity, layers, QueryTriggerInteraction.Ignore))
                if (hit && StructuralPieces.Includes(hit.GetComponentInParent<Piece>())) return true;
            return false;
        }
        private uint[] Eligibility(Mesh mesh, MeshRenderer renderer)
        {
            // Materials are part of classification; the same mesh with different
            // materials must not reuse another renderer's material classification.
            var materials = renderer.sharedMaterials;
            int count = 0;
            for (int slot = 0; slot < mesh.subMeshCount; slot++) count = Math.Max(count, checked((int)(mesh.GetIndexStart(slot) + mesh.GetIndexCount(slot))));
            if (count % 3 != 0) return null;
            var mask = new uint[count / 3]; bool selected = false;
            for (int slot = 0; slot < mesh.subMeshCount && slot < materials.Length; slot++)
            {
                Material material = materials[slot];
                if (!material || !material.shader || material.shader.name.IndexOf("Vegetation", StringComparison.OrdinalIgnoreCase) < 0) continue;
                string name = material.name.ToLowerInvariant();
                if (!name.Contains("leaf") && !name.Contains("leaves") && !name.Contains("foliage")) continue;
                int start = checked((int)mesh.GetIndexStart(slot));
                int length = checked((int)mesh.GetIndexCount(slot));
                if (start % 3 != 0 || length % 3 != 0) return null;
                for (int i = start / 3; i < (start + length) / 3; i++) mask[i] = 1;
                selected = true;
            }
            if (selected) return mask;
            if (mesh.subMeshCount != 1 || materials.Length != 1 || !materials[0] || !materials[0].shader ||
                materials[0].shader.name.IndexOf("Vegetation", StringComparison.OrdinalIgnoreCase) < 0) return null;
            if (!masks.TryGetValue(mesh, out mask))
            {
                mask = null;
                if (TopologyFoliage.TryClassify(mesh, out bool[] canopy))
                {
                    mask = new uint[canopy.Length]; for (int i = 0; i < mask.Length; i++) mask[i] = canopy[i] ? 1u : 0u;
                }
                masks[mesh] = mask;
            }
            return mask;
        }
        private TreeEntry Capture(TreeBase tree)
        {
            var result = new TreeEntry { Tree = tree, Bounds = new Bounds(tree.transform.position, Vector3.zero) };
            try
            {
                foreach (MeshRenderer renderer in tree.GetComponentsInChildren<MeshRenderer>(true))
                {
                    bool temporary = false;
                    for (Transform t = renderer.transform; t && t != tree.transform; t = t.parent)
                        if (t.name.EndsWith("(Clone)", StringComparison.Ordinal)) temporary = true;
                    MeshFilter meshFilter = renderer.GetComponent<MeshFilter>();
                    if (temporary || !meshFilter || !meshFilter.sharedMesh || renderer.isPartOfStaticBatch) continue;
                    Mesh source = meshFilter.sharedMesh;
                    if (!source.isReadable || source.vertexCount > 20000 || source.blendShapeCount != 0 || source.bindposeCount != 0) continue;
                    bool triangles = true;
                    for (int i = 0; i < source.subMeshCount; i++) triangles &= source.GetTopology(i) == MeshTopology.Triangles;
                    if (!triangles) continue;
                    uint[] mask = Eligibility(source, renderer); if (mask == null) continue;
                    var entry = new MeshEntry { Filter = meshFilter, Renderer = renderer, Original = source, OriginalMaterials = renderer.sharedMaterials };
                    result.Meshes.Add(entry);
                    entry.Private = UnityEngine.Object.Instantiate(source);
                    entry.Private.name = "LeafClip GPU identity: " + source.name;
                    Bounds bounds = source.bounds;
                    // Local padding compensates for nonuniform object scale. This
                    // affects frustum culling only, not the cut position.
                    Vector3 scale = renderer.transform.lossyScale;
                    bounds.Expand(new Vector3(2f * boundsPadding.Value / Mathf.Max(0.001f, Mathf.Abs(scale.x)),
                        2f * boundsPadding.Value / Mathf.Max(0.001f, Mathf.Abs(scale.y)), 2f * boundsPadding.Value / Mathf.Max(0.001f, Mathf.Abs(scale.z))));
                    entry.Private.bounds = bounds;
                    entry.PrivateMaterials = new Material[entry.OriginalMaterials.Length];
                    for (int i = 0; i < entry.PrivateMaterials.Length; i++)
                    {
                        if (!entry.OriginalMaterials[i]) continue;
                        entry.PrivateMaterials[i] = new Material(entry.OriginalMaterials[i]) { enableInstancing = false };
                    }
                    entry.Private.UploadMeshData(false);
                    entry.Buffer = entry.Private.GetNativeIndexBufferPtr();
                    if (native.Register(entry.Buffer, mask, mask.Length) == 0) throw new InvalidOperationException("native mesh registration failed");
                    meshFilter.sharedMesh = entry.Private;
                    renderer.sharedMaterials = entry.PrivateMaterials;
                    for (int i = 0; i < 8; i++) result.Bounds.Encapsulate(renderer.transform.TransformPoint(bounds.center + new Vector3(
                        (i & 1) == 0 ? -bounds.extents.x : bounds.extents.x,
                        (i & 2) == 0 ? -bounds.extents.y : bounds.extents.y,
                        (i & 4) == 0 ? -bounds.extents.z : bounds.extents.z)));
                }
                return result;
            }
            catch { Restore(result); throw; }
        }
        private void UpdateBoxes(TreeEntry tree)
        {
            if (!tree.Tree) return;
            if (tree.Dirty)
            {
                tree.Colliders.Clear();
                int layers = LayerMask.GetMask("piece", "piece_nonsolid");
                foreach (Collider hit in Physics.OverlapBox(tree.Bounds.center, tree.Bounds.extents, Quaternion.identity, layers, QueryTriggerInteraction.Ignore))
                    if (hit && hit.enabled && StructuralPieces.Includes(hit.GetComponentInParent<Piece>())) tree.Colliders.Add(hit);
                tree.Colliders.Sort((a, b) => a.GetInstanceID().CompareTo(b.GetInstanceID()));
                tree.Dirty = false; tree.HasSignature = false; tree.Revision++;
            }
            // Small transform checks catch moving pieces without repeating mesh
            // extraction or physics queries for unchanged buildings.
            int signature = tree.Tree.transform.position.GetHashCode();
            unchecked
            {
                foreach (Collider collider in tree.Colliders)
                {
                    if (!collider || !collider.enabled || !collider.gameObject.activeInHierarchy) continue;
                    signature = signature * 31 + collider.GetInstanceID();
                    signature = signature * 31 + collider.transform.localToWorldMatrix.GetHashCode();
                    if (collider is BoxCollider box) { signature = signature * 31 + box.center.GetHashCode(); signature = signature * 31 + box.size.GetHashCode(); }
                    if (collider is MeshCollider mesh) signature = signature * 31 + (mesh.sharedMesh ? mesh.sharedMesh.GetInstanceID() : 0);
                }
            }
            if (tree.Pending != null)
            {
                if (!tree.Pending.IsCompleted) return;
                Task<ulong> completed = tree.Pending; tree.Pending = null;
                tree.Signature = signature; tree.HasSignature = true;
                ulong handle = completed.GetAwaiter().GetResult();
                try
                {
                    if (signature == tree.PendingSignature && tree.Revision == tree.PendingRevision && handle != 0)
                    {
                        foreach (MeshEntry mesh in tree.Meshes)
                            if (native.BindGeometry(mesh.Buffer, tree.PendingOrigin, handle) == 0) throw new InvalidOperationException("native geometry binding failed");
                        tree.Signature = signature; tree.HasSignature = true;
                    }
                    else if (handle != 0) tree.HasSignature = false;
                }
                finally { if (handle != 0) native.ReleaseGeometry(handle); }
                if (handle == 0) throw new InvalidOperationException("native geometry build failed; inspect native log");
            }
            if (tree.HasSignature && tree.Signature == signature) return;
            // Stop using old geometry immediately when a piece changes. A short
            // uncropped warmup is preferable to an obsolete cut after demolition.
            ClearBoxes(tree);
            tree.Signature = signature; tree.HasSignature = true;
            geometry.Snapshot(tree.Colliders, out float[] boxes, out float[] triangles, out int visualPieces, out int fallbackPieces, out int skipped);
            Vector3 origin = tree.Tree.transform.position;
            tree.PendingOrigin = new[] { origin.x, origin.y, origin.z };
            tree.PendingSignature = signature;
            tree.PendingRevision = tree.Revision;
            tree.Pending = Task.Run(() => native.CreateGeometry(boxes, boxes.Length / 18, triangles, triangles.Length / 9));
            plugin.LogClip($"GpuClip {tree.Tree.name}: {visualPieces} pieces use visible mesh surfaces, {fallbackPieces} use colliders; queued {boxes.Length / 18} boxes, {triangles.Length / 9} triangles, {skipped} unsupported colliders across {tree.Meshes.Count} LOD meshes.");
        }
        private void ClearBoxes(TreeEntry tree)
        {
            foreach (MeshEntry mesh in tree.Meshes) native.BindGeometry(mesh.Buffer, new float[3], 0);
        }
        private void BeforeCamera(Camera current)
        {
            if (probing12 && Active && current && active.Count > 0 && loggedCameras.Count < 16 && loggedCameras.Add(current.GetInstanceID()))
                plugin.LogClip($"D3D12 camera: name={current.name}, id={current.GetInstanceID()}, main={current == Camera.main}, type={current.cameraType}, viewport={current.pixelWidth}x{current.pixelHeight}, targetTexture={(current.targetTexture ? current.targetTexture.name : "none")}, path={current.actualRenderingPath}, cullingMask={current.cullingMask}.");
            if (!Active || current != Camera.main || active.Count == 0) { if (current == camera) DetachCamera(); return; }
            // Depth textures and camera color need the same cut. Unity can use
            // distinct projection flips for the depth target and backbuffer.
            bool deferred = current.actualRenderingPath == RenderingPath.DeferredShading;
            if (camera != current || cameraDeferred != deferred)
            {
                DetachCamera(); camera = current; cameraDeferred = deferred;
                AddScope(CameraEvent.BeforeDepthTexture, CameraEvent.AfterDepthTexture, true);
                AddScope(CameraEvent.BeforeDepthNormalsTexture, CameraEvent.AfterDepthNormalsTexture, true);
                if (deferred) AddScope(CameraEvent.BeforeGBuffer, CameraEvent.AfterGBuffer, true);
                AddScope(CameraEvent.BeforeForwardOpaque, CameraEvent.AfterForwardOpaque, current.targetTexture != null);
                AddScope(CameraEvent.BeforeForwardAlpha, CameraEvent.AfterForwardAlpha, current.targetTexture != null);
            }
            float[] backbuffer = CameraInverse(current, false);
            float[] texture = CameraInverse(current, true);
            if (probing12 && native.SetFallbackCamera12(backbuffer, texture, current.pixelWidth, current.pixelHeight) == 0)
                plugin.LogClip("D3D12 fallback camera update failed.");
            foreach (CameraScope scope in cameraScopes)
            {
                int token = native.QueueCamera(scope.RenderToTexture ? texture : backbuffer);
                scope.Begin.Clear(); scope.Begin.IssuePluginEvent(native.GetRenderEvent(), token);
            }
        }
        private static float[] CameraInverse(Camera current, bool renderToTexture)
        {
            Matrix4x4 inverse = (GL.GetGPUProjectionMatrix(current.projectionMatrix, renderToTexture) * current.worldToCameraMatrix).inverse;
            var values = new float[16]; for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) values[r * 4 + c] = inverse[r, c];
            return values;
        }
        private void AddScope(CameraEvent start, CameraEvent finish, bool renderToTexture)
        {
            var scope = new CameraScope { BeginAt = start, EndAt = finish, RenderToTexture = renderToTexture,
                Begin = new CommandBuffer { name = "LeafClip GPU begin " + start },
                End = new CommandBuffer { name = "LeafClip GPU end " + finish } };
            cameraScopes.Add(scope);
            camera.AddCommandBuffer(start, scope.Begin); camera.AddCommandBuffer(finish, scope.End);
            scope.End.IssuePluginEvent(native.GetRenderEvent(), 0);
        }
        private void DetachCamera()
        {
            foreach (CameraScope scope in cameraScopes)
            {
                if (camera) { camera.RemoveCommandBuffer(scope.BeginAt, scope.Begin); camera.RemoveCommandBuffer(scope.EndAt, scope.End); }
                scope.Begin.Dispose(); scope.End.Dispose();
            }
            cameraScopes.Clear();
            camera = null;
        }
        private void Restore(TreeEntry tree)
        {
            if (tree.Pending != null)
            {
                // Keep no Unity references on this continuation. Native handles
                // remain releasable after shutdown; stale jobs never bind a draw.
                NativeClipBridge bridge = native;
                tree.Pending.ContinueWith(task =>
                {
                    if (task.Status == TaskStatus.RanToCompletion && task.Result != 0) bridge.ReleaseGeometry(task.Result);
                    else if (task.IsFaulted) { var ignored = task.Exception; }
                }, TaskScheduler.Default);
                tree.Pending = null;
            }
            foreach (MeshEntry mesh in tree.Meshes)
            {
                if (mesh.Buffer != IntPtr.Zero) native.Unregister(mesh.Buffer);
                if (mesh.Filter && mesh.Filter.sharedMesh == mesh.Private) mesh.Filter.sharedMesh = mesh.Original;
                if (mesh.Renderer && mesh.PrivateMaterials != null)
                {
                    Material[] current = mesh.Renderer.sharedMaterials;
                    if (current.Length == mesh.PrivateMaterials.Length)
                    {
                        for (int i = 0; i < current.Length; i++) if (current[i] == mesh.PrivateMaterials[i]) current[i] = mesh.OriginalMaterials[i];
                        mesh.Renderer.sharedMaterials = current;
                    }
                }
                if (mesh.Private) UnityEngine.Object.Destroy(mesh.Private);
                if (mesh.PrivateMaterials != null) foreach (Material material in mesh.PrivateMaterials) if (material) UnityEngine.Object.Destroy(material);
            }
        }
        private void RestoreAll() { foreach (TreeEntry tree in active) Restore(tree); active.Clear(); captures.Clear(); }
        public void Dispose()
        {
            Camera.onPreCull -= BeforeCamera;
            DetachCamera(); RestoreAll(); native?.Shutdown();
            if (marker) UnityEngine.Object.Destroy(marker);
            known.Clear(); masks.Clear(); geometry.Clear(); Current = null;
        }
    }
}
