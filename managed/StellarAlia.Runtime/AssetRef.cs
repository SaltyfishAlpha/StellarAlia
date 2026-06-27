using System;

namespace StellarAlia;

/// <summary>
/// Reference to a cooked asset by stable UUID — wire-compatible with native
/// AssetID (16 bytes: hi LE + lo LE).
/// Inspector resolves this via <see cref="AssetTypeAttribute"/>; the editor's
/// asset picker filters by that type tag.
/// </summary>
public readonly struct AssetRef : IEquatable<AssetRef>
{
    /// <summary>High 64 bits of the 128-bit UUID.</summary>
    public readonly ulong Hi;
    /// <summary>Low 64 bits of the 128-bit UUID.</summary>
    public readonly ulong Lo;

    /// <summary>Constructs an AssetRef from raw hi/lo halves of a 128-bit UUID.</summary>
    public AssetRef(ulong hi, ulong lo) { Hi = hi; Lo = lo; }
    /// <summary>The all-zero UUID, used as the "unassigned" sentinel.</summary>
    public static readonly AssetRef Invalid = default;
    /// <summary>True when this ref points at a non-zero UUID.</summary>
    public bool IsValid => Hi != 0 || Lo != 0;

    /// <inheritdoc/>
    public override int  GetHashCode() => Hi.GetHashCode() ^ Lo.GetHashCode();
    /// <inheritdoc/>
    public bool          Equals(AssetRef o) => Hi == o.Hi && Lo == o.Lo;
    /// <inheritdoc/>
    public override bool Equals(object? o)  => o is AssetRef a && Equals(a);
    /// <summary>Returns true when both refs point at the same UUID.</summary>
    public static bool operator ==(AssetRef a, AssetRef b) => a.Equals(b);
    /// <summary>Returns true when the refs point at different UUIDs.</summary>
    public static bool operator !=(AssetRef a, AssetRef b) => !a.Equals(b);

    /// <summary>Formats as canonical UUID string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".</summary>
    public override string ToString() {
        if (!IsValid) return "00000000-0000-0000-0000-000000000000";
        uint   p1 = (uint)(Hi >> 32);
        ushort p2 = (ushort)(Hi >> 16);
        ushort p3 = (ushort)(Hi);
        ushort p4 = (ushort)(Lo >> 48);
        ulong  p5 = Lo & 0x0000FFFFFFFFFFFFul;
        return $"{p1:x8}-{p2:x4}-{p3:x4}-{p4:x4}-{p5:x12}";
    }

    /// <summary>Parses a canonical UUID string. Empty or malformed input returns <see cref="Invalid"/>.</summary>
    public static AssetRef FromString(string? s) {
        if (string.IsNullOrEmpty(s)) return Invalid;
        // Strip dashes; expect 32 hex chars.
        Span<char> hex = stackalloc char[32];
        int n = 0;
        foreach (char c in s) {
            if (c == '-') continue;
            if (n >= 32) return Invalid;
            hex[n++] = c;
        }
        if (n != 32) return Invalid;
        if (!ulong.TryParse(hex[..16],  System.Globalization.NumberStyles.HexNumber, null, out ulong hi)
         || !ulong.TryParse(hex[16..],  System.Globalization.NumberStyles.HexNumber, null, out ulong lo))
            return Invalid;
        return new AssetRef(hi, lo);
    }
}

/// <summary>
/// Tag a public AssetRef field with the AssetEntry::type the Inspector picker
/// should filter by ("Mesh", "Texture", "Material", "Script", "Animation", …).
/// Omitting it lets the picker show all assets.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class AssetTypeAttribute : Attribute
{
    /// <summary>The AssetEntry::type tag the Inspector should filter by.</summary>
    public string Type;
    /// <summary>Constructs the attribute with the given asset type tag.</summary>
    public AssetTypeAttribute(string type) { Type = type ?? string.Empty; }
}
