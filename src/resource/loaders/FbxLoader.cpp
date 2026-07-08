#include "resource/loaders/FbxLoader.hpp"

#include "resource/loaders/ImageLoader.hpp"
#include "resource/loaders/MeshUtils.hpp"
#include "core/logs/Log.hpp"

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_map>

namespace StellarAlia::Resource {

namespace {

constexpr double kBakeFps = 30.0;  // animation stacks are curve-based → bake

glm::mat4 ToGlm(const ufbx_matrix& m) {
    glm::mat4 r(1.f);
    for (int c = 0; c < 4; ++c)
        r[c] = {static_cast<float>(m.cols[c].x),
                static_cast<float>(m.cols[c].y),
                static_cast<float>(m.cols[c].z),
                c == 3 ? 1.f : 0.f};
    return r;
}

glm::vec3 ToGlm(const ufbx_vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// ── textures ─────────────────────────────────────────────────────────────────

struct ImageTable {
    std::unordered_map<const ufbx_texture*, int32_t> byTexture;

    int32_t Add(SceneData& scene, const ufbx_texture* tex) {
        if (!tex) return -1;
        if (auto it = byTexture.find(tex); it != byTexture.end()) return it->second;

        ImageData img;
        if (tex->relative_filename.length > 0) {
            img.path = std::string(tex->relative_filename.data, tex->relative_filename.length);
        } else {
            // Only an authoring-machine absolute path — keep the basename so
            // the reference resolves next to the .fbx instead of pointing at
            // another machine's filesystem.
            img.path = std::filesystem::path(
                std::string(tex->filename.data, tex->filename.length))
                .filename().string();
        }

        // Embedded payload = raw image file bytes → decode now so the cook
        // side treats it exactly like a glb-embedded image (derived UUID).
        if (tex->content.size > 0) {
            auto decoded = ImageLoader::LoadFromMemory(
                static_cast<const uint8_t*>(tex->content.data), tex->content.size, img.path);
            if (decoded) {
                decoded->path = {};  // no on-disk source — force the embedded path
                img = std::move(*decoded);
            } else {
                SA_LOG_WARN("FbxLoader: failed to decode embedded texture '{}'", img.path);
            }
        }

        const auto idx = static_cast<int32_t>(scene.images.size());
        scene.images.push_back(std::move(img));
        byTexture.emplace(tex, idx);
        return idx;
    }
};

MaterialData ConvertMaterial(SceneData& scene, ImageTable& images,
                             const ufbx_material* m) {
    MaterialData out;
    out.name = std::string(m->name.data, m->name.length);

    // ufbx maps lambert/phong into the pbr shim, so one read path covers all.
    const ufbx_material_map& base = m->pbr.base_color;
    if (base.has_value)
        out.baseColorFactor = {static_cast<float>(base.value_vec4.x),
                               static_cast<float>(base.value_vec4.y),
                               static_cast<float>(base.value_vec4.z),
                               1.f};
    if (m->pbr.roughness.has_value)
        out.roughnessFactor = static_cast<float>(m->pbr.roughness.value_real);
    if (m->pbr.metalness.has_value)
        out.metallicFactor = static_cast<float>(m->pbr.metalness.value_real);
    if (m->pbr.emission_color.has_value) {
        const float f = m->pbr.emission_factor.has_value
                            ? static_cast<float>(m->pbr.emission_factor.value_real) : 1.f;
        out.emissiveFactor = ToGlm(m->pbr.emission_color.value_vec3) * f;
    }

    out.baseColorTexture.imageIndex = images.Add(scene, base.texture);
    out.normalTexture.imageIndex    = images.Add(scene, m->pbr.normal_map.texture);
    out.emissiveTexture.imageIndex  = images.Add(scene, m->pbr.emission_color.texture);
    return out;
}

// ── skinning ─────────────────────────────────────────────────────────────────

bool MatrixNearlyEqual(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-4f) return false;
    return true;
}

// #83 P1: ALL deformers merge into one skeleton (joints keyed by bone node,
// first IBM wins) — the runtime drives a single matrix palette per entity.
// A single deformer degenerates to its own cluster order, so this is the
// only skin-conversion path.
struct MergedSkin {
    std::vector<const ufbx_node*> jointNodes;   // merged joint → node
    SkeletonData                  skeleton;
    // deformer → local cluster index → merged joint index
    std::unordered_map<const ufbx_skin_deformer*, std::vector<uint32_t>> remap;
};

MergedSkin BuildMergedSkin(const std::vector<const ufbx_skin_deformer*>& deformers) {
    MergedSkin m;
    std::unordered_map<const ufbx_node*, uint32_t> jointOf;

    for (const ufbx_skin_deformer* skin : deformers) {
        auto& table = m.remap[skin];
        table.resize(skin->clusters.count);
        for (size_t c = 0; c < skin->clusters.count; ++c) {
            const ufbx_skin_cluster* cluster = skin->clusters.data[c];
            auto [it, inserted] = jointOf.try_emplace(
                cluster->bone_node, static_cast<uint32_t>(m.jointNodes.size()));
            if (inserted) {
                m.jointNodes.push_back(cluster->bone_node);
                BoneInfo bone;
                bone.name = std::string(cluster->bone_node->name.data,
                                        cluster->bone_node->name.length);
                bone.inverseBindMatrix = ToGlm(cluster->geometry_to_bone);
                m.skeleton.bones.push_back(std::move(bone));
            } else if (!MatrixNearlyEqual(ToGlm(cluster->geometry_to_bone),
                                          m.skeleton.bones[it->second].inverseBindMatrix)) {
                // #83 P1: first wins — deformation of later skins will be off.
                SA_LOG_WARN("FbxLoader: bone '{}' has conflicting inverse-bind "
                            "matrices across skins (bind poses not unified in "
                            "the DCC) — first wins",
                            m.skeleton.bones[it->second].name);
            }
            table[c] = it->second;
        }
    }

    m.skeleton.name = "MergedSkin";
    for (size_t j = 0; j < m.jointNodes.size(); ++j) {
        // Parent = nearest ancestor that is itself a joint (skips
        // non-deforming organizational nodes between joints).
        m.skeleton.bones[j].parentIndex = -1;
        for (const ufbx_node* p = m.jointNodes[j]->parent; p; p = p->parent) {
            if (auto it = jointOf.find(p); it != jointOf.end()) {
                m.skeleton.bones[j].parentIndex = static_cast<int32_t>(it->second);
                break;
            }
        }
    }
    return m;
}

SkinVertex SkinVertexFor(const ufbx_skin_deformer* skin, uint32_t controlPoint,
                         const std::vector<uint32_t>& clusterToJoint) {
    SkinVertex sv;
    sv.weights = {0.f, 0.f, 0.f, 0.f};
    if (controlPoint >= skin->vertices.count) return sv;

    const ufbx_skin_vertex& v = skin->vertices.data[controlPoint];

    // top-4 by weight
    std::array<std::pair<float, uint32_t>, 4> top{};  // weight, cluster
    for (uint32_t k = 0; k < v.num_weights; ++k) {
        const ufbx_skin_weight& w = skin->weights.data[v.weight_begin + k];
        const float weight = static_cast<float>(w.weight);
        auto min = std::min_element(top.begin(), top.end());
        if (weight > min->first) *min = {weight, w.cluster_index};
    }
    std::sort(top.begin(), top.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    float sum = 0.f;
    for (const auto& [w, _] : top) sum += w;
    if (sum <= 0.f) { sv.weights.x = 1.f; return sv; }

    for (int i = 0; i < 4; ++i) {
        sv.joints[i]  = top[i].second < clusterToJoint.size()
                            ? clusterToJoint[top[i].second] : 0u;
        sv.weights[i] = top[i].first / sum;
    }
    return sv;
}

// ── animation baking ─────────────────────────────────────────────────────────

void BakeAnimations(const ufbx_scene* fbx, SceneData& scene,
                    const std::vector<const ufbx_node*>& jointNodes) {
    for (size_t s = 0; s < fbx->anim_stacks.count; ++s) {
        const ufbx_anim_stack* stack = fbx->anim_stacks.data[s];
        const double duration = stack->time_end - stack->time_begin;
        if (duration <= 0.0) continue;

        const auto keyCount =
            static_cast<size_t>(duration * kBakeFps) + 2;  // inclusive end key

        AnimClip clip;
        clip.name     = std::string(stack->name.data, stack->name.length);
        clip.duration = static_cast<float>(duration);

        // Channels target the merged skeleton's joint indices (#83 P1).
        {
            for (size_t c = 0; c < jointNodes.size(); ++c) {
                const ufbx_node* node = jointNodes[c];

                AnimChannel t, r, sc;
                t.boneIndex = r.boneIndex = sc.boneIndex = static_cast<int32_t>(c);
                t.target  = AnimChannel::Target::Translation;
                r.target  = AnimChannel::Target::Rotation;
                sc.target = AnimChannel::Target::Scale;

                for (size_t k = 0; k < keyCount; ++k) {
                    const double rel  = std::min(k / kBakeFps, duration);
                    const auto   time = static_cast<float>(rel);
                    const ufbx_transform tf = ufbx_evaluate_transform(
                        stack->anim, node, stack->time_begin + rel);

                    t.times.push_back(time);
                    t.values.push_back({static_cast<float>(tf.translation.x),
                                        static_cast<float>(tf.translation.y),
                                        static_cast<float>(tf.translation.z), 0.f});
                    r.times.push_back(time);
                    r.values.push_back({static_cast<float>(tf.rotation.x),
                                        static_cast<float>(tf.rotation.y),
                                        static_cast<float>(tf.rotation.z),
                                        static_cast<float>(tf.rotation.w)});
                    sc.times.push_back(time);
                    sc.values.push_back({static_cast<float>(tf.scale.x),
                                         static_cast<float>(tf.scale.y),
                                         static_cast<float>(tf.scale.z), 0.f});
                    if (rel >= duration) break;
                }
                clip.channels.push_back(std::move(t));
                clip.channels.push_back(std::move(r));
                clip.channels.push_back(std::move(sc));
            }
        }

        if (!clip.channels.empty())
            scene.animations.push_back(std::move(clip));
    }
}

} // namespace

// ── FbxLoader::Load ──────────────────────────────────────────────────────────

std::optional<SceneData> FbxLoader::Load(const std::string& path) {
    ufbx_load_opts opts{};
    opts.target_axes                  = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters           = 1.0f;
    opts.space_conversion             = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    opts.geometry_transform_handling  = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.generate_missing_normals     = true;

    ufbx_error error;
    ufbx_scene* fbx = ufbx_load_file(path.c_str(), &opts, &error);
    if (!fbx) {
        SA_LOG_ERROR("FbxLoader: failed to load '{}': {}", path, error.description.data);
        return std::nullopt;
    }

    SceneData scene;
    scene.sourcePath = path;

    ImageTable images;

    // Materials (global list; primitives reference by index).
    std::unordered_map<const ufbx_material*, int32_t> matIndexOf;
    for (size_t i = 0; i < fbx->materials.count; ++i) {
        matIndexOf[fbx->materials.data[i]] = static_cast<int32_t>(i);
        scene.materials.push_back(ConvertMaterial(scene, images, fbx->materials.data[i]));
    }

    // Skins — every deformer merges into ONE skeleton (#83 P1); all skinned
    // primitives reference skin index 0 with remapped joint indices.
    std::vector<const ufbx_skin_deformer*> skinDeformers;
    for (size_t i = 0; i < fbx->skin_deformers.count; ++i)
        skinDeformers.push_back(fbx->skin_deformers.data[i]);

    MergedSkin merged;
    if (!skinDeformers.empty()) {
        merged = BuildMergedSkin(skinDeformers);
        scene.skins.push_back(merged.skeleton);
        if (skinDeformers.size() > 1)
            SA_LOG_INFO("FbxLoader: merged {} skin deformers into one skeleton "
                        "({} joints)", skinDeformers.size(),
                        merged.skeleton.bones.size());
    }

    // Meshes — primitives split per material part.
    std::unordered_map<const ufbx_mesh*, int32_t> meshIndexOf;
    for (size_t mi = 0; mi < fbx->meshes.count; ++mi) {
        const ufbx_mesh* mesh = fbx->meshes.data[mi];
        const ufbx_skin_deformer* skin =
            mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
        const int32_t skinIdx = skin ? 0 : -1;   // merged skeleton = skin 0
        const std::vector<uint32_t>* clusterToJoint =
            skin ? &merged.remap.at(skin) : nullptr;

        MeshData md;
        md.name = std::string(mesh->name.data, mesh->name.length);

        // face lists per material part; a part-less mesh becomes one primitive
        std::vector<std::pair<int32_t, std::vector<uint32_t>>> parts;
        if (mesh->material_parts.count > 0) {
            for (size_t p = 0; p < mesh->material_parts.count; ++p) {
                const ufbx_mesh_part& part = mesh->material_parts.data[p];
                if (part.num_triangles == 0) continue;
                int32_t gmat = -1;
                if (part.index < mesh->materials.count)
                    if (auto it = matIndexOf.find(mesh->materials.data[part.index]);
                        it != matIndexOf.end())
                        gmat = it->second;
                std::vector<uint32_t> faces(part.face_indices.data,
                                            part.face_indices.data + part.face_indices.count);
                parts.emplace_back(gmat, std::move(faces));
            }
        } else {
            std::vector<uint32_t> faces(mesh->num_faces);
            for (uint32_t f = 0; f < mesh->num_faces; ++f) faces[f] = f;
            parts.emplace_back(-1, std::move(faces));
        }

        std::vector<uint32_t> tri(mesh->max_face_triangles * 3);
        const bool hasUVs = mesh->vertex_uv.exists;

        for (auto& [gmat, faces] : parts) {
            Primitive prim;
            prim.materialIndex = gmat;
            prim.skinIndex     = skinIdx;

            for (uint32_t faceIdx : faces) {
                const ufbx_face face = mesh->faces.data[faceIdx];
                const size_t numTris = ufbx_triangulate_face(
                    tri.data(), tri.size(), mesh, face);

                for (size_t i = 0; i < numTris * 3; ++i) {
                    const uint32_t ix = tri[i];
                    Vertex v;
                    v.position = ToGlm(ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                    if (mesh->vertex_normal.exists)
                        v.normal = ToGlm(ufbx_get_vertex_vec3(&mesh->vertex_normal, ix));
                    if (hasUVs) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                        // FBX UV origin is bottom-left; engine follows glTF
                        v.texCoord0 = {static_cast<float>(uv.x),
                                       1.f - static_cast<float>(uv.y)};
                    }
                    prim.indices.push_back(static_cast<uint32_t>(prim.vertices.size()));
                    prim.vertices.push_back(v);

                    if (skin)
                        prim.skinVertices.push_back(
                            SkinVertexFor(skin, mesh->vertex_indices.data[ix],
                                          *clusterToJoint));
                }
            }
            if (prim.vertices.empty()) continue;

            // MikkTSpace welds the expanded corners back into an indexed mesh.
            if (hasUVs)
                MeshUtils::GenerateTangents(prim.vertices, prim.indices,
                                            prim.skinVertices.empty()
                                                ? nullptr : &prim.skinVertices);
            md.primitives.push_back(std::move(prim));
        }

        if (md.primitives.empty()) continue;
        meshIndexOf[mesh] = static_cast<int32_t>(scene.meshes.size());
        scene.meshes.push_back(std::move(md));
    }

    // Node hierarchy (skip ufbx's synthetic root; its children become roots).
    std::unordered_map<const ufbx_node*, uint32_t> nodeIndexOf;
    for (size_t i = 0; i < fbx->nodes.count; ++i) {
        const ufbx_node* n = fbx->nodes.data[i];
        if (n->is_root) continue;
        nodeIndexOf[n] = static_cast<uint32_t>(scene.nodes.size());
        SceneNode sn;
        sn.name           = std::string(n->name.data, n->name.length);
        sn.localTransform = ToGlm(n->node_to_parent);
        if (n->mesh)
            if (auto it = meshIndexOf.find(n->mesh); it != meshIndexOf.end()) {
                sn.meshIndex = it->second;
                if (n->mesh->skin_deformers.count > 0)
                    sn.skinIndex = 0;   // merged skeleton
            }
        scene.nodes.push_back(std::move(sn));
    }
    for (size_t i = 0; i < fbx->nodes.count; ++i) {
        const ufbx_node* n = fbx->nodes.data[i];
        if (n->is_root) continue;
        SceneNode& sn = scene.nodes[nodeIndexOf[n]];
        for (size_t c = 0; c < n->children.count; ++c)
            sn.children.push_back(nodeIndexOf[n->children.data[c]]);
        if (n->parent && n->parent->is_root)
            scene.rootNodes.push_back(nodeIndexOf[n]);
    }

    BakeAnimations(fbx, scene, merged.jointNodes);

    ufbx_free_scene(fbx);

    if (scene.meshes.empty() && scene.animations.empty()) {
        SA_LOG_ERROR("FbxLoader: '{}' contains no usable geometry or animation", path);
        return std::nullopt;
    }

    SA_LOG_INFO("FbxLoader: '{}' — {} meshes, {} materials, {} skins, {} clips, {} verts",
                path, scene.meshes.size(), scene.materials.size(),
                scene.skins.size(), scene.animations.size(), scene.TotalVertexCount());
    return scene;
}

} // namespace StellarAlia::Resource
