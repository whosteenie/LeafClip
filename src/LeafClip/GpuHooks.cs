using HarmonyLib;

namespace LeafClip
{
    [HarmonyPatch(typeof(TreeBase), "Awake")]
    internal static class TreeAwakePatch
    {
        private static void Postfix(TreeBase __instance) => GpuClip.Current?.Register(__instance);
    }

    [HarmonyPatch(typeof(Piece), "Awake")]
    internal static class PieceAwakePatch
    {
        private static void Postfix(Piece __instance) => GpuClip.Current?.Notify(__instance);
    }

    [HarmonyPatch(typeof(Piece), "SetCreator")]
    internal static class PieceCreatorPatch
    {
        private static void Postfix(Piece __instance) => GpuClip.Current?.Notify(__instance);
    }

    [HarmonyPatch(typeof(Piece), "OnDestroy")]
    internal static class PieceDestroyPatch
    {
        private static void Prefix(Piece __instance) => GpuClip.Current?.Notify(__instance);
    }
}
