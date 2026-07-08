#include "function/material/MaterialManager.hpp"
#include "function/material/ProgramCache.hpp"
#include "function/renderer/RenderFeature.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"

#include <filesystem>
#include "resource/ResourceManager.hpp"
#include "resource/vfs/VFS.hpp"
#include "core/logs/Log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace StellarAlia {

// ── Shader loader (local helper) ──────────────────────────────────────────────

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("MaterialManager: cannot open '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────

void MaterialManager::Init(RHI::IRHIDevice*             device,
                            Resource::ResourceManager*   resMgr) {
    m_device         = device;
    m_defaultTexture = resMgr->GetBuiltin(Resource::BuiltinTexture::White1x1);
    m_textureHeap.Init(device, m_defaultTexture);
}

void MaterialManager::Shutdown() {
    m_cachedInstances.clear();
    m_types.clear();  // ShaderPrograms owned by ProgramCache (Issue #86)
    m_textureHeap.Shutdown();
    m_ringBuffer = {};
    m_device     = nullptr;
}

void MaterialManager::SetMaterialParamRingBuffer(RHI::RHIBufferHandle ringBuf) {
    m_ringBuffer = ringBuf;
}

// Issue #72: wires set=1 binding=0 of an SSBO-path instance to the shared
// MaterialParamRing buffer. baseOffset=0 + dynamic offset (per draw) gives the
// final blob location each frame; range = type->uboSize is the slot upper bound.
void MaterialManager::WireSSBODescriptor(MaterialInstance& inst) const {
    if (!inst.m_type || !inst.m_type->usesMaterialParamsSSBO) return;
    if (!m_ringBuffer.IsValid() || !inst.m_descSet.IsValid())  return;
    const uint64_t range =
        inst.m_type->uboSize > 0 ? static_cast<uint64_t>(inst.m_type->uboSize) : 16ull;
    m_device->WriteDescriptorBuffer(inst.m_descSet, 0, m_ringBuffer, 0, range, /*dynamic=*/true);
}

void MaterialManager::ClearProjectInstances() {
    SA_LOG_INFO("MaterialManager: cleared {} cached material instance(s)", m_cachedInstances.size());
    m_cachedInstances.clear();
}

bool MaterialManager::EvictInstance(const AssetID& id) {
    auto it = m_cachedInstances.find(HashID(id));
    if (it == m_cachedInstances.end()) return false;
    // ~MaterialInstance frees its desc set / UBO through the RHI deferred-destroy
    // queue — no WaitIdle (which would flush the queue mid-frame, invalidating
    // the in-recording command buffer).
    m_cachedInstances.erase(it);
    SA_LOG_INFO("MaterialManager: evicted cached instance {}", id.ToString());
    return true;
}

void MaterialManager::ClearProjectTypes() {
    std::vector<std::string> toRemove;
    for (auto& [name, type] : m_types) {
        if (type->isProjectType)
            toRemove.push_back(name);  // ShaderProgram freed by ProgramCache::ClearProjectPrograms
    }
    for (const auto& name : toRemove)
        m_types.erase(name);
    if (!toRemove.empty())
        SA_LOG_INFO("MaterialManager: cleared {} project material type(s)", toRemove.size());
}

MaterialType* MaterialManager::RegisterType(std::unique_ptr<MaterialType> type) {
    SA_LOG_INFO("MaterialManager: registered type '{}'", type->name);
    auto* ptr = type.get();
    m_types[type->name] = std::move(type);
    return ptr;
}

bool MaterialManager::RegisterTypeFromShaders(const MaterialTypeDesc&   desc,
                                               const FeatureInitContext& ctx,
                                               bool                      isProjectType)
{
    auto type  = std::make_unique<MaterialType>();
    type->name = desc.name;

    // ShaderProgram (vert+frag + merged reflection) is owned by ProgramCache (#86),
    // keyed by type name; GetGraphics handles SPV/refl load + the engine-dir fallback.
    // We read the merged reflection back from it for parameter/texture extraction —
    // no second .refl load here.
    type->shader = ctx.programs->GetGraphics(desc.name, desc.vertShader, desc.fragShader,
                                             ctx.shaderDir, ctx.engineShaderDir, isProjectType);
    if (!type->shader) {
        SA_LOG_ERROR("MaterialManager: '{}' — shader program load failed", desc.name);
        return false;
    }

    // Issue #108: skinned pipeline variant — same fragment, "<vert>_skinned"
    // vertex twin. Probed quietly (fs check, not GetGraphics) so vert stems
    // without a twin (fullscreen passes etc.) don't log load errors. Custom
    // .saglsl models use @VertShader deferred_geometry, whose twin lives in
    // the engine shader dir — resolved via the fallback path.
    if (!desc.noVertexInput) {
        const std::string skinnedStem = desc.vertShader + "_skinned";
        namespace fs = std::filesystem;
        const bool haveTwin =
            fs::exists(fs::path(ctx.shaderDir)       / (skinnedStem + ".vert.spv")) ||
            (!ctx.engineShaderDir.empty() &&
             fs::exists(fs::path(ctx.engineShaderDir) / (skinnedStem + ".vert.spv")));
        if (haveTwin)
            type->skinnedShader = ctx.programs->GetGraphics(
                desc.name + ":skinned", skinnedStem, desc.fragShader,
                ctx.shaderDir, ctx.engineShaderDir, isProjectType);
    }
    const RHI::ShaderReflection& merged = type->shader->GetMergedReflection();

    if (auto ubo = merged.FindBinding(2, 0)) {
        // Issue #72 Step 6.5: SSBO at set=2 binding=0 named "MaterialParams" → new path.
        // Block members named with `_Idx` suffix (uint = 4B) carry bindless texture
        // indices; everything else is a regular parameter.
        const bool isSSBO = (ubo->type == RHI::RHIDescriptorType::StorageBuffer &&
                             ubo->name == "MaterialParams");
        type->usesMaterialParamsSSBO = isSSBO;
        type->uboSize = ubo->blockSize;

        for (const auto& m : ubo->members) {
            if (!m.name.empty() && m.name[0] == '_') continue; // skip padding fields

            const bool isTextureIdx =
                isSSBO &&
                m.size == 4 &&
                m.name.size() > 4 &&
                m.name.compare(m.name.size() - 4, 4, "_Idx") == 0;

            if (isTextureIdx) {
                TextureDef td;
                // Strip "_Idx" suffix so .samat texture maps keep using logical names
                // like "t_BaseColor" rather than "t_BaseColor_Idx".
                td.name           = m.name.substr(0, m.name.size() - 4);
                td.binding        = 0;             // unused in SSBO path
                td.slotIndex      = static_cast<uint32_t>(type->textures.size());
                td.uboBlobOffset  = m.offset;
                td.displayName    = m.displayName;
                type->textures.push_back(std::move(td));
            } else {
                ParamDef pd;
                pd.name        = m.name;
                pd.offset      = m.offset;
                pd.size        = m.size;
                pd.uiType      = m.uiType;
                pd.displayName = m.displayName;
                pd.minValue    = m.minValue;
                pd.maxValue    = m.maxValue;
                std::copy(std::begin(m.defaultValue), std::end(m.defaultValue),
                          std::begin(pd.defaultValue));
                type->params.push_back(std::move(pd));
            }
        }
    }

    // Legacy UBO path: gather sampler textures from set=1 binding>=1.
    // SSBO path already populated textures from block members above.
    if (!type->usesMaterialParamsSSBO) {
        for (const auto& b : merged.bindings) {
            if (b.set != 2 || b.binding == 0) continue;
            if (b.type == RHI::RHIDescriptorType::Texture2D   ||
                b.type == RHI::RHIDescriptorType::TextureCube  ||
                b.type == RHI::RHIDescriptorType::Sampler) {
                TextureDef td;
                td.name        = b.name;
                td.binding     = b.binding;
                td.slotIndex   = static_cast<uint32_t>(type->textures.size());
                td.displayName = b.displayName;
                type->textures.push_back(std::move(td));
            }
        }
        std::sort(type->textures.begin(), type->textures.end(),
                  [](const auto& a, const auto& b){ return a.binding < b.binding; });
        for (uint32_t i = 0; i < type->textures.size(); ++i)
            type->textures[i].slotIndex = i;
    }

    type->defaultCullMode   = desc.cullMode;
    type->defaultBlendMode  = desc.blendMode;
    type->defaultTopology   = desc.topology;
    type->defaultDepthTest  = desc.depthTest;
    type->defaultDepthWrite = desc.depthWrite;
    type->noVertexInput     = desc.noVertexInput;

    RegisterType(std::move(type));
    return true;
}

bool MaterialManager::LoadShaderProgram(ShaderProgram& prog,
                                         const std::string& vertStem,
                                         const std::string& fragStem,
                                         const FeatureInitContext& ctx)
{
    const auto vertSpv = LoadSpv(ctx.shaderDir + "/" + vertStem + ".vert.spv");
    const auto fragSpv = LoadSpv(ctx.shaderDir + "/" + fragStem + ".frag.spv");
    if (vertSpv.empty() || fragSpv.empty()) {
        SA_LOG_ERROR("MaterialManager::LoadShaderProgram: .spv not found ({} / {})",
                     vertStem, fragStem);
        return false;
    }

    RHI::ShaderReflection vertRefl, fragRefl;
    if (!RHI::ShaderReflectionIO::LoadFromFile(ctx.shaderDir + "/" + vertStem + ".vert.refl", vertRefl) ||
        !RHI::ShaderReflectionIO::LoadFromFile(ctx.shaderDir + "/" + fragStem + ".frag.refl", fragRefl)) {
        SA_LOG_ERROR("MaterialManager::LoadShaderProgram: .refl not found ({} / {})",
                     vertStem, fragStem);
        return false;
    }

    ShaderProgram::Desc pd;
    pd.vertSpv        = vertSpv;  pd.vertRefl = vertRefl;
    pd.fragSpv        = fragSpv;  pd.fragRefl = fragRefl;
    pd.frameLayout    = ctx.frameLayout;
    pd.bindlessLayout = m_textureHeap.GetLayout();
    if (!prog.Load(ctx.device, pd)) {
        SA_LOG_ERROR("MaterialManager::LoadShaderProgram: program load failed ({} / {})",
                     vertStem, fragStem);
        return false;
    }
    return true;
}

void MaterialManager::RegisterTypesFromShaderDir(const std::string&        shaderDir,
                                                   const FeatureInitContext& ctx,
                                                   bool                      isProjectType)
{
    namespace fs = std::filesystem;
    if (!fs::is_directory(shaderDir)) return;

    for (const auto& entry : fs::directory_iterator(shaderDir)) {
        const fs::path& p = entry.path();
        // Match *.gbuffer.frag.refl — the cooked fragment refl for .saglsl shaders.
        if (p.extension() != ".refl") continue;
        const fs::path stem1 = p.stem();             // *.gbuffer.frag
        if (stem1.extension() != ".frag") continue;
        const fs::path stem2 = stem1.stem();          // *.gbuffer
        if (stem2.extension().string() != ".gbuffer") continue;

        RHI::ShaderReflection fragRefl;
        if (!RHI::ShaderReflectionIO::LoadFromFile(p, fragRefl)) continue;
        const std::string shadingModel = fragRefl.GetMeta("shadingModel");
        if (shadingModel.empty()) continue;   // builtin shader, not a .saglsl type

        // Skip already-registered types (builtin or previously scanned).
        if (GetType(shadingModel)) continue;

        const std::string fragStem  = stem2.string();  // e.g. "simple_albedo.gbuffer"
        const std::string vertMeta  = fragRefl.GetMeta("vertShader");
        const std::string vertName  = vertMeta.empty() ? "deferred_geometry" : vertMeta;

        SA_LOG_INFO("MaterialManager: auto-registering '{}' from .refl", shadingModel);
        if (RegisterTypeFromShaders({shadingModel, vertName, fragStem}, ctx, isProjectType)) {
            if (isProjectType) {
                if (auto* type = GetType(shadingModel))
                    type->isProjectType = true;
            }
        }
    }
}

MaterialType* MaterialManager::GetType(const std::string& name) const {
    auto it = m_types.find(name);
    return it != m_types.end() ? it->second.get() : nullptr;
}

MaterialInstance*
MaterialManager::LoadMaterial(const AssetID& id, Resource::ResourceManager& resMgr) {
    if (!id.IsValid()) return nullptr;

    const uint64_t key = HashID(id);
    auto it = m_cachedInstances.find(key);
    if (it != m_cachedInstances.end()) return it->second.get();

    auto pathOpt = Resource::VFS::ResolveCookedPath(id, ".samatc");
    if (!pathOpt) {
        SA_LOG_ERROR("MaterialManager::LoadMaterial — .samatc not found for {}", id.ToString());
        return nullptr;
    }

    // Parse JSON.
    nlohmann::json root;
    {
        std::ifstream f(pathOpt->string());
        if (!f) {
            SA_LOG_ERROR("MaterialManager::LoadMaterial — cannot open '{}'",
                         pathOpt->string());
            return nullptr;
        }
        try { root = nlohmann::json::parse(f); }
        catch (const nlohmann::json::exception& ex) {
            SA_LOG_ERROR("MaterialManager::LoadMaterial — JSON error in '{}': {}",
                         pathOpt->filename().string(), ex.what());
            return nullptr;
        }
    }

    const std::string typeName = root.value("type", "PBR");
    auto inst = CreateInstance(typeName);
    if (!inst) return nullptr;

    // ── Pipeline-state fields (Issue #56) — missing fields = opaque legacy asset ──
    {
        const std::string am = root.value("alphaMode", "OPAQUE");
        MaterialRenderState rs;
        if      (am == "MASK")  rs.alphaMode = AlphaMode::Mask;
        else if (am == "BLEND") rs.alphaMode = AlphaMode::Blend;
        rs.doubleSided = root.value("doubleSided", false);

        // Mask/Blend rely on the shared MaterialParams SSBO layout (prepass /
        // forward frags reuse the per-draw blob) — legacy-UBO types can't take
        // that path, so they render opaque.
        if (rs.alphaMode != AlphaMode::Opaque &&
            !inst->GetType()->usesMaterialParamsSSBO) {
            SA_LOG_WARN("MaterialManager: material {} ({}) requests alphaMode={} "
                        "but type is legacy-UBO — falling back to OPAQUE",
                        id.ToString(), typeName, am);
            rs.alphaMode = AlphaMode::Opaque;
        }
        inst->m_renderState = rs;
        // Absent keys = inherit: BuildDrawList falls through to the layer below.
        inst->m_alphaModeAuthored   = root.contains("alphaMode");
        inst->m_doubleSidedAuthored = root.contains("doubleSided");
    }

    // ── Apply scalar params (type-driven from reflection metadata) ────────────
    if (root.contains("params")) {
        const auto& p = root["params"];
        const MaterialType* mtype = GetType(typeName);
        if (mtype) {
            for (const auto& param : mtype->params) {
                if (!p.contains(param.name)) continue;
                inst->m_authoredParams.insert(param.name);
                const auto& val = p[param.name];

                // Dispatch on uiType; fall back to member size for Inferred.
                using T = RHI::ParamUIType;
                const T uit = param.uiType;
                const bool is4  = (uit == T::Color4 || uit == T::Vec4) ||
                                  (uit == T::Inferred && param.size == 16);
                const bool is3  = (uit == T::Color3 || uit == T::Vec3) ||
                                  (uit == T::Inferred && param.size == 12);
                const bool is2  = (uit == T::Vec2) ||
                                  (uit == T::Inferred && param.size == 8);

                if (is4)
                    inst->SetParam<glm::vec4>(param.name,
                        {val[0], val[1], val[2], val[3]});
                else if (is3)
                    inst->SetParam<glm::vec3>(param.name,
                        {val[0], val[1], val[2]});
                else if (is2)
                    inst->SetParam<glm::vec2>(param.name,
                        {val[0], val[1]});
                else
                    inst->SetParam<float>(param.name, val.get<float>());
            }
        }
    }

    // ── Bind textures ─────────────────────────────────────────────────────────
    auto loadTex = [&](const std::string& uuidStr) -> RHI::RHITextureHandle {
        if (uuidStr.empty()) return m_defaultTexture;
        const AssetID texID = AssetID::FromString(uuidStr);
        if (!texID.IsValid()) return m_defaultTexture;
        auto h = resMgr.LoadTexture(texID);
        return h.IsValid() ? h : m_defaultTexture;
    };

    if (root.contains("textures")) {
        const auto& t = root["textures"];
        for (const auto& [name, uuidVal] : t.items()) {
            const std::string uuidStr = uuidVal.get<std::string>();
            inst->SetTexture(name, loadTex(uuidStr));
            // An empty/invalid uuid is an unassigned editor placeholder, not an
            // authored "no texture" — it inherits like an absent key.
            if (AssetID::FromString(uuidStr).IsValid())
                inst->m_authoredTextures.insert(name);
        }
    }

    SA_LOG_INFO("MaterialManager: loaded material {}", id.ToString());

    auto* raw = inst.get();
    m_cachedInstances.emplace(key, std::move(inst));
    return raw;
}

std::unique_ptr<MaterialInstance>
MaterialManager::CloneInstance(const MaterialInstance* src) const {
    if (!src) return nullptr;
    auto clone = src->m_type->CreateInstance(m_device, m_defaultTexture);
    if (!clone) return nullptr;
    clone->m_mgr = const_cast<MaterialManager*>(this);
    WireSSBODescriptor(*clone);
    // Copy the UBO parameter blob wholesale — same layout, same values.
    clone->m_uboBlob             = src->m_uboBlob;
    clone->m_renderState         = src->m_renderState;
    clone->m_authoredParams      = src->m_authoredParams;
    clone->m_authoredTextures    = src->m_authoredTextures;
    clone->m_alphaModeAuthored   = src->m_alphaModeAuthored;
    clone->m_doubleSidedAuthored = src->m_doubleSidedAuthored;
    clone->m_paramDirty          = true;
    if (src->m_type->usesMaterialParamsSSBO) {
        clone->m_texAssetIndices = src->m_texAssetIndices;
    } else {
        // Re-bind each texture slot from the source (SetTexture writes the descriptor).
        for (const auto& td : src->m_type->textures) {
            if (td.slotIndex < src->m_textures.size()) {
                const auto tex = src->m_textures[td.slotIndex];
                if (tex.IsValid())
                    clone->SetTexture(td.name, tex);
            }
        }
    }
    return clone;
}

std::unique_ptr<MaterialInstance>
MaterialManager::CreateInstance(const std::string& typeName) const {
    MaterialType* type = GetType(typeName);
    if (!type) {
        SA_LOG_ERROR("MaterialManager::CreateInstance: unknown type '{}'", typeName);
        return nullptr;
    }
    auto inst = type->CreateInstance(m_device, m_defaultTexture);
    if (inst) {
        inst->m_mgr = const_cast<MaterialManager*>(this);
        WireSSBODescriptor(*inst);
    }
    return inst;
}

} // namespace StellarAlia
