using System;
using System.Numerics;
using StellarAlia;

/// Walk-and-steer character controller showcasing the local-space transform
/// API introduced by Issue #81. Two code paths:
///
///   - If the entity has a Dynamic RigidBody → drive XZ velocity along
///     <see cref="Entity.Forward"/>, preserve Y (gravity). Rotation is set
///     directly on the transform (Jolt re-syncs it next tick). This is the
///     production pattern: a real character is a physics body.
///
///   - Otherwise (no rigidbody) → fall back to <see cref="Entity.Translate"/>
///     with <see cref="Space.Self"/>. Useful for kinematic entities like
///     ghosts, cameras-on-rails, etc.
///
/// Either way: <c>Move.X</c> turns the character in place around Y;
/// <c>Move.Y</c> walks forward / backward along its current facing.
///
/// Why not use <c>Self.Translate(Space.Self)</c> on a Dynamic body?
///   Jolt is the authoritative source for Dynamic bodies — PhysicsSystem
///   syncs the body's pose into <c>WorldTransformComponent</c> every fixed
///   tick, overwriting any direct write to <c>TransformComponent.position</c>.
///   The visible result: the entity jumps forward each frame then snaps back
///   to the body's pose — classic "jitter, doesn't move".
public class CharacterController : ScriptBase
{
    float moveSpeed = 4f;     // m/s along Self.Forward
    float turnSpeed = 180f;   // deg/sec yaw

    public override void OnUpdate(float dt)
    {
        Vector2 move = InputAction.ReadVec2("Move");

        var rb = Self.GetRigidBody();
        if (rb.HasValue && rb.Value.Type == RigidBodyType.Dynamic)
        {
            // Cache the proxy in a local: writing `rb.Value.X = …` directly
            // tries to mutate the Nullable's property return (CS1612).
            var body = rb.Value;

            // ── Steer via angular velocity (Jolt is authoritative for Dynamic).
            // Writing TransformComponent.rotation directly would either be
            // overwritten by SyncOut next tick, or snap the entity back to its
            // spawn pose via UpdateTransforms. Both are visible jitter — see
            // PhysicsSystem::SyncOut. The clean path is to let Jolt integrate
            // angular velocity itself.
            float omegaY = -move.X * turnSpeed * Mathf.Deg2Rad;
            body.AngularVelocity = new Vector3(0f, omegaY, 0f);

            // ── Walk via linear velocity. Self.Forward reads WorldRotation
            // which, post-#81, reflects Jolt's current facing — so the velocity
            // direction tracks the body as it rotates.
            Vector3 forward = Self.Forward;
            Vector3 v = body.LinearVelocity;
            v.X = forward.X * moveSpeed * move.Y;
            v.Z = forward.Z * moveSpeed * move.Y;
            body.LinearVelocity = v;
        }
        else
        {
            // Kinematic / no-body: drive the transform directly. Space.Self
            // pre-rotates by current world rotation so "forward" tracks facing.
            if (MathF.Abs(move.X) > 0.01f)
            {
                float yawDelta = -move.X * turnSpeed * dt;
                Self.Rotate(QuaternionExt.FromEulerDegrees(0f, yawDelta, 0f), Space.Self);
            }
            if (MathF.Abs(move.Y) > 0.01f)
            {
                Self.Translate(new Vector3(0f, 0f, -move.Y * moveSpeed * dt), Space.Self);
            }
        }
    }
}
