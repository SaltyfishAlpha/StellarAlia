using System.Numerics;

namespace StellarAlia;

/// <summary>Motion type of a physics body. Mirrors C++ RigidBodyComponent::Type.</summary>
public enum RigidBodyType {
    /// <summary>Immovable collider. Not affected by forces; cannot have velocity.</summary>
    Static = 0,
    /// <summary>Moved by scripts. Pushes dynamics but is not pushed back.</summary>
    Kinematic = 1,
    /// <summary>Fully simulated. Affected by gravity, forces, and impulses.</summary>
    Dynamic = 2,
}

/// <summary>
/// Proxy for physics body operations on an entity.
/// Call <see cref="Entity.GetRigidBody"/> to obtain an instance.
/// </summary>
public readonly struct RigidBodyProxy
{
    private readonly ulong _id;
    internal RigidBodyProxy(ulong id) { _id = id; }

    /// <summary>The body's motion type (Static / Kinematic / Dynamic). LinearVelocity
    /// and AddImpulse only have observable effect on <see cref="RigidBodyType.Dynamic"/>.</summary>
    public RigidBodyType Type => (RigidBodyType)NativeApi.SA_RigidBody_GetType(_id);

    /// <summary>Linear velocity in world space (m/s).</summary>
    public Vector3 LinearVelocity {
        get {
            NativeApi.SA_RigidBody_GetLinearVelocity(_id, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => NativeApi.SA_RigidBody_SetLinearVelocity(_id, value.X, value.Y, value.Z);
    }

    /// <summary>Angular velocity in world space (rad/s).</summary>
    public Vector3 AngularVelocity {
        get {
            NativeApi.SA_RigidBody_GetAngularVelocity(_id, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => NativeApi.SA_RigidBody_SetAngularVelocity(_id, value.X, value.Y, value.Z);
    }

    /// <summary>Applies a continuous force (Newtons). Accumulated over the fixed-step.</summary>
    public void AddForce(Vector3 force) =>
        NativeApi.SA_RigidBody_AddForce(_id, force.X, force.Y, force.Z);

    /// <summary>Applies an instantaneous impulse (N·s). Applied immediately.</summary>
    public void AddImpulse(Vector3 impulse) =>
        NativeApi.SA_RigidBody_AddImpulse(_id, impulse.X, impulse.Y, impulse.Z);
}
