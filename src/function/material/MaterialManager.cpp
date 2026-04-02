#include "function/material/MaterialManager.hpp"
#include "core/logs/Log.hpp"

namespace StellarAlia {

void MaterialManager::Init(RHI::IRHIDevice*      device,
                            RHI::RHITextureHandle defaultTexture) {
    m_device         = device;
    m_defaultTexture = defaultTexture;
}

void MaterialManager::Shutdown() {
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
