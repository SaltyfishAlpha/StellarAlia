using System.Numerics;

namespace StellarAlia;

/// <summary>Result of a successful <see cref="Physics.Raycast"/> call.</summary>
public struct RaycastHit
{
    /// <summary>World-space position of the intersection point.</summary>
    public Vector3 Point;

    /// <summary>Surface normal at the intersection point.</summary>
    public Vector3 Normal;

    /// <summary>The entity whose collider was hit, or null if unresolvable.</summary>
    public Entity? Entity;
}

/// <summary>Physics query utilities.</summary>
public static class Physics
{
    /// <summary>
    /// Casts a ray from <paramref name="origin"/> in <paramref name="direction"/> up to
    /// <paramref name="maxDistance"/> world units. Returns true and fills
    /// <paramref name="hit"/> on contact.
    /// </summary>
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance,
                               out RaycastHit hit)
    {
        hit = default;
        int r = NativeApi.SA_Physics_Raycast(
            origin.X, origin.Y, origin.Z,
            direction.X, direction.Y, direction.Z, maxDistance,
            out float hx, out float hy, out float hz,
            out float nx, out float ny, out float nz,
            out ulong eid);

        if (r == 0) return false;

        hit.Point  = new Vector3(hx, hy, hz);
        hit.Normal = new Vector3(nx, ny, nz);
        hit.Entity = eid != ulong.MaxValue ? new Entity(eid) : null;
        return true;
    }
}
