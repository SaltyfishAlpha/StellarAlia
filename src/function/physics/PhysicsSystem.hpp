#pragma once

#include <memory>

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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    DebugDraw*            m_debugDraw    = nullptr;
    bool                  m_initialized  = false;
};

} // namespace StellarAlia
