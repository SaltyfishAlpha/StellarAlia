using System.Collections.Generic;
using System.Numerics;
using System.Reflection;

namespace StellarAlia.Bridge;

// ─────────────────────────────────────────────────────────────────────────────
// FieldReflector — scans a C# script type for inspectable fields and bridges
// schema / value blobs to and from instances via System.Reflection.
//
// #74 scope: public instance fields only. #75 will add [SerializeField] private
// support, [Range]/[Tooltip]/[Header] attributes, and AssetRef/Entity handling.
// ─────────────────────────────────────────────────────────────────────────────
internal static class FieldReflector
{
    private const ushort kSchemaWireVersion = 2;

    // Cache of (Type → FieldInfo[]) so reflection scan runs at most once per
    // class, then memoised for the life of the ALC. Cleared by ScriptLoader.Unload.
    private static readonly Dictionary<Type, FieldInfo[]> s_cache = new();

    public static void ClearCache() => s_cache.Clear();

    public static IReadOnlyList<FieldInfo> GetSerializedFields(Type t) {
        if (s_cache.TryGetValue(t, out var cached)) return cached;
        var list = new List<FieldInfo>();
        foreach (var f in t.GetFields(BindingFlags.Public | BindingFlags.Instance)) {
            if (f.IsInitOnly || f.IsLiteral) continue;  // skip readonly + const
            list.Add(f);
        }
        var arr = list.ToArray();
        s_cache[t] = arr;
        return arr;
    }

    public static (ScriptFieldKind kind, string typeHint) ResolveKind(Type fieldType) {
        if (fieldType == typeof(bool))                       return (ScriptFieldKind.Bool,   "");
        if (fieldType == typeof(int))                        return (ScriptFieldKind.Int32,  "");
        if (fieldType == typeof(float))                      return (ScriptFieldKind.Float,  "");
        if (fieldType == typeof(string))                     return (ScriptFieldKind.String, "");
        if (fieldType == typeof(Vector2))                    return (ScriptFieldKind.Vec2,   "");
        if (fieldType == typeof(Vector3))                    return (ScriptFieldKind.Vec3,   "");
        if (fieldType == typeof(Vector4))                    return (ScriptFieldKind.Vec4,   "");
        if (fieldType == typeof(StellarAlia.Color))          return (ScriptFieldKind.Color,  "");
        if (fieldType == typeof(StellarAlia.AssetRef))       return (ScriptFieldKind.AssetRef, "");
        if (fieldType == typeof(StellarAlia.Entity))         return (ScriptFieldKind.EntityRef, "");
        if (fieldType.IsEnum)                                return (ScriptFieldKind.Enum,   fieldType.FullName ?? "");
        return (ScriptFieldKind.Unsupported, "");
    }

    // Fixed payload size for a given kind; 0 for variable-length kinds (String).
    private static ushort PayloadSize(ScriptFieldKind k) => k switch {
        ScriptFieldKind.Bool      => 1,
        ScriptFieldKind.Int32     => 4,
        ScriptFieldKind.Float     => 4,
        ScriptFieldKind.Enum      => 4,
        ScriptFieldKind.Vec2      => 8,
        ScriptFieldKind.Vec3      => 12,
        ScriptFieldKind.Vec4      => 16,
        ScriptFieldKind.AssetRef  => 16,
        ScriptFieldKind.EntityRef => 8,
        ScriptFieldKind.Color     => 16,   // RGBA float ⇒ same wire as Vec4
        _ => 0,
    };

    // ── Schema blob ─────────────────────────────────────────────────────────
    //
    // Wire layout:
    //   u16 schemaVersion = 1
    //   str className
    //   u32 fieldCount
    //   for each field:
    //     str name; u8 kind; str typeHint; u16 byteSize
    public static byte[] BuildSchemaBlob(Type t) {
        var w = new BlobWriter();
        w.WriteU16(kSchemaWireVersion);
        w.WriteStr(t.FullName ?? t.Name);

        var fields = GetSerializedFields(t);
        w.WriteU32((uint)fields.Count);
        foreach (var f in fields) {
            (ScriptFieldKind kind, string typeHint) = ResolveKind(f.FieldType);
            // AssetRef may carry [AssetType("Mesh")] for picker filtering.
            if (kind == ScriptFieldKind.AssetRef) {
                var attr = f.GetCustomAttribute<StellarAlia.AssetTypeAttribute>();
                if (attr != null) typeHint = attr.Type;
            }
            w.WriteStr(f.Name);
            w.WriteU8((byte)kind);
            w.WriteStr(typeHint);
            w.WriteU16(PayloadSize(kind));

            // v2 attribute trailer.
            string tooltip = f.GetCustomAttribute<StellarAlia.TooltipAttribute>()?.Text ?? string.Empty;
            string header  = f.GetCustomAttribute<StellarAlia.HeaderAttribute>()?.Text  ?? string.Empty;
            var    range   = f.GetCustomAttribute<StellarAlia.RangeAttribute>();
            bool   hidden  = f.GetCustomAttribute<StellarAlia.HideInInspectorAttribute>() != null;
            byte   flags   = 0;
            if (hidden)       flags |= 0x01;
            if (range != null) flags |= 0x02;
            w.WriteStr(tooltip);
            w.WriteStr(header);
            w.WriteU8(flags);
            if (range != null) {
                w.WriteF32(range.Min);
                w.WriteF32(range.Max);
            }
        }
        return w.ToArray();
    }

    // ── Field-value blob: apply to instance ─────────────────────────────────

    public static int ApplyFieldValues(object instance, ReadOnlySpan<byte> blob) {
        var r = new BlobReader(blob);
        if (!r.ReadU32(out uint count)) return 0;

        var fields = GetSerializedFields(instance.GetType());
        var byName = new Dictionary<string, FieldInfo>(fields.Count);
        foreach (var f in fields) byName[f.Name] = f;

        int applied = 0;
        for (uint i = 0; i < count && !r.Bad; ++i) {
            if (!r.ReadStr(out string name))   return applied;
            if (!r.ReadU8(out byte kindByte))  return applied;
            if (!r.ReadU16(out ushort payLen)) return applied;
            var kind = (ScriptFieldKind)kindByte;

            if (!byName.TryGetValue(name, out var field)) {
                r.Skip(payLen);
                continue;
            }
            if (TryReadValue(ref r, kind, payLen, field.FieldType, out object? value)) {
                field.SetValue(instance, value);
                ++applied;
            }
            // else: TryReadValue already consumed/skipped payLen on its own path.
        }
        return applied;
    }

    private static bool TryReadValue(ref BlobReader r, ScriptFieldKind kind, ushort payLen,
                                     Type fieldType, out object? value)
    {
        value = null;
        switch (kind) {
            case ScriptFieldKind.Bool:
                if (payLen != 1) { r.Skip(payLen); return false; }
                if (!r.ReadU8(out byte b)) return false;
                value = b != 0;
                return true;
            case ScriptFieldKind.Int32:
                if (payLen != 4) { r.Skip(payLen); return false; }
                if (!r.ReadI32(out int iv)) return false;
                value = iv;
                return true;
            case ScriptFieldKind.Enum:
                if (payLen != 4) { r.Skip(payLen); return false; }
                if (!r.ReadI32(out int ev)) return false;
                value = fieldType.IsEnum ? Enum.ToObject(fieldType, ev) : ev;
                return true;
            case ScriptFieldKind.Float:
                if (payLen != 4) { r.Skip(payLen); return false; }
                if (!r.ReadF32(out float fv)) return false;
                value = fv;
                return true;
            case ScriptFieldKind.Vec2: {
                if (payLen != 8) { r.Skip(payLen); return false; }
                if (!r.ReadF32(out float x) || !r.ReadF32(out float y)) return false;
                value = new Vector2(x, y);
                return true;
            }
            case ScriptFieldKind.Vec3: {
                if (payLen != 12) { r.Skip(payLen); return false; }
                if (!r.ReadF32(out float x) || !r.ReadF32(out float y) || !r.ReadF32(out float z)) return false;
                value = new Vector3(x, y, z);
                return true;
            }
            case ScriptFieldKind.Vec4: {
                if (payLen != 16) { r.Skip(payLen); return false; }
                if (!r.ReadF32(out float x) || !r.ReadF32(out float y) || !r.ReadF32(out float z) || !r.ReadF32(out float w)) return false;
                value = new Vector4(x, y, z, w);
                return true;
            }
            case ScriptFieldKind.String: {
                // payload itself is str (u16 + utf8) — payLen is total str size.
                if (!r.ReadStr(out string s)) return false;
                value = s;
                return true;
            }
            case ScriptFieldKind.Color: {
                // Color is wire-compatible with Vec4 (RGBA float).
                if (payLen != 16) { r.Skip(payLen); return false; }
                if (!r.ReadF32(out float cr) || !r.ReadF32(out float cg) ||
                    !r.ReadF32(out float cb) || !r.ReadF32(out float ca)) return false;
                value = new StellarAlia.Color(cr, cg, cb, ca);
                return true;
            }
            case ScriptFieldKind.AssetRef: {
                if (payLen != 16) { r.Skip(payLen); return false; }
                if (!r.ReadU64(out ulong hi) || !r.ReadU64(out ulong lo)) return false;
                value = new StellarAlia.AssetRef(hi, lo);
                return true;
            }
            case ScriptFieldKind.EntityRef: {
                // Native sends the live entt::entity bits (ScriptSystem translates
                // sc.fields' persistent sceneLocalId before encoding).
                if (payLen != 8) { r.Skip(payLen); return false; }
                if (!r.ReadU64(out ulong bits)) return false;
                value = s_entityCtor(bits);
                return true;
            }
            default:
                r.Skip(payLen);
                return false;
        }
    }

    // StellarAlia.Entity has an internal ctor — we reach it via reflection once
    // and cache a delegate that wraps it (called in TryReadValue per EntityRef).
    private static readonly Func<ulong, object> s_entityCtor = BuildEntityCtor();
    private static Func<ulong, object> BuildEntityCtor() {
        var ctor = typeof(StellarAlia.Entity).GetConstructor(
            BindingFlags.Instance | BindingFlags.NonPublic, new[] { typeof(ulong) });
        return ctor is null
            ? (_ => default(StellarAlia.Entity))
            : id => ctor.Invoke(new object[] { id });
    }

    // ── Field-value blob: capture from instance ─────────────────────────────

    public static byte[] CaptureFieldValues(object instance) {
        var fields = GetSerializedFields(instance.GetType());
        var w = new BlobWriter();
        w.WriteU32((uint)fields.Count);

        foreach (var f in fields) {
            (ScriptFieldKind kind, _) = ResolveKind(f.FieldType);
            w.WriteStr(f.Name);
            w.WriteU8((byte)kind);
            WriteFieldPayload(w, kind, f.GetValue(instance));
        }
        return w.ToArray();
    }

    private static void WriteFieldPayload(BlobWriter w, ScriptFieldKind kind, object? raw) {
        switch (kind) {
            case ScriptFieldKind.Bool: {
                w.WriteU16(1);
                w.WriteU8(raw is bool b && b ? (byte)1 : (byte)0);
                break;
            }
            case ScriptFieldKind.Int32: {
                w.WriteU16(4);
                w.WriteI32(raw is int i ? i : 0);
                break;
            }
            case ScriptFieldKind.Enum: {
                w.WriteU16(4);
                w.WriteI32(raw is null ? 0 : Convert.ToInt32(raw));
                break;
            }
            case ScriptFieldKind.Float: {
                w.WriteU16(4);
                w.WriteF32(raw is float f ? f : 0f);
                break;
            }
            case ScriptFieldKind.Vec2: {
                w.WriteU16(8);
                var v = raw is Vector2 vv ? vv : Vector2.Zero;
                w.WriteF32(v.X); w.WriteF32(v.Y);
                break;
            }
            case ScriptFieldKind.Vec3: {
                w.WriteU16(12);
                var v = raw is Vector3 vv ? vv : Vector3.Zero;
                w.WriteF32(v.X); w.WriteF32(v.Y); w.WriteF32(v.Z);
                break;
            }
            case ScriptFieldKind.Vec4: {
                w.WriteU16(16);
                var v = raw is Vector4 vv ? vv : Vector4.Zero;
                w.WriteF32(v.X); w.WriteF32(v.Y); w.WriteF32(v.Z); w.WriteF32(v.W);
                break;
            }
            case ScriptFieldKind.String: {
                string s = raw as string ?? string.Empty;
                byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(s);
                if (utf8.Length > 65535 - 2) {
                    var trunc = new byte[65535 - 2];
                    System.Array.Copy(utf8, trunc, trunc.Length);
                    utf8 = trunc;
                }
                w.WriteU16((ushort)(2 + utf8.Length));  // payloadLen = u16 len + body
                w.WriteStr(s.Length == utf8.Length ? s : System.Text.Encoding.UTF8.GetString(utf8));
                break;
            }
            case ScriptFieldKind.Color: {
                w.WriteU16(16);
                var c = raw is StellarAlia.Color cc ? cc : StellarAlia.Color.White;
                w.WriteF32(c.R); w.WriteF32(c.G); w.WriteF32(c.B); w.WriteF32(c.A);
                break;
            }
            case ScriptFieldKind.AssetRef: {
                w.WriteU16(16);
                var a = raw is StellarAlia.AssetRef ar ? ar : StellarAlia.AssetRef.Invalid;
                w.WriteU64(a.Hi); w.WriteU64(a.Lo);
                break;
            }
            case ScriptFieldKind.EntityRef: {
                w.WriteU16(8);
                // Capture writes the raw entt::entity bits — native translates
                // back to sceneLocalId on decode (ScriptSystem::CaptureFieldValues).
                ulong bits = 0;
                if (raw != null) {
                    var idField = typeof(StellarAlia.Entity).GetField(
                        "_id", BindingFlags.Instance | BindingFlags.NonPublic);
                    if (idField != null) bits = (ulong)idField.GetValue(raw)!;
                }
                w.WriteU64(bits);
                break;
            }
            default: {
                // Unsupported kind: emit zero-length payload, native reader skips.
                w.WriteU16(0);
                break;
            }
        }
    }
}
