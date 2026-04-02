#include "function/material/MaterialManager.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/vfs/VFS.hpp"
#include "core/logs/Log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace StellarAlia {

void MaterialManager::Init(RHI::IRHIDevice*      device,
                            RHI::RHITextureHandle defaultTexture) {
    m_device         = device;
    m_defaultTexture = defaultTexture;
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
MaterialManager::CreateInstance(const std::string& typeName) const {
    MaterialType* type = GetType(typeName);
    if (!type) {
        SA_LOG_ERROR("MaterialManager::CreateInstance: unknown type '{}'", typeName);
        return nullptr;
    }
    return type->CreateInstance(m_device, m_defaultTexture);
}

} // namespace StellarAlia
