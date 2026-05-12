using System.Numerics;

namespace StellarAlia;

/// <summary>
/// Proxy for physics body operations on an entity.
/// Call <see cref="Entity.GetRigidBody"/> to obtain an instance.
/// </summary>
public readonly struct RigidBodyProxy
{
    private readonly ulong _id;
    internal RigidBodyProxy(ulong id) { _id = id; }

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
