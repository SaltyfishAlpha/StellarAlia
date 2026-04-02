#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/asset/AssetID.hpp"
#include "function/material/MaterialType.hpp"
#include "function/material/MaterialInstance.hpp"

namespace StellarAlia::Resource { class ResourceManager; }

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// MaterialManager
//
// Central registry for MaterialTypes and factory for MaterialInstances.
// Lifetime must not exceed the owning IRHIDevice.
// ─────────────────────────────────────────────────────────────────────────────
class MaterialManager {
public:
    // Set the device and a 1×1 white fallback texture used for unset sampler slots.
    void Init(RHI::IRHIDevice*      device,
              RHI::RHITextureHandle defaultTexture);

    // Destroy all types and instances (pipelines/shaders are owned by the device
    // and destroyed separately; here we unload the ShaderPrograms).
    void Shutdown();

    // Register a fully configured MaterialType.
    // The manager takes ownership. Returns a raw pointer for immediate use.
    MaterialType* RegisterType(std::unique_ptr<MaterialType> type);

    // Find a registered type by name. Returns nullptr if not found.
    [[nodiscard]] MaterialType* GetType(const std::string& name) const;

    // Create a MaterialInstance from the named type.
    // Lifetime is managed by the caller.
    [[nodiscard]] std::unique_ptr<MaterialInstance>
    CreateInstance(const std::string& typeName) const;

    // Load a .samat asset by UUID, populate a MaterialInstance, and cache it.
    // Subsequent calls with the same AssetID return the cached instance.
    // Returns nullptr on failure (missing file, unknown type, etc.).
    // The returned pointer is owned by this manager — do not delete it.
    [[nodiscard]] MaterialInstance*
    LoadMaterial(const AssetID& id,
                 const std::filesystem::path& cookCacheDir,
                 Resource::ResourceManager& resMgr);

private:
    static uint64_t HashID(const AssetID& id) { return id.hi ^ id.lo; }

    RHI::IRHIDevice*      m_device         = nullptr;
    RHI::RHITextureHandle m_defaultTexture;
    std::unordered_map<std::string, std::unique_ptr<MaterialType>> m_types;
    // Instances loaded via LoadMaterial(), keyed by AssetID hash.
    std::unordered_map<uint64_t, std::unique_ptr<MaterialInstance>> m_cachedInstances;
};

} // namespace StellarAlia
