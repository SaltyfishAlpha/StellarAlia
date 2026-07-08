#include "resource/loaders/MeshUtils.hpp"

#include "core/logs/Log.hpp"

#include <mikktspace.h>

#include <cstring>
#include <unordered_map>

#include <glm/geometric.hpp>

namespace StellarAlia::Resource::MeshUtils {

void GenerateNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    for (auto& v : vertices)
        v.normal = {0.f, 0.f, 0.f};

    // Unnormalized cross product = 2×triangle area — large faces dominate,
    // which is the weighting we want.
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;
        const glm::vec3 e1 = vertices[i1].position - vertices[i0].position;
        const glm::vec3 e2 = vertices[i2].position - vertices[i0].position;
        const glm::vec3 n  = glm::cross(e1, e2);
        vertices[i0].normal += n;
        vertices[i1].normal += n;
        vertices[i2].normal += n;
    }

    for (auto& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.f, 1.f, 0.f);
    }
}

void GenerateFlatNormals(std::vector<Vertex>&     vertices,
                         std::vector<uint32_t>&   indices,
                         std::vector<SkinVertex>* skinVertices)
{
    if (skinVertices && skinVertices->size() != vertices.size())
        skinVertices = nullptr;

    std::vector<Vertex>     corners;
    std::vector<SkinVertex> skinCorners;
    corners.reserve(indices.size());
    if (skinVertices) skinCorners.reserve(indices.size());

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;

        const glm::vec3 e1 = vertices[i1].position - vertices[i0].position;
        const glm::vec3 e2 = vertices[i2].position - vertices[i0].position;
        glm::vec3 n = glm::cross(e1, e2);
        const float len = glm::length(n);
        n = len > 1e-12f ? n / len : glm::vec3(0.f, 1.f, 0.f);

        for (uint32_t src : {i0, i1, i2}) {
            Vertex v = vertices[src];
            v.normal = n;
            corners.push_back(v);
            if (skinVertices) skinCorners.push_back((*skinVertices)[src]);
        }
    }

    indices.resize(corners.size());
    for (size_t i = 0; i < corners.size(); ++i)
        indices[i] = static_cast<uint32_t>(i);
    vertices = std::move(corners);
    if (skinVertices) *skinVertices = std::move(skinCorners);
}

// ─── MikkTSpace adapter ──────────────────────────────────────────────────────

namespace {

struct MikkMesh {
    std::vector<Vertex>*     corners;      // unindexed, 3 per face
    std::vector<SkinVertex>* skinCorners;  // parallel or nullptr
};

int MikkGetNumFaces(const SMikkTSpaceContext* ctx) {
    const auto* m = static_cast<const MikkMesh*>(ctx->m_pUserData);
    return static_cast<int>(m->corners->size() / 3);
}

int MikkGetNumVerticesOfFace(const SMikkTSpaceContext*, int) { return 3; }

const Vertex& MikkCorner(const SMikkTSpaceContext* ctx, int face, int vert) {
    const auto* m = static_cast<const MikkMesh*>(ctx->m_pUserData);
    return (*m->corners)[static_cast<size_t>(face) * 3 + vert];
}

void MikkGetPosition(const SMikkTSpaceContext* ctx, float out[], int face, int vert) {
    const Vertex& v = MikkCorner(ctx, face, vert);
    out[0] = v.position.x; out[1] = v.position.y; out[2] = v.position.z;
}

void MikkGetNormal(const SMikkTSpaceContext* ctx, float out[], int face, int vert) {
    const Vertex& v = MikkCorner(ctx, face, vert);
    out[0] = v.normal.x; out[1] = v.normal.y; out[2] = v.normal.z;
}

void MikkGetTexCoord(const SMikkTSpaceContext* ctx, float out[], int face, int vert) {
    const Vertex& v = MikkCorner(ctx, face, vert);
    out[0] = v.texCoord0.x; out[1] = v.texCoord0.y;
}

void MikkSetTSpaceBasic(const SMikkTSpaceContext* ctx, const float tangent[],
                        float sign, int face, int vert) {
    auto* m = static_cast<MikkMesh*>(ctx->m_pUserData);
    Vertex& v = (*m->corners)[static_cast<size_t>(face) * 3 + vert];
    v.tangent = {tangent[0], tangent[1], tangent[2], sign};
}

// Bitwise weld key over Vertex (+ optional SkinVertex). Both structs are
// tightly packed floats/uints (48 / 32 bytes, no padding), so byte equality
// is exact equality.
struct WeldKey {
    Vertex     v;
    SkinVertex sv;

    bool operator==(const WeldKey& o) const {
        return std::memcmp(this, &o, sizeof(WeldKey)) == 0;
    }
};

struct WeldKeyHash {
    size_t operator()(const WeldKey& k) const {
        // FNV-1a over the raw bytes.
        const auto* p = reinterpret_cast<const unsigned char*>(&k);
        size_t h = 14695981039346656037ull;
        for (size_t i = 0; i < sizeof(WeldKey); ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }
};

} // namespace

bool GenerateTangents(std::vector<Vertex>&     vertices,
                      std::vector<uint32_t>&   indices,
                      std::vector<SkinVertex>* skinVertices)
{
    if (indices.size() % 3 != 0) {
        SA_LOG_WARN("MeshUtils::GenerateTangents — index count {} not a multiple of 3",
                    indices.size());
        return false;
    }
    for (uint32_t i : indices)
        if (i >= vertices.size()) {
            SA_LOG_WARN("MeshUtils::GenerateTangents — index {} out of range ({})",
                        i, vertices.size());
            return false;
        }
    if (skinVertices && skinVertices->size() != vertices.size())
        skinVertices = nullptr;  // not parallel — treat as unskinned

    // 1) expand to per-corner
    std::vector<Vertex>     corners(indices.size());
    std::vector<SkinVertex> skinCorners(skinVertices ? indices.size() : 0);
    for (size_t i = 0; i < indices.size(); ++i) {
        corners[i] = vertices[indices[i]];
        if (skinVertices) skinCorners[i] = (*skinVertices)[indices[i]];
    }

    // 2) run MikkTSpace
    MikkMesh mesh{&corners, skinVertices ? &skinCorners : nullptr};

    SMikkTSpaceInterface iface{};
    iface.m_getNumFaces          = MikkGetNumFaces;
    iface.m_getNumVerticesOfFace = MikkGetNumVerticesOfFace;
    iface.m_getPosition          = MikkGetPosition;
    iface.m_getNormal            = MikkGetNormal;
    iface.m_getTexCoord          = MikkGetTexCoord;
    iface.m_setTSpaceBasic       = MikkSetTSpaceBasic;

    SMikkTSpaceContext ctx{};
    ctx.m_pInterface = &iface;
    ctx.m_pUserData  = &mesh;

    if (!genTangSpaceDefault(&ctx)) {
        SA_LOG_WARN("MeshUtils::GenerateTangents — MikkTSpace failed");
        return false;
    }

    // 3) weld back
    std::vector<Vertex>     newVerts;
    std::vector<SkinVertex> newSkin;
    std::vector<uint32_t>   newIndices(corners.size());
    newVerts.reserve(vertices.size());
    if (skinVertices) newSkin.reserve(vertices.size());

    std::unordered_map<WeldKey, uint32_t, WeldKeyHash> lookup;
    lookup.reserve(corners.size());

    for (size_t i = 0; i < corners.size(); ++i) {
        WeldKey key{};
        key.v = corners[i];
        if (skinVertices) key.sv = skinCorners[i];

        auto [it, inserted] = lookup.try_emplace(key,
                                                 static_cast<uint32_t>(newVerts.size()));
        if (inserted) {
            newVerts.push_back(corners[i]);
            if (skinVertices) newSkin.push_back(skinCorners[i]);
        }
        newIndices[i] = it->second;
    }

    vertices = std::move(newVerts);
    indices  = std::move(newIndices);
    if (skinVertices) *skinVertices = std::move(newSkin);
    return true;
}

} // namespace StellarAlia::Resource::MeshUtils
