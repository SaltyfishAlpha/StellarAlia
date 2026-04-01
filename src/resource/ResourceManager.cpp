#include "resource/ResourceManager.hpp"

#include "resource/vfs/VFS.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "platform/rhi/RHITypes.hpp"
#include "core/logs/Log.hpp"

namespace StellarAlia::Resource {

void ResourceManager::Init(const std::filesystem::path& cookCacheDir, RHI::IRHIDevice* device) {
    m_device = device;
    VFS::SetCookCacheDir(cookCacheDir);
    SA_LOG_INFO("ResourceManager: cook cache = {}", cookCacheDir.string());
}

void ResourceManager::Shutdown() {
    if (!m_device) return;

    for (auto& [hash, handle] : m_textures)
        if (handle.IsValid()) m_device->DestroyTexture(handle);
    m_textures.clear();

    for (auto& [hash, mesh] : m_meshes) {
        if (mesh.vertexBuffer.IsValid()) m_device->DestroyBuffer(mesh.vertexBuffer);
        if (mesh.indexBuffer.IsValid())  m_device->DestroyBuffer(mesh.indexBuffer);
    }
    m_meshes.clear();

    m_device = nullptr;
}

// ─── LoadTexture ─────────────────────────────────────────────────────────────

RHI::RHITextureHandle ResourceManager::LoadTexture(const AssetID& id) {
    if (!id.IsValid() || !m_device) return {};

    const uint64_t key = HashID(id);
    auto it = m_textures.find(key);
    if (it != m_textures.end()) return it->second;

    auto pathOpt = VFS::ResolveCookedPath(id, ".satex");
    if (!pathOpt) {
        SA_LOG_ERROR("ResourceManager::LoadTexture — .satex not found for {}",
                     id.ToString());
        return {};
    }

    CookedTexture cooked;
    if (!LoadCookedTexture(pathOpt->string(), cooked)) {
        SA_LOG_ERROR("ResourceManager::LoadTexture — failed to parse {}",
                     pathOpt->filename().string());
        return {};
    }

    // Map CookedTextureFormat → RHIFormat
    RHI::RHIFormat rhiFmt = RHI::RHIFormat::RGBA8_UNORM;
    switch (cooked.format) {
        case CookedTextureFormat::RGBA8:   rhiFmt = cooked.srgb
                                                   ? RHI::RHIFormat::RGBA8_SRGB
                                                   : RHI::RHIFormat::RGBA8_UNORM; break;
        case CookedTextureFormat::RGBA32F: rhiFmt = RHI::RHIFormat::RGBA32F;      break;
        default:
            SA_LOG_WARN("ResourceManager::LoadTexture — unsupported format ({}), falling back to RGBA8",
                        static_cast<uint32_t>(cooked.format));
            break;
    }

    RHI::RHITextureDesc texDesc{};
    texDesc.width     = cooked.width;
    texDesc.height    = cooked.height;
    texDesc.mipLevels = cooked.mipLevels;
    texDesc.format    = rhiFmt;
    texDesc.usage     = RHI::RHITextureUsage::Sampled;

    RHI::RHITextureHandle handle = m_device->CreateTexture(texDesc);
    if (!handle.IsValid()) {
        SA_LOG_ERROR("ResourceManager::LoadTexture — CreateTexture failed");
        return {};
    }

    // Upload mip 0 (single mip for now; additional mips uploaded in a later pass)
    const void* pixels = cooked.MipData(0);
    const size_t size  = cooked.MipSize(0);
    if (pixels && size > 0)
        m_device->UploadTextureData(handle, pixels, static_cast<uint64_t>(size));

    SA_LOG_INFO("ResourceManager: loaded texture {} ({}x{} mips={})",
                id.ToString(), cooked.width, cooked.height, cooked.mipLevels);

    m_textures[key] = handle;
    return handle;
}

// ─── LoadMesh ────────────────────────────────────────────────────────────────

const GPUMesh* ResourceManager::LoadMesh(const AssetID& id) {
    if (!id.IsValid() || !m_device) return nullptr;

    const uint64_t key = HashID(id);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end()) return &it->second;

    auto pathOpt = VFS::ResolveCookedPath(id, ".samesh");
    if (!pathOpt) {
        SA_LOG_ERROR("ResourceManager::LoadMesh — .samesh not found for {}",
                     id.ToString());
        return nullptr;
    }

    CookedMesh cooked;
    if (!LoadCookedMesh(pathOpt->string(), cooked)) {
        SA_LOG_ERROR("ResourceManager::LoadMesh — failed to parse {}",
                     pathOpt->filename().string());
        return nullptr;
    }

    // Upload vertex buffer (GPU-only)
    RHI::RHIBufferDesc vbDesc{};
    vbDesc.size       = cooked.vertexData.size();
    vbDesc.usage      = RHI::RHIBufferUsage::Vertex;
    vbDesc.cpuVisible = false;
    vbDesc.debugName  = "VB";

    RHI::RHIBufferHandle vb = m_device->CreateBuffer(vbDesc);
    if (!vb.IsValid()) {
        SA_LOG_ERROR("ResourceManager::LoadMesh — CreateBuffer(VB) failed");
        return nullptr;
    }
    m_device->UploadBufferData(vb, cooked.vertexData.data(),
                               cooked.vertexData.size(), 0);

    // Upload index buffer (GPU-only)
    RHI::RHIBufferDesc ibDesc{};
    ibDesc.size       = cooked.indexData.size();
    ibDesc.usage      = RHI::RHIBufferUsage::Index;
    ibDesc.cpuVisible = false;
    ibDesc.debugName  = "IB";

    RHI::RHIBufferHandle ib = m_device->CreateBuffer(ibDesc);
    if (!ib.IsValid()) {
        m_device->DestroyBuffer(vb);
        SA_LOG_ERROR("ResourceManager::LoadMesh — CreateBuffer(IB) failed");
        return nullptr;
    }
    m_device->UploadBufferData(ib, cooked.indexData.data(),
                               cooked.indexData.size(), 0);

    // Build GPUMesh
    GPUMesh gpu;
    gpu.vertexBuffer = vb;
    gpu.indexBuffer  = ib;
    gpu.vertexCount  = cooked.vertexCount;
    gpu.indexCount   = cooked.indexCount;
    gpu.subMeshes.reserve(cooked.subMeshes.size());
    for (const auto& sm : cooked.subMeshes) {
        GPUSubMesh gsm;
        gsm.firstIndex    = sm.indexOffset;
        gsm.indexCount    = sm.indexCount;
        gsm.vertexOffset  = static_cast<int32_t>(sm.vertexOffset);
        gsm.materialIndex = sm.materialIndex;
        gpu.subMeshes.push_back(gsm);
    }

    SA_LOG_INFO("ResourceManager: loaded mesh {} (vertices={} indices={} submeshes={})",
                id.ToString(), cooked.vertexCount, cooked.indexCount, cooked.subMeshes.size());

    auto [ins, ok] = m_meshes.emplace(key, std::move(gpu));
    return &ins->second;
}

} // namespace StellarAlia::Resource
