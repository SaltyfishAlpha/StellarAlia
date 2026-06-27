namespace StellarAlia;

/// <summary>
/// Lightweight proxy for StaticMeshComponent + MeshRendererComponent of an
/// entity (Issue #71). Use to read the mesh asset, query / override material
/// slots at runtime, and toggle per-renderer shadow flags.
/// </summary>
public sealed class MeshProxy
{
    private readonly ulong _id;
    internal MeshProxy(ulong id) { _id = id; }

    /// <summary>UUID of the static mesh asset. Invalid when no StaticMeshComponent.</summary>
    public AssetRef MeshAsset => AssetRef.FromString(NativeApi.SA_StaticMesh_GetAssetUUID(_id));

    /// <summary>Number of material override slots on the MeshRendererComponent.</summary>
    public int MaterialCount => NativeApi.SA_MeshRenderer_GetSlotCount(_id);

    /// <summary>Read the material assigned to the given slot. Returns Invalid when out of range.</summary>
    public AssetRef GetMaterial(int slot)
        => AssetRef.FromString(NativeApi.SA_MeshRenderer_GetSlotUUID(_id, slot));

    /// <summary>Override the material in the given slot. Returns true when applied
    /// (slot in range), false otherwise — out-of-range slots do not auto-grow.</summary>
    public bool SetMaterial(int slot, AssetRef material)
        => NativeApi.SA_MeshRenderer_SetSlotUUID(_id, slot, material.ToString()) != 0;

    /// <summary>Whether this mesh casts shadows into the shadow map.</summary>
    public bool CastShadow {
        get => NativeApi.SA_MeshRenderer_GetCastShadow(_id) != 0;
        set => NativeApi.SA_MeshRenderer_SetCastShadow(_id, value ? 1 : 0);
    }

    /// <summary>Whether this mesh receives sampled shadows in the lighting pass.</summary>
    public bool ReceiveShadow {
        get => NativeApi.SA_MeshRenderer_GetReceiveShadow(_id) != 0;
        set => NativeApi.SA_MeshRenderer_SetReceiveShadow(_id, value ? 1 : 0);
    }
}
