#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>

#include "core/asset/AssetID.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/RHITypes.hpp"
#include "resource/types/ImageData.hpp"
#include "resource/types/AnimData.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "resource/cook/CookedAnim.hpp"
#include <array>
#include <glm/glm.hpp>

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// BuiltinTexture — well-known procedural textures created at Init time.
// ─────────────────────────────────────────────────────────────────────────────
enum class BuiltinTexture {
    White1x1,   // 1×1 RGBA8 0xFFFFFFFF — default sampler slot fill
};

// ─────────────────────────────────────────────────────────────────────────────
// GPUMesh — GPU-side buffers for a loaded mesh.
// Holds vertex buffer, index buffer, and per-submesh draw metadata.
// ─────────────────────────────────────────────────────────────────────────────
struct GPUSubMesh {
    uint32_t  firstIndex    = 0;
    uint32_t  indexCount    = 0;
    int32_t   vertexOffset  = 0;
    int32_t   materialIndex = -1;
    glm::mat4 localTransform = glm::mat4(1.0f);  // node world transform from cook

    // v4: UUID of the .samat asset for this submesh (invalid when no material)
    AssetID   defaultMaterialID;

    // Mesh-local AABB computed from vertex positions at load time.
    glm::vec3 boundsMin = glm::vec3( 1e30f);
    glm::vec3 boundsMax = glm::vec3(-1e30f);
};

struct GPUMesh {
    RHI::RHIBufferHandle        vertexBuffer;
    RHI::RHIBufferHandle        indexBuffer;
    RHI::RHIBufferHandle        skinDataBuffer;  // joints+weights SSBO; invalid for static meshes
    std::vector<GPUSubMesh>     subMeshes;
    uint32_t                    vertexCount = 0;
    uint32_t                    indexCount  = 0;
    bool IsValid()   const { return vertexBuffer.IsValid() && indexBuffer.IsValid(); }
    bool IsSkinned() const { return skinDataBuffer.IsValid(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// ResourceManager
//
// Loads cooked assets from the cook cache into GPU memory via IRHIDevice.
// Caches loaded handles by AssetID — requesting the same asset twice returns
// the cached handle.
//
// Ownership: ResourceManager owns the GPU handles. Call Shutdown() before
// destroying the IRHIDevice.
// ─────────────────────────────────────────────────────────────────────────────
class ResourceManager {
public:
    ResourceManager() = default;
    ~ResourceManager() = default;

    // Initialize: set the engine cook cache root and the device to upload through.
    // Must be called before any Load*() methods.
    void Init(const std::filesystem::path& engineCookCacheDir, RHI::IRHIDevice* device);

    // Update the active project's cook cache. Call when the user switches projects.
    // VFS will check this path before the engine cache when resolving assets.
    void SetProjectCookCache(const std::filesystem::path& projectCookCacheDir);

    // Release all project-specific GPU and CPU caches without destroying builtins.
    // Calls WaitIdle internally — safe to call mid-frame during a project switch.
    // Preserves m_white1x1 and m_fileTextures (engine/editor-level resources).
    void ClearProjectAssets();

    // Destroy all GPU resources. Call before the device is destroyed.
    void Shutdown();

    // Load a texture from the cook cache onto the GPU.
    // Returns an invalid handle on failure.
    [[nodiscard]] RHI::RHITextureHandle LoadTexture(const AssetID& id);

    // Load a mesh (vertex + index buffers) from the cook cache onto the GPU.
    // Returns nullptr on failure (mesh is not cached on failure).
    [[nodiscard]] const GPUMesh* LoadMesh(const AssetID& id);

    // Load an RGBA32F .satex as CPU-side float data (for HDR panoramas).
    // Returns nullopt if the asset is not found or is not RGBA32F format.
    // Does NOT upload to the GPU — use LoadTexture() for that.
    [[nodiscard]] std::optional<ImageData> LoadHDRImageData(const AssetID& id);

    // Load 9 SH coefficients from a .sash9 file in the cook cache.
    // Returns nullopt if the asset is not found or the file is malformed.
    [[nodiscard]] std::optional<std::array<glm::vec4, 9>> LoadSH9Coeffs(const AssetID& id);

    // Load an arbitrary LDR image (PNG / JPG / TGA / BMP) directly from a filesystem path.
    // Uses canonical path as cache key — loading the same file twice returns the cached handle.
    // The handle is owned by ResourceManager and destroyed in Shutdown().
    // Returns an invalid handle on failure (error is logged).
    [[nodiscard]] RHI::RHITextureHandle LoadTextureFromFile(const std::filesystem::path& path);

    // Return a handle to a procedural built-in texture.
    // The handle is owned by ResourceManager; do not destroy it.
    [[nodiscard]] RHI::RHITextureHandle GetBuiltin(BuiltinTexture which) const;

    // Load a skeleton (bone hierarchy + inverse bind matrices) from a .saskelc file.
    // CPU-side data only — no GPU upload. Returns nullptr on failure.
    [[nodiscard]] const CookedSkeleton* LoadSkeleton(const AssetID& id);

    // Load an animation clip from a .saanim file.
    // CPU-side data only — no GPU upload. Returns nullptr on failure.
    [[nodiscard]] const CookedAnim* LoadAnimClip(const AssetID& id);

    // Load the cooked mesh data (CPU-side) without uploading to GPU.
    // Used by the animation system to access rest-pose vertices and skin data.
    // Returns nullptr on failure.
    [[nodiscard]] const CookedMesh* LoadMeshData(const AssetID& id);

private:
    RHI::IRHIDevice*   m_device = nullptr;

    RHI::RHITextureHandle m_white1x1;   // created in Init, destroyed in Shutdown

    std::unordered_map<uint64_t, RHI::RHITextureHandle> m_textures;
    std::unordered_map<uint64_t, GPUMesh>               m_meshes;
    // File-path-keyed textures loaded via LoadTextureFromFile().
    std::unordered_map<std::size_t, RHI::RHITextureHandle> m_fileTextures;

    // CPU-only caches (no GPU memory; lifetime = ResourceManager lifetime).
    std::unordered_map<uint64_t, CookedSkeleton> m_skeletons;
    std::unordered_map<uint64_t, CookedAnim>     m_animClips;
    std::unordered_map<uint64_t, CookedMesh>     m_cookedMeshes;

    // Simple hash: XOR the two halves of the UUID (sufficient for cache key).
    static uint64_t HashID(const AssetID& id) { return id.hi ^ id.lo; }
};

} // namespace StellarAlia::Resource
