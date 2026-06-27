using System;
using System.Numerics;
using StellarAlia;

/// Third-person camera that trails a target entity. Demonstrates the
/// world-space transform API (Issue #81): reads the target's
/// <see cref="Entity.WorldPosition"/> + <see cref="Entity.WorldRotationEuler"/>
/// each frame, places <see cref="Entity.WorldPosition"/> at a smoothed offset
/// behind it, and points <see cref="Entity.WorldRotation"/> at the character.
///
/// Expected entity setup (Inspector):
///   - TransformComponent + CameraComponent
///   - targetName field set to the controlled entity's tag (default "Player")
///   - offset = camera position in the target's yaw-frame (default behind+above)
public class CameraFollow : ScriptBase
{
    public string  targetName = "Player";
    public Vector3 offset     = new(0f, 3f, 6f);  // behind (+Z) and above (+Y) target
    public float   smoothing  = 8f;               // higher = snappier; 0 = no smoothing
    public Vector3 lookAtBias = new(0f, 1f, 0f);  // aim slightly above target origin

    Entity? _target;

    public override void OnStart()
    {
        var t = Entity.Find(targetName);
        if (t.HasValue) {
            _target = t.Value;
            Debug.Log($"CameraFollow: tracking '{targetName}' (camera = {Self.GetName()})");
        } else {
            Debug.Warn($"CameraFollow: target '{targetName}' not found — " +
                       "set the targetName field in Inspector to your character's tag.");
        }
    }

    public override void OnLateUpdate(float dt)
    {
        if (!_target.HasValue) return;
        var target = _target.Value;
        if (!target.IsValid) return;

        // Rotate the offset by the target's world yaw so the camera trails
        // behind the character as it turns. We deliberately drop pitch/roll
        // (a third-person cam shouldn't tilt with the character).
        Quaternion yawOnly = QuaternionExt.FromEulerDegrees(
            0f, target.WorldRotationEuler.Y, 0f);
        Vector3 desiredPos = target.WorldPosition + Vector3.Transform(offset, yawOnly);

        // Exponential smoothing toward the desired position. Lerp factor is
        // clamped so a hitched frame can't overshoot past the target.
        float t = smoothing <= 0f ? 1f : MathF.Min(1f, smoothing * dt);
        Self.WorldPosition = Vector3.Lerp(Self.WorldPosition, desiredPos, t);

        // Aim at a point slightly above the target so the character stays
        // centered vertically in the frame even when very close.
        Vector3 lookTarget = target.WorldPosition + lookAtBias;
        Self.WorldRotation = LookRotation(Self.WorldPosition, lookTarget);
    }

    // Builds a rotation whose −Z (Entity.Forward in this engine) points from
    // `from` toward `to`. Right-handed; world-up is +Y. No System.Numerics
    // helper covers exactly this convention, so we assemble the basis by hand.
    static Quaternion LookRotation(Vector3 from, Vector3 to)
    {
        Vector3 forward = to - from;
        float len2 = forward.LengthSquared();
        if (len2 < 1e-8f) return Quaternion.Identity;
        forward /= MathF.Sqrt(len2);

        Vector3 up = Vector3.UnitY;
        // Degenerate case: looking straight up/down. Fall back to world +Z.
        if (MathF.Abs(Vector3.Dot(forward, up)) > 0.999f)
            up = Vector3.UnitZ;

        // Engine convention: Entity.Forward = −Z. Build basis so the matrix's
        // −Z column equals `forward`.
        Vector3 zAxis = -forward;                                  // +Z = back
        Vector3 xAxis = Vector3.Normalize(Vector3.Cross(up, zAxis));
        Vector3 yAxis = Vector3.Cross(zAxis, xAxis);

        // System.Numerics is row-vector: each ROW of the matrix is a basis
        // axis expressed in world space.
        Matrix4x4 m = new(
            xAxis.X, xAxis.Y, xAxis.Z, 0f,
            yAxis.X, yAxis.Y, yAxis.Z, 0f,
            zAxis.X, zAxis.Y, zAxis.Z, 0f,
            0f,      0f,      0f,      1f);
        return Quaternion.CreateFromRotationMatrix(m);
    }
}
