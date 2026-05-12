#include "function/material/MaterialManager.hpp"
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
}

void MaterialManager::Shutdown() {
    m_cachedInstances.clear();
    for (auto& [name, type] : m_types)
        type->shader.Unload(m_device);
    m_types.clear();
    m_device = nullptr;
}

void MaterialManager::ClearProjectInstances() {
    SA_LOG_INFO("MaterialManager: cleared {} cached material instance(s)", m_cachedInstances.size());
    m_cachedInstances.clear();
}

void MaterialManager::ClearProjectTypes() {
    std::vector<std::string> toRemove;
    for (auto& [name, type] : m_types) {
        if (type->isProjectType) {
            type->shader.Unload(m_device);
            toRemove.push_back(name);
        }
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
                                               const FeatureInitContext& ctx)
{
    // Project material types may reference vert/frag SPV that live in the engine
    // builtin dir (e.g. deferred_geometry.vert), so resolve each file by trying
    // ctx.shaderDir first then falling back to ctx.engineShaderDir.
    auto resolve = [&](const std::string& rel) {
        const std::string primary = ctx.shaderDir + "/" + rel;
        if (std::filesystem::exists(primary)) return primary;
        if (!ctx.engineShaderDir.empty() && ctx.engineShaderDir != ctx.shaderDir) {
            const std::string fallback = ctx.engineShaderDir + "/" + rel;
            if (std::filesystem::exists(fallback)) return fallback;
        }
        return primary;  // let the caller report the missing primary path
    };
    const std::string vertSpvPath  = resolve(desc.vertShader + ".vert.spv");
    const std::string fragSpvPath  = resolve(desc.fragShader + ".frag.spv");
    const std::string vertReflPath = resolve(desc.vertShader + ".vert.refl");
    const std::string fragReflPath = resolve(desc.fragShader + ".frag.refl");

    const auto vertSpv = LoadSpv(vertSpvPath);
    const auto fragSpv = LoadSpv(fragSpvPath);
    if (vertSpv.empty() || fragSpv.empty()) {
        SA_LOG_ERROR("MaterialManager: '{}' — shader .spv not found", desc.name);
        return false;
    }

    RHI::ShaderReflection vertRefl, fragRefl;
    if (!RHI::ShaderReflectionIO::LoadFromFile(vertReflPath, vertRefl) ||
        !RHI::ShaderReflectionIO::LoadFromFile(fragReflPath, fragRefl)) {
        SA_LOG_ERROR("MaterialManager: '{}' — .refl files not found", desc.name);
        return false;
    }

    const RHI::ShaderReflection merged = RHI::MergeReflections(vertRefl, fragRefl);

    auto type  = std::make_unique<MaterialType>();
    type->name = desc.name;

    if (auto ubo = merged.FindBinding(1, 0)) {
        type->uboSize = ubo->blockSize;
        for (const auto& m : ubo->members) {
            if (!m.name.empty() && m.name[0] == '_') continue; // skip padding fields
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

    for (const auto& b : merged.bindings) {
        if (b.set != 1) continue;
        if (b.type == RHI::RHIDescriptorType::Texture2D   ||
            b.type == RHI::RHIDescriptorType::TextureCube  ||
            b.type == RHI::RHIDescriptorType::Sampler)
            type->textures.push_back({b.name, b.binding,
                                      static_cast<uint32_t>(type->textures.size()),
                                      b.displayName});
    }
    std::sort(type->textures.begin(), type->textures.end(),
              [](const auto& a, const auto& b){ return a.binding < b.binding; });
    for (uint32_t i = 0; i < type->textures.size(); ++i)
        type->textures[i].slotIndex = i;

    type->defaultCullMode   = desc.cullMode;
    type->defaultBlendMode  = desc.blendMode;
    type->defaultTopology   = desc.topology;
    type->defaultDepthTest  = desc.depthTest;
    type->defaultDepthWrite = desc.depthWrite;
    type->noVertexInput     = desc.noVertexInput;

    ShaderProgram::Desc pd;
    pd.vertSpv     = vertSpv;  pd.vertRefl = vertRefl;
    pd.fragSpv     = fragSpv;  pd.fragRefl = fragRefl;
    pd.frameLayout = ctx.frameLayout;
    if (!type->shader.Load(ctx.device, pd)) {
        SA_LOG_ERROR("MaterialManager: '{}' — shader program load failed", desc.name);
        return false;
    }

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
    pd.vertSpv     = vertSpv;  pd.vertRefl = vertRefl;
    pd.fragSpv     = fragSpv;  pd.fragRefl = fragRefl;
    pd.frameLayout = ctx.frameLayout;
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
        if (fragRefl.shadingModel.empty()) continue;   // builtin shader, not a .saglsl type

        // Skip already-registered types (builtin or previously scanned).
        if (GetType(fragRefl.shadingModel)) continue;

        const std::string fragStem = stem2.string();  // e.g. "simple_albedo.gbuffer"
        const std::string vertName = fragRefl.vertShader.empty()
                                         ? "deferred_geometry"
                                         : fragRefl.vertShader;

        SA_LOG_INFO("MaterialManager: auto-registering '{}' from .refl",
                    fragRefl.shadingModel);
        if (RegisterTypeFromShaders({fragRefl.shadingModel, vertName, fragStem}, ctx)) {
            if (isProjectType) {
                if (auto* type = GetType(fragRefl.shadingModel))
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

    // ── Apply scalar params (type-driven from reflection metadata) ────────────
    if (root.contains("params")) {
        const auto& p = root["params"];
        const MaterialType* mtype = GetType(typeName);
        if (mtype) {
            for (const auto& param : mtype->params) {
                if (!p.contains(param.name)) continue;
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
        for (const auto& [name, uuidVal] : t.items())
            inst->SetTexture(name, loadTex(uuidVal.get<std::string>()));
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
    // Copy the UBO parameter blob wholesale — same layout, same values.
    clone->m_uboBlob    = src->m_uboBlob;
    clone->m_paramDirty = true;
    // Re-bind each texture slot from the source (SetTexture writes the descriptor).
    for (const auto& td : src->m_type->textures) {
        if (td.slotIndex < src->m_textures.size()) {
            const auto tex = src->m_textures[td.slotIndex];
            if (tex.IsValid())
                clone->SetTexture(td.name, tex);
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
    return type->CreateInstance(m_device, m_defaultTexture);
}

} // namespace StellarAlia
