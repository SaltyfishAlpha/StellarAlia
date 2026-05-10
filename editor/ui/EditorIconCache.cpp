#include "ui/EditorIconCache.hpp"

#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "core/logs/Log.hpp"

#include <imgui_impl_vulkan.h>
#include <volk.h>

namespace StellarAlia::Editor {

using namespace StellarAlia::RHI;
using namespace StellarAlia::Resource;

// ─── helpers ─────────────────────────────────────────────────────────────────

static bool IsImageExt(const std::filesystem::path& p) {
    const auto ext = p.extension().string();
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
        || ext == ".bmp" || ext == ".tga";
}

// ─── Init / Shutdown ─────────────────────────────────────────────────────────

void EditorIconCache::Init(VulkanDevice*            device,
                           ResourceManager*         resMgr,
                           const std::filesystem::path& engineAssetsDir) {
    m_device  = device;
    m_resMgr  = resMgr;

    // Build the thumbnail LRU with an evict callback that frees GPU resources.
    m_thumbCache = LruCache<std::string, ThumbEntry>(
        kMaxThumbnails,
        [this](const std::string&, ThumbEntry& e) {
            if (e.id)
                ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(e.id));
            if (e.handle.IsValid())
                m_device->DestroyTexture(e.handle);
        });

    // Load engine logo.
    const auto logoPath = engineAssetsDir / "textures" / "editor" / "engine_logo.png";
    m_logoHandle = resMgr->LoadTextureFromFile(logoPath);
    if (m_logoHandle.IsValid()) {
        m_logoId = uploadToImGui(m_logoHandle);
    } else {
        SA_LOG_WARN("EditorIconCache: engine_logo.png not found at '{}'", logoPath.string());
    }
}

void EditorIconCache::Shutdown() {
    ClearAllThumbnails();

    // Logo is owned by ResourceManager; only free the descriptor set.
    if (m_logoId) {
        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_logoId));
        m_logoId = ImTextureID(0);
    }
    m_logoHandle = {};

    m_device  = nullptr;
    m_resMgr  = nullptr;
}

// ─── Thumbnail access ─────────────────────────────────────────────────────────

ImTextureID EditorIconCache::GetThumbnailForPath(const std::filesystem::path& absPath) {
    if (!m_device || !IsImageExt(absPath)) return ImTextureID(0);

    const std::string key = absPath.string();
    if (ThumbEntry* e = m_thumbCache.Get(key))
        return e->id;

    // Lazy-load: read pixels → create GPU texture → ImGui descriptor set.
    auto img = ImageLoader::Load(absPath.string());
    if (!img) {
        m_thumbCache.Put(key, ThumbEntry{});  // sentinel: marks as failed, prevents retry
        return ImTextureID(0);
    }

    RHITextureDesc td{};
    td.width     = img->width;
    td.height    = img->height;
    td.format    = RHIFormat::RGBA8_UNORM;
    td.usage     = RHITextureUsage::Sampled;

    RHITextureHandle handle = m_device->CreateTexture(td);
    if (!handle.IsValid()) return ImTextureID(0);

    const uint64_t byteSize = static_cast<uint64_t>(img->width) * img->height * 4u;
    m_device->UploadTextureData(handle, img->pixels.data(), byteSize);

    ImTextureID id = uploadToImGui(handle);

    m_thumbCache.Put(key, ThumbEntry{handle, id, img->width, img->height});
    return id;
}

bool EditorIconCache::GetImageSize(const std::filesystem::path& absPath,
                                   uint32_t& w, uint32_t& h) {
    const std::string key = absPath.string();
    if (ThumbEntry* e = m_thumbCache.Get(key)) {
        w = e->w;
        h = e->h;
        return true;
    }
    return false;
}

void EditorIconCache::ClearAllThumbnails() {
    m_thumbCache.Clear();
}

// ─── Private ─────────────────────────────────────────────────────────────────

ImTextureID EditorIconCache::uploadToImGui(RHITextureHandle handle) const {
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        m_device->GetLinearSampler(),
        m_device->GetTextureImageView(handle),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return reinterpret_cast<ImTextureID>(ds);
}

} // namespace StellarAlia::Editor
