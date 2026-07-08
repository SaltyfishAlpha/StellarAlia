#include "function/debug/DebugDraw.hpp"

#include <algorithm>
#include <cmath>

namespace StellarAlia {

static constexpr float kTwoPi = 6.28318530f;
static constexpr float kPi    = 3.14159265f;

uint32_t DebugDraw::PackColor(glm::vec4 c) noexcept {
    auto u = [](float f) -> uint32_t {
        return static_cast<uint32_t>(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f);
    };
    return u(c.r) | (u(c.g) << 8) | (u(c.b) << 16) | (u(c.a) << 24);
}

void DebugDraw::Emit(glm::vec3 p, uint32_t c) {
    if (m_verts.size() >= kMaxVertices) return;
    m_verts.push_back({p.x, p.y, p.z, c});
}

void DebugDraw::EmitOverlay(glm::vec3 p, uint32_t c) {
    if (m_overlayVerts.size() >= kMaxVertices) return;
    m_overlayVerts.push_back({p.x, p.y, p.z, c});
}

void DebugDraw::Clear() {
    m_verts.clear();
    m_overlayVerts.clear();
    m_boneInstances.clear();
    m_jointInstances.clear();
}

std::span<const DebugDraw::Vertex> DebugDraw::GetVertices()        const { return m_verts; }
std::span<const DebugDraw::Vertex> DebugDraw::GetOverlayVertices() const { return m_overlayVerts; }
std::span<const DebugDraw::BoneInstance> DebugDraw::GetBoneInstances()  const { return m_boneInstances; }
std::span<const DebugDraw::BoneInstance> DebugDraw::GetJointInstances() const { return m_jointInstances; }

void DebugDraw::DrawJointSolid(glm::vec3 center, float radius, glm::vec4 color) {
    if (m_jointInstances.size() >= kMaxBoneInstances) return;
    glm::mat4 m(radius);          // uniform scale on the diagonal
    m[3] = glm::vec4(center, 1.f);
    m_jointInstances.push_back({m, color});
}

void DebugDraw::DrawBoneSolid(glm::vec3 head, glm::vec3 tail,
                              float width, glm::vec4 color) {
    if (m_boneInstances.size() >= kMaxBoneInstances) return;
    glm::vec3 axis = tail - head;
    const float len = glm::length(axis);
    if (len < 1e-5f) return;
    axis /= len;
    // Orthonormal basis with Z = bone axis; matches debug_bone.vert's unit octahedron.
    const glm::vec3 right = std::abs(axis.y) < 0.9f
        ? glm::normalize(glm::cross(glm::vec3(0.f, 1.f, 0.f), axis))
        : glm::normalize(glm::cross(glm::vec3(1.f, 0.f, 0.f), axis));
    const glm::vec3 up = glm::cross(axis, right);
    // model = translate(head) * rotate(basis) * scale(width, width, len)
    glm::mat4 m(1.f);
    m[0] = glm::vec4(right * width, 0.f);
    m[1] = glm::vec4(up    * width, 0.f);
    m[2] = glm::vec4(axis  * len,   0.f);
    m[3] = glm::vec4(head,          1.f);
    m_boneInstances.push_back({m, color});
}

void DebugDraw::DrawLineOverlay(glm::vec3 from, glm::vec3 to, glm::vec4 color) {
    const uint32_t c = PackColor(color);
    EmitOverlay(from, c);
    EmitOverlay(to,   c);
}

void DebugDraw::DrawSphereOverlay(glm::vec3 center, float radius,
                                   glm::vec4 color, int segments) {
    const uint32_t c = PackColor(color);
    for (int ring = 0; ring < 3; ++ring) {
        for (int i = 0; i < segments; ++i) {
            const float a0 = kTwoPi * i       / segments;
            const float a1 = kTwoPi * (i + 1) / segments;
            const float s0 = std::sin(a0), c0 = std::cos(a0);
            const float s1 = std::sin(a1), c1 = std::cos(a1);
            glm::vec3 p0, p1;
            if      (ring == 0) { p0 = {radius*c0, radius*s0, 0};  p1 = {radius*c1, radius*s1, 0}; }
            else if (ring == 1) { p0 = {radius*c0, 0, radius*s0};  p1 = {radius*c1, 0, radius*s1}; }
            else                { p0 = {0, radius*c0, radius*s0};  p1 = {0, radius*c1, radius*s1}; }
            EmitOverlay(center + p0, c);
            EmitOverlay(center + p1, c);
        }
    }
}

void DebugDraw::DrawBoneOverlay(glm::vec3 head, glm::vec3 tail,
                                glm::vec4 color, float width) {
    const uint32_t c = PackColor(color);
    glm::vec3 axis = tail - head;
    const float len = glm::length(axis);
    if (len < 1e-5f) return;
    axis /= len;
    const glm::vec3 right = std::abs(axis.y) < 0.9f
        ? glm::normalize(glm::cross(axis, glm::vec3(0.f, 1.f, 0.f)))
        : glm::normalize(glm::cross(axis, glm::vec3(1.f, 0.f, 0.f)));
    const glm::vec3 fwd = glm::cross(axis, right);
    // Collar (widest ring) sits ~14% down the bone, matching Blender/Maya octahedra.
    const glm::vec3 collar = head + axis * (len * 0.14f);
    const glm::vec3 ring[4] = {
        collar + right * width, collar + fwd * width,
        collar - right * width, collar - fwd * width,
    };
    for (int i = 0; i < 4; ++i) {
        EmitOverlay(head,    c); EmitOverlay(ring[i],         c);  // head → collar
        EmitOverlay(ring[i], c); EmitOverlay(tail,            c);  // collar → tail
        EmitOverlay(ring[i], c); EmitOverlay(ring[(i + 1) % 4], c);  // collar square
    }
}

void DebugDraw::DrawLine(glm::vec3 from, glm::vec3 to, glm::vec4 color) {
    const uint32_t c = PackColor(color);
    Emit(from, c);
    Emit(to,   c);
}

void DebugDraw::DrawBox(glm::vec3 center, glm::vec3 halfExtents,
                         glm::quat rot, glm::vec4 color) {
    const uint32_t c = PackColor(color);
    auto corner = [&](float sx, float sy, float sz) -> glm::vec3 {
        return center + rot * (halfExtents * glm::vec3(sx, sy, sz));
    };
    glm::vec3 p[8];
    int i = 0;
    for (float sx : {-1.f, 1.f})
        for (float sy : {-1.f, 1.f})
            for (float sz : {-1.f, 1.f})
                p[i++] = corner(sx, sy, sz);
    // p[0]=(---) p[1]=(--+) p[2]=(-+-) p[3]=(-++)
    // p[4]=(+--) p[5]=(+-+) p[6]=(++-) p[7]=(+++)
    auto e = [&](int a, int b){ Emit(p[a], c); Emit(p[b], c); };
    e(0,1); e(0,2); e(1,3); e(2,3);  // -x face
    e(4,5); e(4,6); e(5,7); e(6,7);  // +x face
    e(0,4); e(1,5); e(2,6); e(3,7);  // connecting edges
}

void DebugDraw::DrawSphere(glm::vec3 center, float radius,
                            glm::vec4 color, int segments) {
    const uint32_t c = PackColor(color);
    for (int ring = 0; ring < 3; ++ring) {
        for (int i = 0; i < segments; ++i) {
            const float a0 = kTwoPi * i       / segments;
            const float a1 = kTwoPi * (i + 1) / segments;
            const float s0 = std::sin(a0), c0 = std::cos(a0);
            const float s1 = std::sin(a1), c1 = std::cos(a1);
            glm::vec3 p0, p1;
            if      (ring == 0) { p0 = {radius*c0, radius*s0, 0};    p1 = {radius*c1, radius*s1, 0}; }
            else if (ring == 1) { p0 = {radius*c0, 0, radius*s0};    p1 = {radius*c1, 0, radius*s1}; }
            else                { p0 = {0, radius*c0, radius*s0};    p1 = {0, radius*c1, radius*s1}; }
            Emit(center + p0, c);
            Emit(center + p1, c);
        }
    }
}

void DebugDraw::DrawCapsule(glm::vec3 base, glm::vec3 top, float radius,
                             glm::vec4 color, int segments) {
    const uint32_t c = PackColor(color);
    glm::vec3 axis = top - base;
    const float len = glm::length(axis);
    if (len < 1e-6f) { DrawSphere(base, radius, color, segments); return; }
    axis /= len;
    const glm::vec3 right = std::abs(axis.x) < 0.9f
        ? glm::normalize(glm::cross(axis, glm::vec3(1.f, 0.f, 0.f)))
        : glm::normalize(glm::cross(axis, glm::vec3(0.f, 1.f, 0.f)));
    const glm::vec3 fwd = glm::cross(axis, right);
    for (int i = 0; i < segments; ++i) {
        const float a0 = kTwoPi * i       / segments;
        const float a1 = kTwoPi * (i + 1) / segments;
        const glm::vec3 r0 = radius * (right * std::cos(a0) + fwd * std::sin(a0));
        const glm::vec3 r1 = radius * (right * std::cos(a1) + fwd * std::sin(a1));
        Emit(base + r0, c); Emit(base + r1, c);  // bottom ring segment
        Emit(top  + r0, c); Emit(top  + r1, c);  // top ring segment
        Emit(base + r0, c); Emit(top  + r0, c);  // lateral strut
    }

    // Hemisphere end caps — two perpendicular semi-circle arcs per end.
    const int halfSegs = segments / 2;
    for (int i = 0; i < halfSegs; ++i) {
        const float t0 = kPi * i       / halfSegs;
        const float t1 = kPi * (i + 1) / halfSegs;
        const float c0 = std::cos(t0), s0 = std::sin(t0);
        const float c1 = std::cos(t1), s1 = std::sin(t1);
        // bottom cap: pole at base - axis*radius
        Emit(base + radius * (right * c0 - axis * s0), c);
        Emit(base + radius * (right * c1 - axis * s1), c);
        Emit(base + radius * (fwd   * c0 - axis * s0), c);
        Emit(base + radius * (fwd   * c1 - axis * s1), c);
        // top cap: pole at top + axis*radius
        Emit(top  + radius * (right * c0 + axis * s0), c);
        Emit(top  + radius * (right * c1 + axis * s1), c);
        Emit(top  + radius * (fwd   * c0 + axis * s0), c);
        Emit(top  + radius * (fwd   * c1 + axis * s1), c);
    }
}

void DebugDraw::DrawArrow(glm::vec3 from, glm::vec3 to, glm::vec4 color,
                           float headSize) {
    const uint32_t c = PackColor(color);
    Emit(from, c);
    Emit(to,   c);
    const glm::vec3 dir = to - from;
    const float len = glm::length(dir);
    if (len < 1e-4f) return;
    const glm::vec3 d = dir / len;
    const glm::vec3 right = std::abs(d.y) < 0.9f
        ? glm::normalize(glm::cross(d, glm::vec3(0.f, 1.f, 0.f)))
        : glm::normalize(glm::cross(d, glm::vec3(1.f, 0.f, 0.f)));
    const glm::vec3 up2  = glm::cross(d, right);
    const glm::vec3 stem = to - d * headSize;
    const float h = headSize * 0.4f;
    Emit(to, c); Emit(stem + right * h,  c);
    Emit(to, c); Emit(stem - right * h,  c);
    Emit(to, c); Emit(stem + up2   * h,  c);
    Emit(to, c); Emit(stem - up2   * h,  c);
}

void DebugDraw::DrawAxes(const glm::mat4& transform, float size) {
    const glm::vec3 o  = glm::vec3(transform[3]);
    const glm::vec3 xd = glm::vec3(transform[0]) * size;
    const glm::vec3 yd = glm::vec3(transform[1]) * size;
    const glm::vec3 zd = glm::vec3(transform[2]) * size;
    DrawArrow(o, o + xd, {1.f,  0.2f, 0.2f, 1.f});
    DrawArrow(o, o + yd, {0.2f, 1.f,  0.2f, 1.f});
    DrawArrow(o, o + zd, {0.2f, 0.2f, 1.f,  1.f});
}

void DebugDraw::DrawFrustum(const glm::mat4& invViewProj, glm::vec4 color) {
    const uint32_t c = PackColor(color);
    static const glm::vec4 ndc[8] = {
        {-1,-1,0,1}, {1,-1,0,1}, {1,1,0,1}, {-1,1,0,1},   // near plane corners
        {-1,-1,1,1}, {1,-1,1,1}, {1,1,1,1}, {-1,1,1,1},   // far  plane corners
    };
    glm::vec3 ws[8];
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 v = invViewProj * ndc[i];
        ws[i] = glm::vec3(v) / v.w;
    }
    for (int i = 0; i < 4; ++i) {
        Emit(ws[i],         c); Emit(ws[(i+1)%4],     c);   // near ring
        Emit(ws[i+4],       c); Emit(ws[(i+1)%4 + 4], c);   // far ring
        Emit(ws[i],         c); Emit(ws[i+4],          c);   // struts
    }
}

void DebugDraw::DrawGrid(float spacing, int halfCells, glm::vec4 color) {
    const uint32_t c = PackColor(color);
    const float ext = halfCells * spacing;
    for (int i = -halfCells; i <= halfCells; ++i) {
        const float t = i * spacing;
        Emit({t,   0.f, -ext}, c); Emit({t,   0.f,  ext}, c);
        Emit({-ext, 0.f,  t},  c); Emit({ ext, 0.f,  t},  c);
    }
}

} // namespace StellarAlia
