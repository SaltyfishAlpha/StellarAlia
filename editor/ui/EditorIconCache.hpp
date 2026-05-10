#pragma once

#include "core/LruCache.hpp"
#include "platform/rhi/RHITypes.hpp"

#include <filesystem>
#include <imgui.h>

namespace StellarAlia::RHI   { class VulkanDevice; }
namespace StellarAlia::Resource { class ResourceManager; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorIconCache
//
// Manages two classes of ImGui textures for the editor:
//
//   1. Engine logo   — a single permanent texture loaded at Init.
//   2. Thumbnails    — LRU-bounded cache (kMaxThumbnails) of image file previews.
//      Each entry owns its GPU texture and its ImGui descriptor set; both are
//      released by the LRU evict callback.
//
// Usage:
//   Call Init after ImGui_ImplVulkan_Init (descriptor pool must already exist).
//   Call Shutdown before ImGui_ImplVulkan_Shutdown.
//   Call ClearAllThumbnails() BEFORE ResourceManager::ClearProjectAssets() on
//   project switch, so descriptor sets are freed before image views are destroyed.
// ─────────────────────────────────────────────────────────────────────────────
class EditorIconCache {
public:
    static constexpr size_t kMaxThumbnails = 256;

    void Init(RHI::VulkanDevice*           device,
              Resource::ResourceManager*   resMgr,
              const std::filesystem::path& engineAssetsDir);
    void Shutdown();

    // Engine logo texture (nullptr if logo file is missing).
    [[nodiscard]] ImTextureID GetEngineLogo() const { return m_logoId; }

    // Returns an ImTextureID for the given image file (lazy-loaded, LRU-cached).
    // Returns nullptr if the file cannot be loaded or is not a recognised image.
    [[nodiscard]] ImTextureID GetThumbnailForPath(const std::filesystem::path& absPath);

    // Fills w/h with the pixel dimensions of the cached thumbnail.
    // Returns false if not in cache or not yet loaded.
    // Touches the LRU entry (not logically const, but safe to call alongside Draw).
    bool GetImageSize(const std::filesystem::path& absPath,
                      uint32_t& w, uint32_t& h);

    // Frees all thumbnail descriptor sets and GPU textures; clears the LRU.
    // Must be called before the underlying RHI textures are destroyed.
    void ClearAllThumbnails();

    // Returns true when the thumbnail for absPath is already in the LRU cache.
    // Use this to skip loading when the per-frame budget is exhausted.
    [[nodiscard]] bool IsThumbnailCached(const std::filesystem::path& absPath) const {
        return m_thumbCache.Contains(absPath.string());
    }

    // Returns true when there is still room in the LRU for a new entry.
    // Once the cache is full, adding another entry evicts the oldest one — which
    // will be re-requested next frame, causing thrashing.  Stop loading when full.
    [[nodiscard]] bool CanLoadThumbnail() const {
        return m_thumbCache.Size() < kMaxThumbnails;
    }

private:
    struct ThumbEntry {
        RHI::RHITextureHandle handle;
        ImTextureID           id = ImTextureID(0);
        uint32_t              w  = 0;
        uint32_t              h  = 0;
    };

    RHI::VulkanDevice*           m_device  = nullptr;
    Resource::ResourceManager*   m_resMgr  = nullptr;

    // Engine logo
    RHI::RHITextureHandle m_logoHandle;
    ImTextureID           m_logoId = ImTextureID(0);

    // Thumbnail LRU
    LruCache<std::string, ThumbEntry> m_thumbCache;

    [[nodiscard]] ImTextureID uploadToImGui(RHI::RHITextureHandle handle) const;
};

} // namespace StellarAlia::Editor
