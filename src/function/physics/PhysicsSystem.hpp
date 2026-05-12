#pragma once

#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace StellarAlia {
class Scene;
class DebugDraw;

// ── Per-frame editor debug toggles ────────────────────────────────────────────
struct PhysicsDebugSettings {
    bool shapes    = false;   // draw collider wireframes
    bool aabbs     = false;   // draw broad-phase AABBs
    bool velocity  = false;   // draw velocity arrows
    bool contacts  = false;   // draw contact points
};

// ─────────────────────────────────────────────────────────────────────────────
// PhysicsSystem — thin ECS wrapper around Jolt Physics.
//
// Jolt types are fully hidden behind Pimpl; callers never include Jolt headers.
//
// Frame loop:
//   accumulator += dt;
//   while (accumulator >= kFixedStep) {
//       physics.SyncIn(scene);
//       physics.Step(kFixedStep);
//       physics.SyncOut(scene);
//       accumulator -= kFixedStep;
//   }
//   physics.DrawDebug(settings);   // optional editor overlay
// ─────────────────────────────────────────────────────────────────────────────
class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    // Non-copyable
    PhysicsSystem(const PhysicsSystem&)            = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    // Init creates Jolt factory, registers types, allocates temp memory.
    // Pass a DebugDraw pointer for collider overlay drawing; may be null.
    bool Init(DebugDraw* debugDraw = nullptr);
    void Shutdown();

    // ── Per fixed-step calls ──────────────────────────────────────────────────
    // SyncIn  — create bodies on first call; push Kinematic poses to Jolt.
    void SyncIn(Scene& scene);
    // Step    — advance the simulation by fixedDt seconds.
    void Step(float fixedDt);
    // SyncOut — copy Dynamic body poses back to WorldTransformComponent.
    void SyncOut(Scene& scene);

    // ── Editor overlay ────────────────────────────────────────────────────────
    // Draws collider wireframes into DebugDraw using ECS data (no Jolt callback).
    // No-op when debugDraw is null or settings are all false.
    void DrawDebug(const PhysicsDebugSettings& settings, const Scene& scene);

    // ── Play-state management ────────────────────────────────────────────────
    // Remove all Jolt bodies and reset bodyId fields to ~0u, then mark every
    // entity's WorldTransformComponent dirty so UpdateTransforms() re-derives
    // positions from TransformComponent on the next frame.
    // Call this when the engine transitions from Playing back to Editing.
    void Reset(Scene& scene);

    // ── Script API — RigidBody velocity/force ─────────────────────────────────
    // bodyId must be a valid Jolt body (RigidBodyComponent::bodyId != ~0u).
    glm::vec3 GetLinearVelocity (uint32_t bodyId) const;
    void      SetLinearVelocity (uint32_t bodyId, glm::vec3 v);
    glm::vec3 GetAngularVelocity(uint32_t bodyId) const;
    void      SetAngularVelocity(uint32_t bodyId, glm::vec3 v);
    void      AddForce          (uint32_t bodyId, glm::vec3 f);
    void      AddImpulse        (uint32_t bodyId, glm::vec3 imp);

    // ── Script API — Physics raycast ──────────────────────────────────────────
    // Returns true if the ray hits something.  maxDist is in world units.
    // On hit: hitPos, hitNormal are filled; hitEntity is the ECS entity handle
    // whose RigidBodyComponent owns the body, or entt::null if unresolvable.
    bool Raycast(glm::vec3 origin, glm::vec3 direction, float maxDist,
                 glm::vec3& hitPos, glm::vec3& hitNormal,
                 entt::entity& hitEntity, entt::registry& reg) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    DebugDraw*            m_debugDraw    = nullptr;
    bool                  m_initialized  = false;
};

} // namespace StellarAlia
