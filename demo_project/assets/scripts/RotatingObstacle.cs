using System.Numerics;
using StellarAlia;

/// Kinematic rotating obstacle (Issue #71 demo companion to PlayerController).
///
/// Expected entity setup:
///   - TransformComponent
///   - StaticMeshComponent (any shape — bar/cube works)
///   - RigidBodyComponent  type=Kinematic
///   - ColliderComponent   Box, half-extents matching the mesh
///
/// Kinematic semantics in Jolt:
///   - Setting TransformComponent each frame is the authoritative drive path;
///     PhysicsSystem::SyncIn pushes it to Jolt via SetPositionAndRotation.
///   - Kinematic ↔ Kinematic: NO contacts (engine-level rule). Two Kinematic
///     bodies pass through each other — by design.
///   - Kinematic ↔ Dynamic: Kinematic has infinite effective mass and shoves
///     Dynamic out of the way. This is what lets this obstacle push a Dynamic
///     PlayerController, but the obstacle itself is not affected by collisions.
public class RotatingObstacle : ScriptBase
{
    float rotateSpeed = 90f;  // degrees per second around Y

    float rotY;

    public override void OnStart()
    {
        rotY = Self.LocalRotationEuler.Y;
    }

    public override void OnUpdate(float dt)
    {
        rotY += rotateSpeed * dt;
        Self.LocalRotationEuler = new Vector3(0f, rotY, 0f);
    }
}
