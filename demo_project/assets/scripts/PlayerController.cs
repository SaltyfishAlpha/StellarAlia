using System;
using System.Numerics;
using StellarAlia;

/// Physics-driven player controller (Issue #71 demo).
///
/// Expected entity setup (attach in editor):
///   - TransformComponent
///   - StaticMeshComponent (cube)
///   - RigidBodyComponent  type=Dynamic, mass=1
///   - ColliderComponent   Box, half-extents = mesh half-size
///
/// Why Dynamic instead of Kinematic:
///   Jolt (like PhysX/Bullet) does NOT generate contacts between two Kinematic
///   bodies — they pass through each other. To get pushed by the rotating
///   obstacle (which IS Kinematic), the player must be Dynamic.
///
/// Controls:
///   WASD       — horizontal movement (XZ plane)
///   Space      — jump (impulse along +Y)
///   Gamepad    — left stick = move, A/South = jump
public class PlayerController : ScriptBase
{
    float moveSpeed   = 4f;    // target horizontal speed (m/s)
    float jumpImpulse = 5f;    // instantaneous Y velocity on jump (m/s)
    float groundProbe = 0.02f; // |linVel.Y| below this counts as grounded

    public override void OnStart()
    {
        // Fail-loud diagnostics — the most common cause of "cube doesn't move"
        // is missing/wrong-type RigidBodyComponent. Make the misconfig obvious
        // instead of silently no-op'ing inside the native bindings.
        var rb = Self.GetRigidBody();
        if (!rb.HasValue) {
            Debug.Log("PlayerController: entity has no RigidBodyComponent — attach RigidBody (Dynamic) + Collider in Inspector.");
            return;
        }
        if (rb.Value.Type != RigidBodyType.Dynamic) {
            Debug.Log($"PlayerController: RigidBody type is {rb.Value.Type}, but velocity-driven motion requires Dynamic. Change it in Inspector.");
        }
    }

    public override void OnUpdate(float dt)
    {
        var rb = Self.GetRigidBody();
        if (!rb.HasValue) return;       // entity has no RigidBodyComponent
        var body = rb.Value;

        // ── Horizontal movement: overwrite XZ velocity, preserve Y (gravity) ──
        Vector2 move = InputAction.ReadVec2("Move");
        // Debug.Log("move:" + move);
        Vector3 v = body.LinearVelocity;
        v.X =  move.X * moveSpeed;
        v.Z = -move.Y * moveSpeed;   // input +Y = forward = -Z world
        body.LinearVelocity = v;

        // ── Jump: only when vertically near rest (cheap "grounded" check) ──
        if (InputAction.WasActivated("Jump") && MathF.Abs(v.Y) < groundProbe)
            body.AddImpulse(new Vector3(0f, jumpImpulse, 0f));

        // Keep the cube upright — Dynamic bodies tumble on contact otherwise.
        // Engine has no axis-lock flag yet, so zero angular velocity each step.
        body.AngularVelocity = Vector3.Zero;
    }
}
