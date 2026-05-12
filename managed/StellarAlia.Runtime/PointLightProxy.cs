using System.Numerics;

namespace StellarAlia;

/// <summary>
/// Proxy for point-light property access on an entity.
/// Call <see cref="Entity.GetPointLight"/> to obtain an instance.
/// </summary>
public readonly struct PointLightProxy
{
    private readonly ulong _id;
    internal PointLightProxy(ulong id) { _id = id; }

    /// <summary>Light color in linear RGB.</summary>
    public Vector3 Color {
        get {
            NativeApi.SA_PointLight_GetColor(_id, out float r, out float g, out float b);
            return new Vector3(r, g, b);
        }
        set => NativeApi.SA_PointLight_SetColor(_id, value.X, value.Y, value.Z);
    }

    /// <summary>Light intensity (lux).</summary>
    public float Intensity {
        get => NativeApi.SA_PointLight_GetIntensity(_id);
        set => NativeApi.SA_PointLight_SetIntensity(_id, value);
    }

    /// <summary>Attenuation range in world units.</summary>
    public float Range {
        get => NativeApi.SA_PointLight_GetRange(_id);
        set => NativeApi.SA_PointLight_SetRange(_id, value);
    }
}
