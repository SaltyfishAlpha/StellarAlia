using System.Numerics;

namespace StellarAlia;

/// <summary>
/// Lightweight proxy for MaterialOverrideComponent (Issue #71). Reads / writes
/// per-entity material parameter overrides by name. Names must match the
/// shader ParamDef. Returns zero / no-op when the entity has no
/// MaterialOverrideComponent — add it from the editor first.
/// </summary>
public sealed class MaterialOverrideProxy
{
    private readonly ulong _id;
    internal MaterialOverrideProxy(ulong id) { _id = id; }

    /// <summary>Reads a float-typed material parameter override. Returns 0 when unset.</summary>
    public float GetFloat(string param)
        => NativeApi.SA_MaterialOverride_GetFloat(_id, param);

    /// <summary>Writes a float-typed material parameter override.</summary>
    public void  SetFloat(string param, float value)
        => NativeApi.SA_MaterialOverride_SetFloat(_id, param, value);

    /// <summary>Reads a vec3-typed material parameter override. Returns zero when unset.</summary>
    public Vector3 GetVec3(string param) {
        NativeApi.SA_MaterialOverride_GetVec3(_id, param, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }
    /// <summary>Writes a vec3-typed material parameter override.</summary>
    public void SetVec3(string param, Vector3 v)
        => NativeApi.SA_MaterialOverride_SetVec3(_id, param, v.X, v.Y, v.Z);

    /// <summary>Reads a vec4-typed material parameter override. Returns zero when unset.</summary>
    public Vector4 GetVec4(string param) {
        NativeApi.SA_MaterialOverride_GetVec4(_id, param, out float x, out float y, out float z, out float w);
        return new Vector4(x, y, z, w);
    }
    /// <summary>Writes a vec4-typed material parameter override.</summary>
    public void SetVec4(string param, Vector4 v)
        => NativeApi.SA_MaterialOverride_SetVec4(_id, param, v.X, v.Y, v.Z, v.W);

    /// <summary>Reads a vec4 override as <see cref="Color"/>. Returns zero when unset.</summary>
    // Color is sugar over Vec4 — matches the convention used by shader ParamDefs.
    public Color GetColor(string param) {
        NativeApi.SA_MaterialOverride_GetVec4(_id, param, out float r, out float g, out float b, out float a);
        return new Color(r, g, b, a);
    }
    /// <summary>Writes a vec4 override from a <see cref="Color"/>.</summary>
    public void SetColor(string param, Color c)
        => NativeApi.SA_MaterialOverride_SetVec4(_id, param, c.R, c.G, c.B, c.A);
}
