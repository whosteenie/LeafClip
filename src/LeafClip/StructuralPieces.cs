namespace LeafClip
{
    // Uses the game's build metadata rather than an English prefab-name test.
    internal static class StructuralPieces
    {
        internal static bool Includes(Piece piece)
        {
            if (!piece || !piece.IsPlacedByPlayer()) return false;
            const Piece.UsageTagFlags excluded = Piece.UsageTagFlags.Furniture | Piece.UsageTagFlags.Crafting |
                Piece.UsageTagFlags.Lighting | Piece.UsageTagFlags.Decor | Piece.UsageTagFlags.Storage |
                Piece.UsageTagFlags.Stacks | Piece.UsageTagFlags.Transport;
            if ((piece.m_usage & excluded) != 0) return false;
            const Piece.UsageTagFlags structural = Piece.UsageTagFlags.Building | Piece.UsageTagFlags.Floor |
                Piece.UsageTagFlags.Wall | Piece.UsageTagFlags.Roof | Piece.UsageTagFlags.Architecture |
                Piece.UsageTagFlags.Stairs | Piece.UsageTagFlags.Doors;
            return (piece.m_usage & structural) != 0 || piece.m_category == Piece.PieceCategory.BuildingWorkbench ||
                piece.m_category == Piece.PieceCategory.BuildingStonecutter;
        }
    }
}
