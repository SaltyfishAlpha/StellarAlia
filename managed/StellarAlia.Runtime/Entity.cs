using System.Numerics;
using System.Text;

namespace StellarAlia;

/// <summary>
/// Lightweight handle to a scene entity. Cheap to copy; check IsValid before use.
///
/// Transform API: all accessors are explicit about coordinate space —
/// <see cref="LocalPosition"/> / <see cref="LocalRotation"/> / <see cref="LocalScale"/>
/// are parent-relative (the serialised form the Inspector shows), while
/// <see cref="WorldPosition"/> / <see cref="WorldRotation"/> /
/// <see cref="LossyWorldScale"/> walk the hierarchy. World-space readers are
/// lazily refreshed in native code, so a freshly-set parent transform is
/// visible immediately within the same script frame.
/// </summary>
public readonly unsafe struct Entity
{
    private readonly ulong _id;

    internal Entity(ulong id) { _id = id; }

    /// <summary>True when the underlying entity still exists in the scene.</summary>
    public bool IsValid => NativeApi.SA_Entity_IsValid(_id) != 0;

    // ── Transform — local (parent-relative) ──────────────────────────────────

    /// <summary>The entity's local-space position (parent-relative).</summary>
    public Vector3 LocalPosition {
        get {
            NativeApi.SA_Entity_GetLocalPosition(_id, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => NativeApi.SA_Entity_SetLocalPosition(_id, value.X, value.Y, value.Z);
    }

    /// <summary>The entity's local rotation as Euler angles in degrees (XYZ order).</summary>
    public Vector3 LocalRotationEuler {
        get {
            NativeApi.SA_Entity_GetLocalRotationEuler(_id, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => NativeApi.SA_Entity_SetLocalRotationEuler(_id, value.X, value.Y, value.Z);
    }

    /// <summary>The entity's local rotation quaternion (parent-relative).</summary>
    public Quaternion LocalRotation {
        get {
            NativeApi.SA_Entity_GetLocalRotationQuat(_id, out float w, out float x, out float y, out float z);
            return new Quaternion(x, y, z, w);
        }
        set => NativeApi.SA_Entity_SetLocalRotationQuat(_id, value.W, value.X, value.Y, value.Z);
    }

    /// <summary>The entity's local scale.</summary>
    public Vector3 LocalScale {
        get {
            NativeApi.SA_Entity_GetLocalScale(_id, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => NativeApi.SA_Entity_SetLocalScale(_id, value.X, value.Y, value.Z);
    }

    // ── Transform — world (hierarchy-resolved, lazy-refreshed) ───────────────

    /// <summary>The entity's world-space position (parent chain composed).</summary>
    public Vector3 WorldPosition {
        get {
            NativeApi.SA_Entity_GetWorldPosition(_id, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => NativeApi.SA_Entity_SetWorldPosition(_id, value.X, value.Y, value.Z);
    }

    /// <summary>The entity's world rotation as a quaternion.</summary>
    public Quaternion WorldRotation {
        get {
            NativeApi.SA_Entity_GetWorldRotationQuat(_id, out float w, out float x, out float y, out float z);
            return new Quaternion(x, y, z, w);
        }
        set => NativeApi.SA_Entity_SetWorldRotationQuat(_id, value.W, value.X, value.Y, value.Z);
    }

    /// <summary>The entity's world rotation as Euler angles in degrees (XYZ order).</summary>
    public Vector3 WorldRotationEuler {
        get => QuaternionExt.ToEulerDegrees(WorldRotation);
        set => WorldRotation = QuaternionExt.FromEulerDegrees(value);
    }

    /// <summary>
    /// Lossy world scale extracted from the world matrix basis vector lengths.
    /// Read-only by design — non-uniform parent scale would make a world-scale
    /// setter ambiguous (Unity follows the same convention).
    /// </summary>
    public Vector3 LossyWorldScale {
        get {
            NativeApi.SA_Entity_GetLossyWorldScale(_id, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
    }

    /// <summary>
    /// The entity's 4×4 world matrix. Returned in System.Numerics row-major
    /// layout, so <c>Vector3.Transform(point, WorldMatrix)</c> applies the
    /// full TRS chain. Useful as the raw input for custom space conversions.
    /// </summary>
    public Matrix4x4 WorldMatrix {
        get {
            // Native fills 16 floats in glm column-major; transpose into the
            // System.Numerics row-major layout so Vector3.Transform "just works".
            float* m = stackalloc float[16];
            NativeApi.SA_Entity_GetWorldMatrix(_id, m);
            return new Matrix4x4(
                m[0],  m[1],  m[2],  m[3],
                m[4],  m[5],  m[6],  m[7],
                m[8],  m[9],  m[10], m[11],
                m[12], m[13], m[14], m[15]);
        }
    }

    // ── Direction basis (always world) ───────────────────────────────────────

    /// <summary>World-space forward vector (engine convention: −Z of world rotation).</summary>
    public Vector3 Forward => Vector3.Transform(-Vector3.UnitZ, WorldRotation);
    /// <summary>World-space right vector (+X of world rotation).</summary>
    public Vector3 Right   => Vector3.Transform( Vector3.UnitX, WorldRotation);
    /// <summary>World-space up vector (+Y of world rotation).</summary>
    public Vector3 Up      => Vector3.Transform( Vector3.UnitY, WorldRotation);

    // ── Movement / rotation helpers ──────────────────────────────────────────

    /// <summary>
    /// Translates the entity by <paramref name="delta"/>. <see cref="Space.Self"/>
    /// interprets <paramref name="delta"/> in the entity's local frame (so
    /// <c>Translate(Vector3.UnitX)</c> moves along Right); <see cref="Space.World"/>
    /// applies the delta directly in world space.
    /// </summary>
    public void Translate(Vector3 delta, Space space = Space.Self) {
        if (space == Space.World) {
            WorldPosition += delta;
        } else {
            // Self-space: rotate the delta by the current world rotation so
            // "+UnitX" really means "+Right" regardless of parent rotation.
            WorldPosition += Vector3.Transform(delta, WorldRotation);
        }
    }

    /// <summary>
    /// Rotates the entity by <paramref name="delta"/>. <see cref="Space.Self"/>
    /// pre-composes (local), <see cref="Space.World"/> post-composes (world).
    /// </summary>
    public void Rotate(Quaternion delta, Space space = Space.Self) {
        if (space == Space.World) {
            WorldRotation = delta * WorldRotation;
        } else {
            LocalRotation = LocalRotation * delta;
        }
    }

    // ── Space conversion (pure managed; reads WorldMatrix once) ──────────────

    /// <summary>Transforms a point from this entity's local space into world space.</summary>
    public Vector3 TransformPoint(Vector3 localPoint)
        => Vector3.Transform(localPoint, WorldMatrix);

    /// <summary>Transforms a point from world space into this entity's local space.</summary>
    public Vector3 InverseTransformPoint(Vector3 worldPoint) {
        if (!Matrix4x4.Invert(WorldMatrix, out var inv)) return worldPoint;
        return Vector3.Transform(worldPoint, inv);
    }

    /// <summary>
    /// Transforms a direction from local to world space — rotation only, scale
    /// and translation are ignored (matches Unity's TransformDirection).
    /// </summary>
    public Vector3 TransformDirection(Vector3 localDir)
        => Vector3.Transform(localDir, WorldRotation);

    /// <summary>Transforms a direction from world to local space (rotation only).</summary>
    public Vector3 InverseTransformDirection(Vector3 worldDir)
        => Vector3.Transform(worldDir, Quaternion.Conjugate(WorldRotation));

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// <summary>Destroys this entity and all its components.</summary>
    public void Destroy() => NativeApi.SA_Entity_Destroy(_id);

    /// <summary>Creates a new empty entity with a TagComponent and TransformComponent.</summary>
    public static Entity Create() => new Entity(NativeApi.SA_Entity_Create());

    // ── Identity ─────────────────────────────────────────────────────────────

    /// <summary>Returns the entity's TagComponent name (UTF-8, truncated to 255 bytes).</summary>
    public string GetName() {
        byte[] buf = new byte[256];
        NativeApi.SA_Entity_GetName(_id, buf, buf.Length);
        int len = System.Array.IndexOf(buf, (byte)0);
        return Encoding.UTF8.GetString(buf, 0, len < 0 ? buf.Length : len);
    }

    /// <summary>Finds an immediate child by name, or null when none matches.</summary>
    public Entity? FindChild(string name) {
        int found = NativeApi.SA_Entity_FindChild(_id, name, out ulong childId);
        return found != 0 ? new Entity(childId) : null;
    }

    // ── Component proxies ─────────────────────────────────────────────────────

    /// <summary>Returns an animator proxy for this entity, or null if not valid.</summary>
    public AnimatorProxy? GetAnimator()  => IsValid ? new AnimatorProxy(_id)  : null;

    /// <summary>Returns a rigidbody proxy for this entity, or null when the
    /// entity has no RigidBodyComponent. Previous behaviour returned a proxy
    /// unconditionally and let LinearVelocity / AddImpulse silently no-op —
    /// which made misconfigured scenes hard to diagnose (Issue #71 demo).</summary>
    public RigidBodyProxy? GetRigidBody()
        => IsValid && NativeApi.SA_RigidBody_HasComponent(_id) != 0
            ? new RigidBodyProxy(_id) : null;

    /// <summary>Returns a point-light proxy for this entity, or null if not valid.</summary>
    public PointLightProxy? GetPointLight() => IsValid ? new PointLightProxy(_id) : null;

    /// <summary>Returns a mesh proxy (StaticMesh + MeshRenderer) for this entity, or null if not valid.</summary>
    public MeshProxy? GetMesh() => IsValid ? new MeshProxy(_id) : null;

    /// <summary>Returns a material-override proxy for this entity, or null if not valid.</summary>
    public MaterialOverrideProxy? GetMaterialOverride()
        => IsValid ? new MaterialOverrideProxy(_id) : null;

    // ── Static lookup ─────────────────────────────────────────────────────────

    /// <summary>O(N) scene scan — cache the result in OnStart, do not call per-frame.</summary>
    public static Entity? Find(string name) {
        int found = NativeApi.SA_Entity_FindByName(name, out ulong id);
        return found != 0 ? new Entity(id) : null;
    }

    /// <inheritdoc/>
    public override string ToString() => $"Entity({_id})";
}
