using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace LeafClip
{
    internal sealed class NativeClipBridge
    {
        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
        [DllImport("kernel32", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int InitializeDelegate(IntPtr resource);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int Initialize12Delegate(IntPtr resource);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int Enable12Delegate(int enabled);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int FallbackCamera12Delegate([In] float[] backbufferInverse, [In] float[] textureInverse, int width, int height);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int VersionDelegate();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int RegisterDelegate(IntPtr buffer, [In] uint[] mask, int count, float branchRadius);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void UnregisterDelegate(IntPtr buffer);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int ObstructionsDelegate(IntPtr buffer, [In] float[] tree, [In] float[] boxes, int count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate ulong CreateGeometryDelegate([In] float[] boxes, int boxCount, [In] float[] triangles, int triangleCount);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int BindGeometryDelegate(IntPtr buffer, [In] float[] tree, ulong geometry);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void ReleaseGeometryDelegate(ulong geometry);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate int CameraDelegate([In] float[] inverse);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate IntPtr EventDelegate();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)] internal delegate int LogDelegate(StringBuilder output, int capacity);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void StatsDelegate([Out] ulong[] values, int count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void ShutdownDelegate();
        internal readonly InitializeDelegate Initialize;
        internal readonly Initialize12Delegate Initialize12;
        internal readonly Enable12Delegate Enable12;
        internal readonly FallbackCamera12Delegate SetFallbackCamera12;
        internal readonly RegisterDelegate Register;
        internal readonly UnregisterDelegate Unregister;
        internal readonly ObstructionsDelegate SetObstructions;
        internal readonly CreateGeometryDelegate CreateGeometry;
        internal readonly BindGeometryDelegate BindGeometry;
        internal readonly ReleaseGeometryDelegate ReleaseGeometry;
        internal readonly CameraDelegate QueueCamera;
        internal readonly EventDelegate GetRenderEvent;
        internal readonly LogDelegate PollLog;
        internal readonly StatsDelegate GetStats;
        internal readonly StatsDelegate GetStats12;
        internal readonly ShutdownDelegate Shutdown;
        internal NativeClipBridge()
        {
            string path = Path.Combine(Path.GetDirectoryName(typeof(Plugin).Assembly.Location), "LeafClip.Native.dll");
            if (!File.Exists(path)) throw new FileNotFoundException("Place LeafClip.Native.dll beside LeafClip.dll.", path);
            // Search only the DLL's own directory and System32 for dependencies.
            IntPtr module = LoadLibraryEx(path, IntPtr.Zero, 0x100 | 0x800);
            if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (Bind<VersionDelegate>(module, "LC_GetApiVersion")() != 8)
                throw new InvalidOperationException("LeafClip.Native.dll has an incompatible API version.");
            Initialize = Bind<InitializeDelegate>(module, "LC_Initialize");
            Initialize12 = Bind<Initialize12Delegate>(module, "LC_Initialize12");
            Enable12 = Bind<Enable12Delegate>(module, "LC_Enable12");
            SetFallbackCamera12 = Bind<FallbackCamera12Delegate>(module, "LC_SetFallbackCamera12");
            Register = Bind<RegisterDelegate>(module, "LC_Register");
            Unregister = Bind<UnregisterDelegate>(module, "LC_Unregister");
            SetObstructions = Bind<ObstructionsDelegate>(module, "LC_SetObstructions");
            CreateGeometry = Bind<CreateGeometryDelegate>(module, "LC_CreateGeometry");
            BindGeometry = Bind<BindGeometryDelegate>(module, "LC_BindGeometry");
            ReleaseGeometry = Bind<ReleaseGeometryDelegate>(module, "LC_ReleaseGeometry");
            QueueCamera = Bind<CameraDelegate>(module, "LC_QueueCamera");
            GetRenderEvent = Bind<EventDelegate>(module, "LC_GetRenderEvent");
            PollLog = Bind<LogDelegate>(module, "LC_PollLog");
            GetStats = Bind<StatsDelegate>(module, "LC_GetStats");
            GetStats12 = Bind<StatsDelegate>(module, "LC_GetStats12");
            Shutdown = Bind<ShutdownDelegate>(module, "LC_Shutdown");
            // Native hooks can be chained by other graphics mods. The module pins
            // itself on initialization and remains loaded until process exit.
        }
        private static T Bind<T>(IntPtr module, string name) where T : Delegate
        {
            IntPtr entry = GetProcAddress(module, name);
            if (entry == IntPtr.Zero) throw new EntryPointNotFoundException(name);
            return Marshal.GetDelegateForFunctionPointer<T>(entry);
        }
    }
}
