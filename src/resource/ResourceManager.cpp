#include "resource/ResourceManager.hpp"

#include "resource/vfs/VFS.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "resource/cook/CookedSH9.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "resource/cook/CookedAnim.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "platform/rhi/RHITypes.hpp"
#include "core/logs/Log.hpp"

#include <cstring>
#include <functional>

namespace StellarAlia::Resource {

void ResourceManager::Init(const std::filesystem::path& engineCookCacheDir, RHI::IRHIDevice* device) {
    m_device = device;
    VFS::SetEngineCookCacheDir(engineCookCacheDir);
    SA_LOG_INFO("ResourceManager: engine cook cache = {}", engineCookCacheDir.string());

    // Create built-in textures.
    RHI::RHITextureDesc whiteDesc{};
    whiteDesc.width     = whiteDesc.height = 1;
    whiteDesc.format    = RHI::RHIFormat::RGBA8_UNORM;
    whiteDesc.usage     = RHI::RHITextureUsage::Sampled;
    whiteDesc.debugName = "Builtin_White1x1";
    m_white1x1 = m_device->CreateTexture(whiteDesc);
    const uint32_t whitePixel = 0xFFFFFFFFu;
    m_device->UploadTextureData(m_white1x1, &whitePixel, sizeof(whitePixel));
}

void ResourceManager::SetProjectCookCache(const std::filesystem::path& projectCookCacheDir) {
    VFS::SetCookCacheDir(projectCookCacheDir);
    SA_LOG_INFO("ResourceManager: project cook cache = {}", projectCookCacheDir.string());
}

void ResourceManager::Shutdown() {
    if (!m_device) return;

    if (m_white1x1.IsValid()) {
        m_device->DestroyTexture(m_white1x1);
        m_white1x1 = {};
    }

    for (auto& [hash, handle] : m_textures)
        if (handle.IsValid()) m_device->DestroyTexture(handle);
    m_textures.clear();

    for (auto& [hash, handle] : m_fileTextures)
        if (handle.IsValid()) m_device->DestroyTexture(handle);
    m_fileTextures.clear();

    for (auto& [hash, mesh] : m_meshes) {
        if (mesh.vertexBuffer.IsValid())   m_device->DestroyBuffer(mesh.vertexBuffer);
        if (mesh.indexBuffer.IsValid())    m_device->DestroyBuffer(mesh.indexBuffer);
        if (mesh.skinDataBuffer.IsValid()) m_device->DestroyBuffer(mesh.skinDataBuffer);
    }
    m_meshes.clear();

    m_device = nullptr;
}

void ResourceManager::ClearProjectAssets() {
    if (!m_device) return;
    m_device->WaitIdle();

    const size_t nTex  = m_textures.size();
    const size_t nMesh = m_meshes.size();

    for (auto& [hash, handle] : m_textures)
        if (handle.IsValid()) m_device->DestroyTexture(handle);
    m_textures.clear();

    for (auto& [hash, mesh] : m_meshes) {
        if (mesh.vertexBuffer.IsValid())   m_device->DestroyBuffer(mesh.vertexBuffer);
        if (mesh.indexBuffer.IsValid())    m_device->DestroyBuffer(mesh.indexBuffer);
        if (mesh.skinDataBuffer.IsValid()) m_device->DestroyBuffer(mesh.skinDataBuffer);
    }
    m_meshes.clear();

    m_skeletons.clear();
    m_animClips.clear();
    m_cookedMeshes.clear();

    SA_LOG_INFO("ResourceManager: cleared {} texture(s) and {} mesh(es) from project cache",
                nTex, nMesh);
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
    texDesc.mipLevels = std::max(1u, cooked.mipLevels);
    texDesc.format    = rhiFmt;
    texDesc.usage     = RHI::RHITextureUsage::Sampled;
    texDesc.cubemap   = cooked.cubemap;

    RHI::RHITextureHandle handle = m_device->CreateTexture(texDesc);
    if (!handle.IsValid()) {
        SA_LOG_ERROR("ResourceManager::LoadTexture — CreateTexture failed");
        return {};
    }

    // Upload all mip levels.
    if (cooked.mipLevels > 1) {
        std::vector<RHI::IRHIDevice::MipUpload> mipUploads;
        mipUploads.reserve(cooked.mipLevels);
        for (uint32_t m = 0; m < cooked.mipLevels; ++m)
            mipUploads.push_back({cooked.MipData(m), cooked.MipSize(m)});
        m_device->UploadTextureMips(handle, mipUploads);
    } else {
        const void*  pixels = cooked.MipData(0);
        const size_t size   = cooked.MipSize(0);
        if (pixels && size > 0)
            m_device->UploadTextureData(handle, pixels, static_cast<uint64_t>(size));
    }

    SA_LOG_INFO("ResourceManager: loaded texture {} ({}x{} mips={} {})",
                id.ToString(), cooked.width, cooked.height, cooked.mipLevels,
                cooked.cubemap ? "cubemap" : "2D");

    m_textures[key] = handle;
    return handle;
}

// ─── LoadTextureFromFile ──────────────────────────────────────────────────────

RHI::RHITextureHandle ResourceManager::LoadTextureFromFile(const std::filesystem::path& path) {
    if (!m_device) return {};

    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(path, ec);
    if (ec) {
        SA_LOG_ERROR("ResourceManager::LoadTextureFromFile — path not found: '{}'", path.string());
        return {};
    }

    const std::size_t key = std::hash<std::string>{}(canonical.string());
    auto it = m_fileTextures.find(key);
    if (it != m_fileTextures.end()) return it->second;

    auto img = ImageLoader::Load(canonical.string());
    if (!img) return {};   // ImageLoader already logged the error

    RHI::RHITextureDesc td{};
    td.width     = img->width;
    td.height    = img->height;
    td.format    = RHI::RHIFormat::RGBA8_UNORM;
    td.usage     = RHI::RHITextureUsage::Sampled;
    td.debugName = canonical.filename().string().c_str();

    RHI::RHITextureHandle handle = m_device->CreateTexture(td);
    if (!handle.IsValid()) {
        SA_LOG_ERROR("ResourceManager::LoadTextureFromFile — CreateTexture failed for '{}'",
                     canonical.string());
        return {};
    }

    const uint64_t byteSize = static_cast<uint64_t>(img->width) * img->height * 4u;
    m_device->UploadTextureData(handle, img->pixels.data(), byteSize);

    SA_LOG_INFO("ResourceManager: loaded file texture {}x{} from '{}'",
                img->width, img->height, canonical.string());

    m_fileTextures[key] = handle;
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

    // Upload skin data buffer (GPU-only SSBO) for skinned meshes
    RHI::RHIBufferHandle skb{};
    if (cooked.IsSkinned()) {
        RHI::RHIBufferDesc skDesc{};
        skDesc.size       = cooked.skinData.size();
        skDesc.usage      = RHI::RHIBufferUsage::Storage;
        skDesc.cpuVisible = false;
        skDesc.debugName  = "SkinDataBuffer";
        skb = m_device->CreateBuffer(skDesc);
        if (!skb.IsValid()) {
            m_device->DestroyBuffer(vb);
            m_device->DestroyBuffer(ib);
            SA_LOG_ERROR("ResourceManager::LoadMesh — CreateBuffer(SkinData) failed");
            return nullptr;
        }
        m_device->UploadBufferData(skb, cooked.skinData.data(), cooked.skinData.size(), 0);
    }

    // Build GPUMesh
    GPUMesh gpu;
    gpu.vertexBuffer   = vb;
    gpu.indexBuffer    = ib;
    gpu.skinDataBuffer = skb;
    gpu.vertexCount    = cooked.vertexCount;
    gpu.indexCount     = cooked.indexCount;
    gpu.subMeshes.reserve(cooked.subMeshes.size());
    for (const auto& sm : cooked.subMeshes) {
        GPUSubMesh gsm;
        gsm.firstIndex         = sm.indexOffset;
        gsm.indexCount         = sm.indexCount;
        gsm.vertexOffset       = static_cast<int32_t>(sm.vertexOffset);
        gsm.materialIndex      = sm.materialIndex;
        gsm.localTransform     = sm.localTransform;
        gsm.defaultMaterialID  = sm.defaultMaterialID;

        // Compute mesh-local AABB from vertex positions before GPU upload.
        // Vertex layout: 48 bytes/vertex, first 12 bytes = vec3 position.
        constexpr uint32_t kStride = 48;
        for (uint32_t vi = sm.vertexOffset; vi < sm.vertexOffset + sm.vertexCount; ++vi) {
            const glm::vec3 p = *reinterpret_cast<const glm::vec3*>(
                cooked.vertexData.data() + vi * kStride);
            gsm.boundsMin = glm::min(gsm.boundsMin, p);
            gsm.boundsMax = glm::max(gsm.boundsMax, p);
        }

        gpu.subMeshes.push_back(gsm);
    }

    SA_LOG_INFO("ResourceManager: loaded mesh {} (vertices={} indices={} submeshes={})",
                id.ToString(), cooked.vertexCount, cooked.indexCount, cooked.subMeshes.size());

    auto [ins, ok] = m_meshes.emplace(key, std::move(gpu));
    return &ins->second;
}

// ─── LoadHDRImageData ─────────────────────────────────────────────────────────

std::optional<ImageData> ResourceManager::LoadHDRImageData(const AssetID& id) {
    if (!id.IsValid()) return std::nullopt;

    auto pathOpt = VFS::ResolveCookedPath(id, ".satex");
    if (!pathOpt) {
        SA_LOG_ERROR("ResourceManager::LoadHDRImageData — .satex not found for {}",
                     id.ToString());
        return std::nullopt;
    }

    CookedTexture cooked;
    if (!LoadCookedTexture(pathOpt->string(), cooked)) {
        SA_LOG_ERROR("ResourceManager::LoadHDRImageData — failed to parse {}",
                     pathOpt->filename().string());
        return std::nullopt;
    }

    if (cooked.format != CookedTextureFormat::RGBA32F) {
        SA_LOG_ERROR("ResourceManager::LoadHDRImageData — {} is not RGBA32F",
                     pathOpt->filename().string());
        return std::nullopt;
    }

    const float* fptr = reinterpret_cast<const float*>(cooked.MipData(0));
    const size_t fcount = cooked.MipSize(0) / sizeof(float);

    ImageData img;
    img.width    = cooked.width;
    img.height   = cooked.height;
    img.channels = 4;
    img.isHDR    = true;
    img.pixelsHDR.assign(fptr, fptr + fcount);
    return img;
}

// ─── LoadSH9Coeffs ────────────────────────────────────────────────────────────

std::optional<std::array<glm::vec4, 9>> ResourceManager::LoadSH9Coeffs(const AssetID& id) {
    if (!id.IsValid()) return std::nullopt;

    auto pathOpt = VFS::ResolveCookedPath(id, ".sash9");
    if (!pathOpt) {
        SA_LOG_ERROR("ResourceManager::LoadSH9Coeffs — .sash9 not found for {}",
                     id.ToString());
        return std::nullopt;
    }

    CookedSH9 sh9;
    if (!LoadCookedSH9(pathOpt->string(), sh9)) {
        SA_LOG_ERROR("ResourceManager::LoadSH9Coeffs — failed to parse {}",
                     pathOpt->filename().string());
        return std::nullopt;
    }

    SA_LOG_INFO("ResourceManager: loaded SH9 {}", id.ToString());
    return sh9.coeffs;
}

// ─── GetBuiltin ──────────────────────────────────────────────────────────────

RHI::RHITextureHandle ResourceManager::GetBuiltin(BuiltinTexture which) const {
    switch (which) {
        case BuiltinTexture::White1x1: return m_white1x1;
    }
    return {};
}

// ─── LoadSkeleton ─────────────────────────────────────────────────────────────

const CookedSkeleton* ResourceManager::LoadSkeleton(const AssetID& id) {
    if (!id.IsValid()) return nullptr;

    const uint64_t key = HashID(id);
    auto it = m_skeletons.find(key);
    if (it != m_skeletons.end()) return &it->second;

    auto pathOpt = VFS::ResolveCookedPath(id, ".saskelc");
    if (!pathOpt) {
        SA_LOG_ERROR("ResourceManager::LoadSkeleton — .saskelc not found for {}", id.ToString());
        return nullptr;
    }

    CookedSkeleton skel;
    if (!LoadCookedSkeleton(pathOpt->string(), skel)) {
        SA_LOG_ERROR("ResourceManager::LoadSkeleton — failed to parse {}",
                     pathOpt->filename().string());
        return nullptr;
    }

    SA_LOG_INFO("ResourceManager: loaded skeleton {} ({} bones)", id.ToString(), skel.bones.size());
    auto [ins, ok] = m_skeletons.emplace(key, std::move(skel));
    return &ins->second;
}

// ─── LoadAnimClip ─────────────────────────────────────────────────────────────

const CookedAnim* ResourceManager::LoadAnimClip(const AssetID& id) {
    if (!id.IsValid()) return nullptr;

    const uint64_t key = HashID(id);
    auto it = m_animClips.find(key);
    if (it != m_animClips.end()) return &it->second;

    auto pathOpt = VFS::ResolveCookedPath(id, ".saanim");
    if (!pathOpt) {
        SA_LOG_ERROR("ResourceManager::LoadAnimClip — .saanim not found for {}", id.ToString());
        return nullptr;
    }

    CookedAnim anim;
    if (!LoadCookedAnim(pathOpt->string(), anim)) {
        SA_LOG_ERROR("ResourceManager::LoadAnimClip — failed to parse {}",
                     pathOpt->filename().string());
        return nullptr;
    }

    SA_LOG_INFO("ResourceManager: loaded anim '{}' ({} channels, {:.2f}s)",
                anim.clip.name, anim.clip.channels.size(), anim.clip.duration);
    auto [ins, ok] = m_animClips.emplace(key, std::move(anim));
    return &ins->second;
}

// ─── LoadMeshData ─────────────────────────────────────────────────────────────

const CookedMesh* ResourceManager::LoadMeshData(const AssetID& id) {
    if (!id.IsValid()) return nullptr;

    const uint64_t key = HashID(id);
    auto it = m_cookedMeshes.find(key);
    if (it != m_cookedMeshes.end()) return &it->second;

    auto pathOpt = VFS::ResolveCookedPath(id, ".samesh");
    if (!pathOpt) {
        SA_LOG_ERROR("ResourceManager::LoadMeshData — .samesh not found for {}", id.ToString());
        return nullptr;
    }

    CookedMesh mesh;
    if (!LoadCookedMesh(pathOpt->string(), mesh)) {
        SA_LOG_ERROR("ResourceManager::LoadMeshData — failed to parse {}",
                     pathOpt->filename().string());
        return nullptr;
    }

    auto [ins, ok] = m_cookedMeshes.emplace(key, std::move(mesh));
    return &ins->second;
}

} // namespace StellarAlia::Resource
