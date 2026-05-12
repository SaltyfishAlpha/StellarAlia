using System.Numerics;
using System.Text;

namespace StellarAlia;

/// <summary>
/// Lightweight handle to a scene entity. Cheap to copy; check IsValid before use.
/// </summary>
public readonly struct Entity
{
    private readonly ulong _id;

    internal Entity(ulong id) { _id = id; }

    public bool IsValid => NativeApi.SA_Entity_IsValid(_id) != 0;

    // ── Transform ────────────────────────────────────────────────────────────

    public Vector3 GetPosition() {
        NativeApi.SA_Entity_GetPosition(_id, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    public void SetPosition(Vector3 v) =>
        NativeApi.SA_Entity_SetPosition(_id, v.X, v.Y, v.Z);

    public Vector3 GetRotationEuler() {
        NativeApi.SA_Entity_GetRotationEuler(_id, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    public void SetRotationEuler(Vector3 degrees) =>
        NativeApi.SA_Entity_SetRotationEuler(_id, degrees.X, degrees.Y, degrees.Z);

    /// <summary>Gets the rotation as a quaternion (lossless, no gimbal lock).</summary>
    public Quaternion GetRotation() {
        NativeApi.SA_Entity_GetRotationQuat(_id, out float w, out float x, out float y, out float z);
        return new Quaternion(x, y, z, w);
    }

    /// <summary>Sets the rotation from a quaternion. Normalised automatically.</summary>
    public void SetRotation(Quaternion q) =>
        NativeApi.SA_Entity_SetRotationQuat(_id, q.W, q.X, q.Y, q.Z);

    /// <summary>Local forward direction (−Z of the entity's rotation).</summary>
    public Vector3 Forward => Vector3.Transform(-Vector3.UnitZ, GetRotation());

    /// <summary>Local right direction (+X of the entity's rotation).</summary>
    public Vector3 Right => Vector3.Transform(Vector3.UnitX, GetRotation());

    /// <summary>Local up direction (+Y of the entity's rotation).</summary>
    public Vector3 Up => Vector3.Transform(Vector3.UnitY, GetRotation());

    public Vector3 GetScale() {
        NativeApi.SA_Entity_GetScale(_id, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    public void SetScale(Vector3 v) =>
        NativeApi.SA_Entity_SetScale(_id, v.X, v.Y, v.Z);

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// <summary>Destroys this entity and all its components.</summary>
    public void Destroy() => NativeApi.SA_Entity_Destroy(_id);

    /// <summary>Creates a new empty entity with a TagComponent and TransformComponent.</summary>
    public static Entity Create() => new Entity(NativeApi.SA_Entity_Create());

    // ── Identity ─────────────────────────────────────────────────────────────

    public string GetName() {
        byte[] buf = new byte[256];
        NativeApi.SA_Entity_GetName(_id, buf, buf.Length);
        int len = System.Array.IndexOf(buf, (byte)0);
        return Encoding.UTF8.GetString(buf, 0, len < 0 ? buf.Length : len);
    }

    public Entity? FindChild(string name) {
        int found = NativeApi.SA_Entity_FindChild(_id, name, out ulong childId);
        return found != 0 ? new Entity(childId) : null;
    }

    // ── Component proxies ─────────────────────────────────────────────────────

    /// <summary>Returns an animator proxy for this entity, or null if not valid.</summary>
    public AnimatorProxy? GetAnimator()  => IsValid ? new AnimatorProxy(_id)  : null;

    /// <summary>Returns a rigidbody proxy for this entity, or null if not valid.</summary>
    public RigidBodyProxy? GetRigidBody() => IsValid ? new RigidBodyProxy(_id) : null;

    /// <summary>Returns a point-light proxy for this entity, or null if not valid.</summary>
    public PointLightProxy? GetPointLight() => IsValid ? new PointLightProxy(_id) : null;

    // ── Static lookup ─────────────────────────────────────────────────────────

    /// <summary>O(N) scene scan — cache the result in OnStart, do not call per-frame.</summary>
    public static Entity? Find(string name) {
        int found = NativeApi.SA_Entity_FindByName(name, out ulong id);
        return found != 0 ? new Entity(id) : null;
    }

    public override string ToString() => $"Entity({_id})";
}
