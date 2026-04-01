#pragma once

#include <filesystem>
#include <unordered_map>

#include "core/asset/AssetID.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// GPUMesh — GPU-side buffers for a loaded mesh.
// Holds vertex buffer, index buffer, and per-submesh draw metadata.
// ─────────────────────────────────────────────────────────────────────────────
struct GPUSubMesh {
    uint32_t firstIndex   = 0;
    uint32_t indexCount   = 0;
    int32_t  vertexOffset = 0;
    int32_t  materialIndex = -1;
};

struct GPUMesh {
    RHI::RHIBufferHandle        vertexBuffer;
    RHI::RHIBufferHandle        indexBuffer;
    std::vector<GPUSubMesh>     subMeshes;
    uint32_t                    vertexCount = 0;
    uint32_t                    indexCount  = 0;
    bool IsValid() const { return vertexBuffer.IsValid() && indexBuffer.IsValid(); }
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

    // Initialize: set the cook cache root and the device to upload through.
    // Must be called before any Load*() methods.
    void Init(const std::filesystem::path& cookCacheDir, RHI::IRHIDevice* device);

    // Destroy all GPU resources. Call before the device is destroyed.
    void Shutdown();

    // Load a texture from the cook cache onto the GPU.
    // Returns an invalid handle on failure.
    [[nodiscard]] RHI::RHITextureHandle LoadTexture(const AssetID& id);

    // Load a mesh (vertex + index buffers) from the cook cache onto the GPU.
    // Returns nullptr on failure (mesh is not cached on failure).
    [[nodiscard]] const GPUMesh* LoadMesh(const AssetID& id);

private:
    RHI::IRHIDevice*   m_device = nullptr;

    std::unordered_map<uint64_t, RHI::RHITextureHandle> m_textures;
    std::unordered_map<uint64_t, GPUMesh>               m_meshes;

    // Simple hash: XOR the two halves of the UUID (sufficient for cache key).
    static uint64_t HashID(const AssetID& id) { return id.hi ^ id.lo; }
};

} // namespace StellarAlia::Resource
