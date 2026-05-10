#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/asset/AssetID.hpp"
#include "function/material/MaterialType.hpp"
#include "function/material/MaterialInstance.hpp"
#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia::Resource { class ResourceManager; }
namespace StellarAlia { struct FeatureInitContext; }

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// MaterialTypeDesc — declarative description for RegisterTypeFromShaders.
//
// The feature author specifies only what they care about: shader filenames
// and render state. Reflection parsing, binding layout, shader compile, and
// type registration are all handled internally by MaterialManager.
// ─────────────────────────────────────────────────────────────────────────────
struct MaterialTypeDesc {
    std::string        name;
    std::string        vertShader;               // filename stem, e.g. "pbr" → pbr.vert.spv
    std::string        fragShader;               // filename stem, e.g. "pbr" → pbr.frag.spv
    RHI::RHICullMode   cullMode      = RHI::RHICullMode::Back;
    RHI::RHIBlendMode  blendMode     = RHI::RHIBlendMode::Opaque;
    RHI::RHITopology   topology      = RHI::RHITopology::TriangleList;
    bool               depthTest     = true;
    bool               depthWrite    = true;
    bool               noVertexInput = false;    // true for fullscreen-triangle passes
};

// ─────────────────────────────────────────────────────────────────────────────
// MaterialManager
//
// Central registry for MaterialTypes and factory for MaterialInstances.
// Lifetime must not exceed the owning IRHIDevice.
// ─────────────────────────────────────────────────────────────────────────────
class MaterialManager {
public:
    // Set the device and resource manager.
    // The white 1×1 fallback texture is obtained from resMgr as a builtin resource.
    void Init(RHI::IRHIDevice*                  device,
              Resource::ResourceManager*         resMgr);

    // Destroy all types and instances (pipelines/shaders are owned by the device
    // and destroyed separately; here we unload the ShaderPrograms).
    void Shutdown();

    // Release all cached MaterialInstances loaded from the active project.
    // Call after ResourceManager::ClearProjectAssets() (GPU must be idle).
    // Preserves registered MaterialTypes (engine-level, not project-specific).
    void ClearProjectInstances();

    // Unload and remove all MaterialTypes where isProjectType=true.
    // Call after ClearProjectInstances() when switching projects.
    void ClearProjectTypes();

    // Register a fully configured MaterialType.
    // The manager takes ownership. Returns a raw pointer for immediate use.
    MaterialType* RegisterType(std::unique_ptr<MaterialType> type);

    // Load shaders, parse reflection, build, and register a MaterialType in one call.
    // Returns false on shader load or compile failure.
    // This is the preferred path from RenderFeature::OnInit.
    bool RegisterTypeFromShaders(const MaterialTypeDesc& desc, const FeatureInitContext& ctx);

    // Load a ShaderProgram directly from shader stems (without registering a MaterialType).
    // Use for internal passes that need a ShaderProgram but not a full MaterialType, e.g. GPU skinning.
    // vertStem / fragStem follow the same naming convention as MaterialTypeDesc::vertShader / fragShader.
    // Returns false on any shader load or compile failure.
    bool LoadShaderProgram(ShaderProgram& prog,
                           const std::string& vertStem,
                           const std::string& fragStem,
                           const FeatureInitContext& ctx);

    // Scan shaderDir for *.gbuffer.frag.refl files that carry a shadingModel field
    // (written by ShaderCookTool for .saglsl-compiled shaders) and auto-register
    // each as a MaterialType. Already-registered types are silently skipped.
    // Set isProjectType=true when registering project-specific shading models
    // so ClearProjectTypes() can remove them on project switch.
    void RegisterTypesFromShaderDir(const std::string& shaderDir,
                                     const FeatureInitContext& ctx,
                                     bool isProjectType = false);

    // Find a registered type by name. Returns nullptr if not found.
    [[nodiscard]] MaterialType* GetType(const std::string& name) const;

    // Access the full type registry (read-only). Used by editor drawers to
    // enumerate all params without hardcoding material type names.
    [[nodiscard]] const std::unordered_map<std::string, std::unique_ptr<MaterialType>>&
    GetTypes() const { return m_types; }

    // Create a MaterialInstance from the named type.
    // Lifetime is managed by the caller.
    [[nodiscard]] std::unique_ptr<MaterialInstance>
    CreateInstance(const std::string& typeName) const;

    // Load a .samat asset by UUID, populate a MaterialInstance, and cache it.
    // Subsequent calls with the same AssetID return the cached instance.
    // Returns nullptr on failure (missing file, unknown type, etc.).
    // The returned pointer is owned by this manager — do not delete it.
    [[nodiscard]] MaterialInstance*
    LoadMaterial(const AssetID& id, Resource::ResourceManager& resMgr);

    // Return an independent copy of src with the same type, parameters, and
    // textures. The caller owns the returned instance.
    // Use this when per-entity parameter overrides are needed on top of a shared
    // cached instance (copy-on-write pattern).
    [[nodiscard]] std::unique_ptr<MaterialInstance>
    CloneInstance(const MaterialInstance* src) const;

private:
    static uint64_t HashID(const AssetID& id) { return id.hi ^ id.lo; }

    RHI::IRHIDevice*      m_device         = nullptr;
    RHI::RHITextureHandle m_defaultTexture;
    std::unordered_map<std::string, std::unique_ptr<MaterialType>> m_types;
    // Instances loaded via LoadMaterial(), keyed by AssetID hash.
    std::unordered_map<uint64_t, std::unique_ptr<MaterialInstance>> m_cachedInstances;
};

} // namespace StellarAlia
