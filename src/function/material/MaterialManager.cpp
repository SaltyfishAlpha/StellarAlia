#include "function/material/MaterialManager.hpp"
#include "function/renderer/RenderFeature.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"
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

MaterialType* MaterialManager::RegisterType(std::unique_ptr<MaterialType> type) {
    SA_LOG_INFO("MaterialManager: registered type '{}'", type->name);
    auto* ptr = type.get();
    m_types[type->name] = std::move(type);
    return ptr;
}

bool MaterialManager::RegisterTypeFromShaders(const MaterialTypeDesc&   desc,
                                               const FeatureInitContext& ctx)
{
    const std::string vertSpvPath  = ctx.shaderDir + "/" + desc.vertShader + ".vert.spv";
    const std::string fragSpvPath  = ctx.shaderDir + "/" + desc.fragShader + ".frag.spv";
    const std::string vertReflPath = ctx.shaderDir + "/" + desc.vertShader + ".vert.refl";
    const std::string fragReflPath = ctx.shaderDir + "/" + desc.fragShader + ".frag.refl";

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
        for (const auto& m : ubo->members)
            type->params.push_back({m.name, m.offset, m.size});
    }

    for (const auto& b : merged.bindings) {
        if (b.set != 1) continue;
        if (b.type == RHI::RHIDescriptorType::Texture2D   ||
            b.type == RHI::RHIDescriptorType::TextureCube  ||
            b.type == RHI::RHIDescriptorType::Sampler)
            type->textures.push_back({b.name, b.binding,
                                      static_cast<uint32_t>(type->textures.size())});
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

MaterialType* MaterialManager::GetType(const std::string& name) const {
    auto it = m_types.find(name);
    return it != m_types.end() ? it->second.get() : nullptr;
}

MaterialInstance*
MaterialManager::LoadMaterial(const AssetID& id,
                               const std::filesystem::path& cookCacheDir,
                               Resource::ResourceManager& resMgr) {
    if (!id.IsValid()) return nullptr;

    const uint64_t key = HashID(id);
    auto it = m_cachedInstances.find(key);
    if (it != m_cachedInstances.end()) return it->second.get();

    // Resolve .samat path from cook cache.
    Resource::VFS::SetCookCacheDir(cookCacheDir);
    auto pathOpt = Resource::VFS::ResolveCookedPath(id, ".samat");
    if (!pathOpt) {
        SA_LOG_ERROR("MaterialManager::LoadMaterial — .samat not found for {}", id.ToString());
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

    // ── Apply scalar params ───────────────────────────────────────────────────
    if (root.contains("params")) {
        const auto& p = root["params"];

        if (p.contains("baseColorFactor")) {
            const auto& a = p["baseColorFactor"];
            glm::vec4 v{a[0], a[1], a[2], a[3]};
            inst->SetParam<glm::vec4>("baseColorFactor", v);
        }
        if (p.contains("roughnessFactor"))
            inst->SetParam<float>("roughnessFactor", p["roughnessFactor"].get<float>());
        if (p.contains("metallicFactor"))
            inst->SetParam<float>("metallicFactor",  p["metallicFactor"].get<float>());
        if (p.contains("normalScale"))
            inst->SetParam<float>("normalScale",     p["normalScale"].get<float>());
        if (p.contains("occlusionStrength"))
            inst->SetParam<float>("occlusionStrength", p["occlusionStrength"].get<float>());
        if (p.contains("emissiveFactor")) {
            const auto& a = p["emissiveFactor"];
            glm::vec3 v{a[0], a[1], a[2]};
            inst->SetParam<glm::vec3>("emissiveFactor", v);
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
