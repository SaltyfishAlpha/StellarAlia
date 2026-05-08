// Jolt.h must be included first — it sets up internal macros.
#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include "function/physics/PhysicsSystem.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/debug/DebugDraw.hpp"
#include "core/logs/Log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <thread>

namespace StellarAlia {

// ── Jolt layer constants ──────────────────────────────────────────────────────

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING     = 1;
    static constexpr uint32_t         NUM_LAYERS = 2;
}

namespace BPLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING{ 0 };
    static constexpr JPH::BroadPhaseLayer MOVING    { 1 };
    static constexpr uint32_t             NUM_LAYERS = 2;
}

// ── Layer interfaces (required Jolt boilerplate) ──────────────────────────────

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        m_objectToBP[Layers::NON_MOVING] = BPLayers::NON_MOVING;
        m_objectToBP[Layers::MOVING]     = BPLayers::MOVING;
    }
    uint32_t GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        JPH_ASSERT(layer < Layers::NUM_LAYERS);
        return m_objectToBP[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch ((JPH::BroadPhaseLayer::Type)layer) {
            case (JPH::BroadPhaseLayer::Type)BPLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BPLayers::MOVING:     return "MOVING";
            default: return "UNKNOWN";
        }
    }
#endif
private:
    JPH::BroadPhaseLayer m_objectToBP[Layers::NUM_LAYERS];
};

class ObjVsBPFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override {
        switch (obj) {
            case Layers::NON_MOVING: return bp == BPLayers::MOVING;
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }
};

class ObjVsObjFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        switch (a) {
            case Layers::NON_MOVING: return b == Layers::MOVING;
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }
};

// ── Conversion helpers ────────────────────────────────────────────────────────

static JPH::Vec3 ToJolt(glm::vec3 v)  { return { v.x, v.y, v.z }; }
static JPH::Quat ToJolt(glm::quat q)  { return { q.x, q.y, q.z, q.w }; }
static glm::vec3 FromJolt(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
static glm::quat FromJolt(JPH::QuatArg q) { return { q.GetW(), q.GetX(), q.GetY(), q.GetZ() }; }

// Extract world position and rotation from a TRS matrix.
// glm::quat_cast on a matrix that includes scale produces a non-unit quaternion,
// so the rotation columns must be normalized first.
static std::pair<glm::vec3, glm::quat> DecomposePosRot(const glm::mat4& m) {
    const glm::vec3 pos(m[3]);
    const glm::mat3 rotMat(
        glm::normalize(glm::vec3(m[0])),
        glm::normalize(glm::vec3(m[1])),
        glm::normalize(glm::vec3(m[2]))
    );
    return { pos, glm::quat_cast(rotMat) };
}

// ── Pimpl ─────────────────────────────────────────────────────────────────────

struct PhysicsSystem::Impl {
    BPLayerInterfaceImpl              bpLayerInterface;
    ObjVsBPFilterImpl                 objVsBPFilter;
    ObjVsObjFilterImpl                objVsObjFilter;
    JPH::TempAllocatorImpl            tempAllocator{ 10u * 1024u * 1024u };  // 10 MB
    JPH::JobSystemThreadPool          jobSystem{
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        static_cast<int>(std::thread::hardware_concurrency()) - 1
    };
    JPH::PhysicsSystem                jolt;
};

// ── PhysicsSystem ─────────────────────────────────────────────────────────────

PhysicsSystem::PhysicsSystem()  = default;
PhysicsSystem::~PhysicsSystem() { if (m_initialized) Shutdown(); }

bool PhysicsSystem::Init(DebugDraw* debugDraw) {
    m_debugDraw = debugDraw;

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    m_impl = std::make_unique<Impl>();

    constexpr uint32_t kMaxBodies        = 2048;
    constexpr uint32_t kNumBodyMutexes   = 0;    // 0 = auto
    constexpr uint32_t kMaxBodyPairs     = 4096;
    constexpr uint32_t kMaxContacts      = 4096;

    m_impl->jolt.Init(
        kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContacts,
        m_impl->bpLayerInterface,
        m_impl->objVsBPFilter,
        m_impl->objVsObjFilter
    );
    m_impl->jolt.SetGravity(JPH::Vec3(0.f, -9.81f, 0.f));

    m_initialized = true;
    SA_LOG_INFO("PhysicsSystem: initialised (Jolt v{}.{}.{})",
        JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
    return true;
}

void PhysicsSystem::Shutdown() {
    if (!m_initialized) return;
    m_initialized = false;
    m_impl.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    SA_LOG_INFO("PhysicsSystem: shut down");
}

// ── SyncIn — create bodies on first call, push kinematic poses ────────────────

void PhysicsSystem::SyncIn(Scene& scene) {
    if (!m_initialized) return;

    JPH::BodyInterface& bi  = m_impl->jolt.GetBodyInterface();
    auto&               reg = scene.Registry();

    reg.view<RigidBodyComponent, TransformComponent>().each(
        [&](entt::entity e,
            RigidBodyComponent& rb,
            const TransformComponent& tr)
    {
        const auto* col = reg.try_get<ColliderComponent>(e);

        // ── Create body on first encounter ────────────────────────────────────
        if (rb.bodyId == ~0u) {
            // Build base shape
            JPH::Ref<JPH::Shape> shape;
            if (col) {
                switch (col->shape) {
                    case ColliderComponent::Shape::Sphere:
                        shape = new JPH::SphereShape(col->extents.x);
                        break;
                    case ColliderComponent::Shape::Capsule:
                        shape = new JPH::CapsuleShape(col->extents.y, col->extents.x);
                        break;
                    case ColliderComponent::Shape::Box:
                    default:
                        shape = new JPH::BoxShape(ToJolt(col->extents));
                        break;
                }

                // Wrap with local offset / rotation when non-trivial.
                const bool hasOffset = glm::dot(col->offset, col->offset) > 1e-10f;
                const bool hasRot    = glm::abs(col->rotation.w) < 1.f - 1e-6f;
                if (hasOffset || hasRot) {
                    JPH::RotatedTranslatedShapeSettings rt(
                        ToJolt(col->offset), ToJolt(col->rotation), shape);
                    auto result = rt.Create();
                    if (result.IsValid())
                        shape = result.Get();
                }
            } else {
                shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
            }

            // Determine motion type and layer
            JPH::EMotionType motionType;
            JPH::ObjectLayer layer;
            switch (rb.type) {
                case RigidBodyComponent::Type::Static:
                    motionType = JPH::EMotionType::Static;
                    layer      = Layers::NON_MOVING;
                    break;
                case RigidBodyComponent::Type::Kinematic:
                    motionType = JPH::EMotionType::Kinematic;
                    layer      = Layers::MOVING;
                    break;
                case RigidBodyComponent::Type::Dynamic:
                default:
                    motionType = JPH::EMotionType::Dynamic;
                    layer      = Layers::MOVING;
                    break;
            }

            // Use the world transform for initial placement.
            // DecomposePosRot normalizes the rotation columns to strip scale before
            // extracting the quaternion — quat_cast on a raw TRS matrix produces a
            // non-unit quaternion that corrupts Jolt's internal rotation state.
            const auto* wt = reg.try_get<WorldTransformComponent>(e);
            auto [worldPos, worldRot] = wt
                ? DecomposePosRot(wt->matrix)
                : std::make_pair(tr.position, tr.rotation);

            JPH::BodyCreationSettings settings(
                shape,
                ToJolt(worldPos),
                ToJolt(worldRot),
                motionType,
                layer
            );
            settings.mFriction    = rb.friction;
            settings.mRestitution = rb.restitution;
            if (motionType == JPH::EMotionType::Dynamic && rb.mass > 0.f) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = rb.mass;
            }

            JPH::Body* body = bi.CreateBody(settings);
            if (!body) {
                SA_LOG_ERROR("PhysicsSystem: failed to create body for entity {}",
                             static_cast<uint32_t>(entt::to_integral(e)));
                return;
            }
            bi.AddBody(body->GetID(), JPH::EActivation::Activate);
            rb.bodyId = body->GetID().GetIndexAndSequenceNumber();
            return;
        }

        // ── Kinematic: push TransformComponent → Jolt every step ─────────────
        if (rb.type == RigidBodyComponent::Type::Kinematic) {
            const JPH::BodyID bid{ rb.bodyId };
            const auto* wt = reg.try_get<WorldTransformComponent>(e);
            auto [p, r] = wt
                ? DecomposePosRot(wt->matrix)
                : std::make_pair(tr.position, tr.rotation);
            bi.SetPositionAndRotation(bid, ToJolt(p), ToJolt(r),
                                      JPH::EActivation::Activate);
        }
    });
}

// ── Step ──────────────────────────────────────────────────────────────────────

void PhysicsSystem::Step(float fixedDt) {
    if (!m_initialized) return;
    constexpr int kCollisionSteps = 1;
    m_impl->jolt.Update(fixedDt, kCollisionSteps,
                        &m_impl->tempAllocator, &m_impl->jobSystem);
}

// ── SyncOut — copy Dynamic poses back to WorldTransformComponent ──────────────

void PhysicsSystem::SyncOut(Scene& scene) {
    if (!m_initialized) return;

    const JPH::BodyInterface& bi  = m_impl->jolt.GetBodyInterface();
    auto&                     reg = scene.Registry();

    reg.view<RigidBodyComponent>().each(
        [&](entt::entity e, const RigidBodyComponent& rb)
    {
        if (rb.type != RigidBodyComponent::Type::Dynamic) return;
        if (rb.bodyId == ~0u) return;

        const JPH::BodyID bid{ rb.bodyId };
        const glm::vec3 pos = FromJolt(bi.GetPosition(bid));
        const glm::quat rot = FromJolt(bi.GetRotation(bid));

        // Preserve scale from TransformComponent
        const auto* tr    = reg.try_get<TransformComponent>(e);
        const glm::vec3 s = tr ? tr->scale : glm::vec3(1.f);

        auto* wt = reg.try_get<WorldTransformComponent>(e);
        if (!wt) wt = &reg.emplace<WorldTransformComponent>(e);

        wt->matrix =
            glm::translate(glm::mat4(1.f), pos) *
            glm::mat4_cast(rot) *
            glm::scale(glm::mat4(1.f), s);
        wt->dirty = false;   // bypass UpdateTransforms for this entity
    });
}

// ── DrawDebug — ECS-based collider wireframes ─────────────────────────────────

void PhysicsSystem::DrawDebug(const PhysicsDebugSettings& settings, const Scene& scene) {
    if (!m_initialized || !m_debugDraw) return;
    if (!settings.shapes && !settings.aabbs && !settings.velocity) return;

    const JPH::BodyInterface& bi  = m_impl->jolt.GetBodyInterface();
    auto&                     reg = const_cast<entt::registry&>(scene.Registry());

    reg.view<RigidBodyComponent, ColliderComponent, WorldTransformComponent>().each(
        [&](entt::entity /*e*/,
            const RigidBodyComponent&    rb,
            const ColliderComponent&     col,
            const WorldTransformComponent& wt)
    {
        const auto [pos, rot] = DecomposePosRot(wt.matrix);
        const glm::vec4 col_active   = { 0.2f, 1.0f, 0.3f, 1.f };
        const glm::vec4 col_static   = { 0.5f, 0.5f, 0.5f, 1.f };
        const glm::vec4 color = (rb.type == RigidBodyComponent::Type::Static)
                              ? col_static : col_active;

        if (settings.shapes) {
            // Apply collider-local offset and rotation in world space.
            const glm::vec3 drawPos  = pos + rot * col.offset;
            const glm::quat drawRot  = rot * col.rotation;

            switch (col.shape) {
                case ColliderComponent::Shape::Box:
                    m_debugDraw->DrawBox(drawPos, col.extents, drawRot, color);
                    break;
                case ColliderComponent::Shape::Sphere:
                    m_debugDraw->DrawSphere(drawPos, col.extents.x, color);
                    break;
                case ColliderComponent::Shape::Capsule:
                    m_debugDraw->DrawCapsule(
                        drawPos - drawRot * glm::vec3(0, col.extents.y, 0),
                        drawPos + drawRot * glm::vec3(0, col.extents.y, 0),
                        col.extents.x, color);
                    break;
            }
        }

        if (settings.velocity && rb.bodyId != ~0u &&
            rb.type == RigidBodyComponent::Type::Dynamic)
        {
            const JPH::BodyID bid{ rb.bodyId };
            const glm::vec3 vel = FromJolt(bi.GetLinearVelocity(bid));
            if (glm::dot(vel, vel) > 0.001f)
                m_debugDraw->DrawArrow(pos, pos + vel * 0.1f, { 1.f, 1.f, 0.f, 1.f });
        }
    });
}

// ── Reset — remove all Jolt bodies, restore dirty flags ──────────────────────

void PhysicsSystem::Reset(Scene& scene) {
    if (!m_initialized) return;

    JPH::BodyInterface& bi  = m_impl->jolt.GetBodyInterface();
    auto&               reg = scene.Registry();

    reg.view<RigidBodyComponent>().each([&](entt::entity e, RigidBodyComponent& rb) {
        if (rb.bodyId != ~0u) {
            const JPH::BodyID bid{ rb.bodyId };
            bi.RemoveBody(bid);
            bi.DestroyBody(bid);
            rb.bodyId = ~0u;
        }
        // Let UpdateTransforms() recompute world position from TransformComponent.
        if (auto* wt = reg.try_get<WorldTransformComponent>(e))
            wt->dirty = true;
    });

    SA_LOG_INFO("PhysicsSystem: reset ({} bodies removed)", 0);
}

} // namespace StellarAlia
