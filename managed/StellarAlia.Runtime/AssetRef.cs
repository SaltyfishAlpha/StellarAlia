using System;

namespace StellarAlia;

// Reference to a cooked asset by stable UUID — wire-compatible with native
// AssetID (16 bytes: hi LE + lo LE).
//
// Inspector resolves this via `[AssetType("Mesh")]`/`[AssetType("Texture")]`
// etc.; the editor's asset picker filters by that type tag.
public readonly struct AssetRef : IEquatable<AssetRef>
{
    public readonly ulong Hi;
    public readonly ulong Lo;

    public AssetRef(ulong hi, ulong lo) { Hi = hi; Lo = lo; }
    public static readonly AssetRef Invalid = default;
    public bool IsValid => Hi != 0 || Lo != 0;

    public override int  GetHashCode() => Hi.GetHashCode() ^ Lo.GetHashCode();
    public bool          Equals(AssetRef o) => Hi == o.Hi && Lo == o.Lo;
    public override bool Equals(object? o)  => o is AssetRef a && Equals(a);
    public static bool operator ==(AssetRef a, AssetRef b) => a.Equals(b);
    public static bool operator !=(AssetRef a, AssetRef b) => !a.Equals(b);

    // Canonical UUID string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
    public override string ToString() {
        if (!IsValid) return "00000000-0000-0000-0000-000000000000";
        uint   p1 = (uint)(Hi >> 32);
        ushort p2 = (ushort)(Hi >> 16);
        ushort p3 = (ushort)(Hi);
        ushort p4 = (ushort)(Lo >> 48);
        ulong  p5 = Lo & 0x0000FFFFFFFFFFFFul;
        return $"{p1:x8}-{p2:x4}-{p3:x4}-{p4:x4}-{p5:x12}";
    }
}

// Tag a public AssetRef field with the AssetEntry::type the Inspector picker
// should filter by ("Mesh", "Texture", "Material", "Script", "Animation", …).
// Omitting it lets the picker show all assets.
[AttributeUsage(AttributeTargets.Field)]
public sealed class AssetTypeAttribute : Attribute
{
    public string Type;
    public AssetTypeAttribute(string type) { Type = type ?? string.Empty; }
}
