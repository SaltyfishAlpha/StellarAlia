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

    public Vector3 GetScale() {
        NativeApi.SA_Entity_GetScale(_id, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    public void SetScale(Vector3 v) =>
        NativeApi.SA_Entity_SetScale(_id, v.X, v.Y, v.Z);

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

    // ── Animator ─────────────────────────────────────────────────────────────

    public AnimatorProxy? GetAnimator() {
        // Return null if entity has no animator component (checked implicitly via IsPlaying call).
        // SA_Animator_IsPlaying returns 0 for missing component, but we need a validity sentinel.
        // For now: construct proxy; callers use IsPlaying to detect absence.
        return IsValid ? new AnimatorProxy(_id) : null;
    }

    // ── Static lookup ─────────────────────────────────────────────────────────

    /// O(N) scene scan — cache the result in OnStart, do not call per-frame.
    public static Entity? Find(string name) {
        int found = NativeApi.SA_Entity_FindByName(name, out ulong id);
        return found != 0 ? new Entity(id) : null;
    }

    public override string ToString() => $"Entity({_id})";
}
