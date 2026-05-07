#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// DebugDraw — per-frame line accumulator for editor overlay visualization.
//
// All Draw* methods append LINE_LIST vertices (2 per line).
// Call Clear() at the start of each frame.
// SceneRenderer reads GetVertices() and uploads to a GPU SSBO for rendering.
//
// Vertex layout matches the debug_line.vert SSBO exactly (std430, 16 bytes).
// ─────────────────────────────────────────────────────────────────────────────
class DebugDraw {
public:
    struct Vertex {
        float    px, py, pz;   // position (12 bytes)
        uint32_t color;         // packed RGBA8: R=bits0-7, G=bits8-15, B=bits16-23, A=bits24-31
    };
    static_assert(sizeof(Vertex) == 16);

    static constexpr uint32_t kMaxVertices = 1u << 17;  // 131 072 = 65 536 lines max

    // ── Primitive draw calls ──────────────────────────────────────────────────
    void DrawLine   (glm::vec3 from, glm::vec3 to, glm::vec4 color);
    void DrawBox    (glm::vec3 center, glm::vec3 halfExtents,
                     glm::quat rot, glm::vec4 color);
    void DrawSphere (glm::vec3 center, float radius,
                     glm::vec4 color, int segments = 16);
    void DrawCapsule(glm::vec3 base, glm::vec3 top, float radius,
                     glm::vec4 color, int segments = 16);

    // ── Editor visualization ──────────────────────────────────────────────────
    // Directed arrow with 4-ray cone tip
    void DrawArrow  (glm::vec3 from, glm::vec3 to, glm::vec4 color,
                     float headSize = 0.08f);
    // Three axis arrows from transform origin: X=red, Y=green, Z=blue
    void DrawAxes   (const glm::mat4& transform, float size = 1.f);
    // Camera frustum wireframe from inverse view-projection matrix
    void DrawFrustum(const glm::mat4& invViewProj, glm::vec4 color);
    // Uniform XZ grid centred at origin
    void DrawGrid   (float spacing = 1.f, int halfCells = 20,
                     glm::vec4 color = {0.28f, 0.28f, 0.28f, 1.f});

    // ── Frame management ──────────────────────────────────────────────────────
    void Clear();
    [[nodiscard]] std::span<const Vertex> GetVertices() const;

private:
    static uint32_t PackColor(glm::vec4 c) noexcept;
    void Emit(glm::vec3 p, uint32_t c);

    std::vector<Vertex> m_verts;
};

} // namespace StellarAlia
