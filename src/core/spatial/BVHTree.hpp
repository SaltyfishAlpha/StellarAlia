#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <vector>

namespace StellarAlia::Core {

// ─────────────────────────────────────────────────────────────────────────────
// Frustum — 6 half-spaces extracted from a combined view-projection matrix.
// Plane equation: dot(plane.xyz, p) + plane.w >= 0  means p is inside.
// ─────────────────────────────────────────────────────────────────────────────
struct Frustum {
    glm::vec4 planes[6]; // left, right, bottom, top, near, far

    // Gribb-Hartmann extraction for Vulkan NDC (depth [0,1]).
    // viewProj = proj * view (GLM column-major).
    static Frustum Extract(const glm::mat4& m) {
        // Rows of m (GLM is column-major: m[col][row])
        auto row = [&](int r) {
            return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]);
        };
        const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        Frustum f;
        f.planes[0] = r3 + r0; // left
        f.planes[1] = r3 - r0; // right
        f.planes[2] = r3 + r1; // bottom
        f.planes[3] = r3 - r1; // top
        f.planes[4] = r2;      // near  (Vulkan: z >= 0)
        f.planes[5] = r3 - r2; // far
        // Normalize so plane.w is correct distance metric.
        for (auto& p : f.planes) {
            const float len = glm::length(glm::vec3(p));
            if (len > 1e-6f) p /= len;
        }
        return f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Ray — world-space ray for BVH traversal and scene picking.
// ─────────────────────────────────────────────────────────────────────────────
struct Ray {
    glm::vec3 origin;
    glm::vec3 dir;    // unit vector
    glm::vec3 invDir; // 1/dir, pre-computed for slab tests

    static Ray FromOriginDir(const glm::vec3& o, const glm::vec3& d) {
        Ray r;
        r.origin = o;
        r.dir    = d;
        r.invDir = glm::vec3(1.f) / d;
        return r;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BVHTree<T> — axis-aligned BVH over arbitrary payloads.
//
// Usage per scene change:
//   bvh.Clear();
//   for each object: bvh.Insert(worldMin, worldMax, payload);
//   bvh.Build();   // longest-axis median split, O(N log N)
//
// Usage per frame:
//   bvh.Query(frustum, outVec);       // frustum culling
//   bvh.Raycast(ray, maxDist, t, p);  // nearest hit picking
//
// Skinned objects (dynamic AABB) can call UpdateLeaf after animation.
// ─────────────────────────────────────────────────────────────────────────────
template<typename T>
class BVHTree {
public:
    void Clear() {
        m_prims.clear();
        m_nodes.clear();
    }

    void Insert(const glm::vec3& worldMin, const glm::vec3& worldMax, T payload) {
        m_prims.push_back({ worldMin, worldMax,
            (worldMin + worldMax) * 0.5f, std::move(payload) });
    }

    // Build the BVH after all Insert calls. Safe to call on empty tree.
    void Build() {
        m_nodes.clear();
        if (m_prims.empty()) return;
        m_nodes.reserve(m_prims.size() * 2);
        BuildRecursive(0, static_cast<int>(m_prims.size()));
    }

    // Collect payloads whose AABB intersects the frustum.
    void Query(const Frustum& f, std::vector<T>& out) const {
        if (m_nodes.empty()) return;
        QueryNode(0, f, out);
    }

    // Find the closest payload hit by ray within [0, maxDist].
    // Returns true if any hit was found; outT = hit distance, outPayload = payload.
    bool Raycast(const Ray& ray, float maxDist, float& outT, T& outPayload) const {
        if (m_nodes.empty()) return false;
        outT = maxDist;
        bool hit = false;
        RaycastNode(0, ray, outT, outPayload, hit);
        return hit;
    }

    // Update the world-space AABB of a leaf identified by payload (== comparison).
    // Rebuilds the whole tree — call only when a single leaf changes per frame.
    // For many skinned updates prefer Clear+Insert+Build.
    void UpdateLeaf(const T& payload, const glm::vec3& newMin, const glm::vec3& newMax) {
        for (auto& p : m_prims) {
            if (p.payload == payload) {
                p.min      = newMin;
                p.max      = newMax;
                p.centroid = (newMin + newMax) * 0.5f;
                break;
            }
        }
        Build();
    }

    // Slab test: ray vs AABB. Returns true if hit in [0, tMax].
    // Exposed as public for use outside the BVH (e.g. brute-force Phase B picking).
    static bool RayAABB(const Ray& ray, const glm::vec3& mn, const glm::vec3& mx,
                        float tMax, float& tHit) {
        const glm::vec3 t0 = (mn - ray.origin) * ray.invDir;
        const glm::vec3 t1 = (mx - ray.origin) * ray.invDir;
        const glm::vec3 tNear = glm::min(t0, t1);
        const glm::vec3 tFar  = glm::max(t0, t1);
        const float tMin = std::max({tNear.x, tNear.y, tNear.z});
        tHit             = std::min({tFar.x,  tFar.y,  tFar.z});
        return tMin <= tHit && tHit >= 0.f && tMin <= tMax;
    }

private:
    struct Prim {
        glm::vec3 min, max, centroid;
        T         payload;
    };

    struct Node {
        glm::vec3 aabbMin, aabbMax;
        int32_t   left       = -1; // -1 = leaf
        int32_t   right      = -1;
        int32_t   primBegin  = -1; // leaf: first prim index
        int32_t   primEnd    = -1; // leaf: one-past-last
    };

    std::vector<Prim> m_prims; // stable after Build (sorted in-place)
    std::vector<Node> m_nodes;

    // Returns node index.
    int BuildRecursive(int begin, int end) {
        const int nodeIdx = static_cast<int>(m_nodes.size());
        m_nodes.push_back({});
        Node& node = m_nodes[nodeIdx];

        // Compute node AABB.
        node.aabbMin = glm::vec3( 1e30f);
        node.aabbMax = glm::vec3(-1e30f);
        for (int i = begin; i < end; ++i) {
            node.aabbMin = glm::min(node.aabbMin, m_prims[i].min);
            node.aabbMax = glm::max(node.aabbMax, m_prims[i].max);
        }

        if (end - begin <= 2) {
            // Leaf
            node.primBegin = begin;
            node.primEnd   = end;
            return nodeIdx;
        }

        // Split on longest axis at centroid median.
        glm::vec3 extent(0.f);
        for (int i = begin; i < end; ++i)
            extent = glm::max(extent, glm::abs(m_prims[i].centroid - node.aabbMin));
        const int axis = (extent.x >= extent.y && extent.x >= extent.z) ? 0
                       : (extent.y >= extent.z)                          ? 1 : 2;

        const int mid = (begin + end) / 2;
        std::nth_element(m_prims.begin() + begin, m_prims.begin() + mid,
                         m_prims.begin() + end,
                         [axis](const Prim& a, const Prim& b) {
                             return a.centroid[axis] < b.centroid[axis];
                         });

        // Re-fetch node ref after potential realloc from recursive calls.
        const int leftIdx  = BuildRecursive(begin, mid);
        const int rightIdx = BuildRecursive(mid, end);
        m_nodes[nodeIdx].left  = leftIdx;
        m_nodes[nodeIdx].right = rightIdx;
        return nodeIdx;
    }

    // AABB vs frustum: p-vertex test on each plane.
    static bool AABBInFrustum(const glm::vec3& mn, const glm::vec3& mx,
                               const Frustum& f) {
        for (const auto& plane : f.planes) {
            const glm::vec3 pv(
                plane.x >= 0.f ? mx.x : mn.x,
                plane.y >= 0.f ? mx.y : mn.y,
                plane.z >= 0.f ? mx.z : mn.z);
            if (glm::dot(glm::vec3(plane), pv) + plane.w < 0.f)
                return false;
        }
        return true;
    }

    void QueryNode(int idx, const Frustum& f, std::vector<T>& out) const {
        const Node& node = m_nodes[idx];
        if (!AABBInFrustum(node.aabbMin, node.aabbMax, f)) return;

        if (node.left == -1) {
            // Leaf — add all prims.
            for (int i = node.primBegin; i < node.primEnd; ++i)
                out.push_back(m_prims[i].payload);
            return;
        }
        QueryNode(node.left,  f, out);
        QueryNode(node.right, f, out);
    }

    void RaycastNode(int idx, const Ray& ray, float& bestT, T& bestPayload,
                     bool& hit) const {
        const Node& node = m_nodes[idx];
        float tBox;
        if (!RayAABB(ray, node.aabbMin, node.aabbMax, bestT, tBox)) return;

        if (node.left == -1) {
            // Leaf — test each prim individually.
            for (int i = node.primBegin; i < node.primEnd; ++i) {
                float t;
                if (RayAABB(ray, m_prims[i].min, m_prims[i].max, bestT, t)) {
                    bestT       = t;
                    bestPayload = m_prims[i].payload;
                    hit         = true;
                }
            }
            return;
        }

        // Visit nearer child first for early termination.
        float tL, tR;
        const bool hitL = RayAABB(ray, m_nodes[node.left ].aabbMin,
                                       m_nodes[node.left ].aabbMax, bestT, tL);
        const bool hitR = RayAABB(ray, m_nodes[node.right].aabbMin,
                                       m_nodes[node.right].aabbMax, bestT, tR);
        if (hitL && hitR) {
            const int near = tL <= tR ? node.left  : node.right;
            const int far_ = tL <= tR ? node.right : node.left;
            RaycastNode(near, ray, bestT, bestPayload, hit);
            RaycastNode(far_, ray, bestT, bestPayload, hit);
        } else if (hitL) {
            RaycastNode(node.left,  ray, bestT, bestPayload, hit);
        } else if (hitR) {
            RaycastNode(node.right, ray, bestT, bestPayload, hit);
        }
    }
};

} // namespace StellarAlia::Core
