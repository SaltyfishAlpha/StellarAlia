namespace StellarAlia;

/// <summary>
/// Coordinate-space tag used by <see cref="Entity.Translate"/> and
/// <see cref="Entity.Rotate"/> to disambiguate whether the supplied delta is
/// expressed in the entity's local frame or in world space.
/// </summary>
public enum Space
{
    /// <summary>Apply the delta in the entity's local frame (parent-relative).</summary>
    Self,
    /// <summary>Apply the delta in world space.</summary>
    World,
}
