#include "gizmo/GizmoSystem.hpp"

#include <glm/gtc/constants.hpp>

namespace StellarAlia::Editor {

void GizmoSystem::Draw(DebugDraw& dd, const glm::mat4& world, float size) const {
    const glm::vec3 origin = glm::vec3(world[3]);
    // Normalize axis vectors so the gizmo ignores entity scale.
    const glm::vec3 ax = glm::normalize(glm::vec3(world[0]));
    const glm::vec3 ay = glm::normalize(glm::vec3(world[1]));
    const glm::vec3 az = glm::normalize(glm::vec3(world[2]));

    switch (mode) {
        case GizmoMode::Translate: DrawTranslate(dd, origin, ax, ay, az, size); break;
        case GizmoMode::Rotate:    DrawRotate   (dd, origin, ax, ay, az, size); break;
        case GizmoMode::Scale:     DrawScale    (dd, origin, ax, ay, az, size); break;
    }
}

void GizmoSystem::DrawTranslate(DebugDraw& dd,
                                 glm::vec3 origin, glm::vec3 ax, glm::vec3 ay, glm::vec3 az,
                                 float size)
{
    dd.DrawArrow(origin, origin + ax * size, {1.f, 0.f, 0.f, 1.f});
    dd.DrawArrow(origin, origin + ay * size, {0.f, 1.f, 0.f, 1.f});
    dd.DrawArrow(origin, origin + az * size, {0.f, 0.f, 1.f, 1.f});
}

void GizmoSystem::DrawRotate(DebugDraw& dd,
                              glm::vec3 origin, glm::vec3 ax, glm::vec3 ay, glm::vec3 az,
                              float size)
{
    constexpr int kSegs = 32;
    constexpr float kStep = glm::two_pi<float>() / kSegs;

    auto ring = [&](glm::vec3 u, glm::vec3 v, glm::vec4 color) {
        for (int i = 0; i < kSegs; ++i) {
            const float a0 = kStep * i;
            const float a1 = kStep * (i + 1);
            const glm::vec3 p0 = origin + (glm::cos(a0) * u + glm::sin(a0) * v) * size;
            const glm::vec3 p1 = origin + (glm::cos(a1) * u + glm::sin(a1) * v) * size;
            dd.DrawLine(p0, p1, color);
        }
    };

    ring(ay, az, {1.f, 0.f, 0.f, 1.f});  // ring in YZ plane → rotates around X
    ring(ax, az, {0.f, 1.f, 0.f, 1.f});  // ring in XZ plane → rotates around Y
    ring(ax, ay, {0.f, 0.f, 1.f, 1.f});  // ring in XY plane → rotates around Z
}

void GizmoSystem::DrawScale(DebugDraw& dd,
                             glm::vec3 origin, glm::vec3 ax, glm::vec3 ay, glm::vec3 az,
                             float size)
{
    const float boxHalf = size * 0.07f;

    dd.DrawLine(origin, origin + ax * size, {1.f, 0.f, 0.f, 1.f});
    dd.DrawLine(origin, origin + ay * size, {0.f, 1.f, 0.f, 1.f});
    dd.DrawLine(origin, origin + az * size, {0.f, 0.f, 1.f, 1.f});

    const glm::quat identity{1.f, 0.f, 0.f, 0.f};
    dd.DrawBox(origin + ax * size, glm::vec3(boxHalf), identity, {1.f, 0.f, 0.f, 1.f});
    dd.DrawBox(origin + ay * size, glm::vec3(boxHalf), identity, {0.f, 1.f, 0.f, 1.f});
    dd.DrawBox(origin + az * size, glm::vec3(boxHalf), identity, {0.f, 0.f, 1.f, 1.f});
}

} // namespace StellarAlia::Editor
