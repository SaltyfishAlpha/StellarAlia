#include "ui/EditorIconCache.hpp"

#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/loaders/DdsLoader.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "core/logs/Log.hpp"

#include <imgui_impl_vulkan.h>
#include <volk.h>

#include <algorithm>
#include <cmath>

namespace StellarAlia::Editor {

using namespace StellarAlia::RHI;
using namespace StellarAlia::Resource;

// ─── helpers ─────────────────────────────────────────────────────────────────

static bool IsImageExt(const std::filesystem::path& p) {
    const auto ext = p.extension().string();
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
        || ext == ".bmp" || ext == ".tga"
        || ext == ".dds" || ext == ".hdr";   // Issue #108: bcdec / tonemap paths
}

// Thumbnails are previews — cap them so browsing folders of 4K textures never
// decodes/uploads full-res images (several per frame → visible hitches).
static constexpr uint32_t kThumbMaxDim = 256;

static void DownscaleRGBA8(ImageData& img, uint32_t maxDim) {
    if (img.width <= maxDim && img.height <= maxDim) return;
    const float scale = static_cast<float>(maxDim) /
                        static_cast<float>(std::max(img.width, img.height));
    const uint32_t nw = std::max(1u, static_cast<uint32_t>(img.width  * scale));
    const uint32_t nh = std::max(1u, static_cast<uint32_t>(img.height * scale));

    std::vector<uint8_t> out(size_t(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; ++y) {
        const uint32_t sy = static_cast<uint32_t>(uint64_t(y) * img.height / nh);
        for (uint32_t x = 0; x < nw; ++x) {
            const uint32_t sx = static_cast<uint32_t>(uint64_t(x) * img.width / nw);
            std::memcpy(&out[(size_t(y) * nw + x) * 4],
                        &img.pixels[(size_t(sy) * img.width + sx) * 4], 4);
        }
    }
    img.pixels = std::move(out);
    img.width  = nw;
    img.height = nh;
}

// Thumbnail pixels for formats stb can't decode directly. outSrcW/outSrcH
// report the source resolution (the returned pixels are preview-sized).
static std::optional<ImageData> LoadThumbPixels(const std::filesystem::path& p,
                                                uint32_t& outSrcW, uint32_t& outSrcH) {
    const auto ext = p.extension().string();

    if (ext == ".dds") {
        auto dds = DdsLoader::Load(p.string());
        if (!dds) return std::nullopt;
        outSrcW = dds->tex.width;
        outSrcH = dds->tex.height;
        // Use the authored mip chain: decode the deepest mip still ≥ the
        // preview size instead of paying a full-res BC7 decode.
        uint32_t mip = 0;
        while (mip + 1 < dds->tex.mipLevels &&
               std::max(dds->tex.width >> (mip + 1), dds->tex.height >> (mip + 1))
                   >= kThumbMaxDim)
            ++mip;
        auto img = DdsLoader::DecodeToRGBA8(dds->tex, mip);
        if (img) DownscaleRGBA8(*img, kThumbMaxDim);
        return img;
    }

    if (ext == ".hdr") {
        auto hdr = ImageLoader::LoadHDR(p.string());
        if (!hdr || hdr->pixelsHDR.empty()) return std::nullopt;
        outSrcW = hdr->width;
        outSrcH = hdr->height;
        // Stride-sample straight to preview size — tonemapping a full 8K
        // skybox per thumbnail would stall the frame.
        const float scale = std::min(
            1.f, static_cast<float>(kThumbMaxDim) /
                 static_cast<float>(std::max(hdr->width, hdr->height)));
        const uint32_t nw = std::max(1u, static_cast<uint32_t>(hdr->width  * scale));
        const uint32_t nh = std::max(1u, static_cast<uint32_t>(hdr->height * scale));
        const uint32_t ch = hdr->channels ? hdr->channels : 3;

        ImageData img;
        img.width    = nw;
        img.height   = nh;
        img.channels = 4;
        img.pixels.resize(size_t(nw) * nh * 4);
        for (uint32_t y = 0; y < nh; ++y) {
            const uint32_t sy = static_cast<uint32_t>(uint64_t(y) * hdr->height / nh);
            for (uint32_t x = 0; x < nw; ++x) {
                const uint32_t sx = static_cast<uint32_t>(uint64_t(x) * hdr->width / nw);
                const size_t src = (size_t(sy) * hdr->width + sx) * ch;
                const size_t dst = (size_t(y) * nw + x) * 4;
                for (uint32_t c = 0; c < 3; ++c) {
                    const float v = hdr->pixelsHDR[src + std::min(c, ch - 1)];
                    const float t = std::pow(v / (1.f + v), 1.f / 2.2f);
                    img.pixels[dst + c] = static_cast<uint8_t>(
                        std::clamp(t, 0.f, 1.f) * 255.f + 0.5f);
                }
                img.pixels[dst + 3] = 255;
            }
        }
        return img;
    }

    auto img = ImageLoader::Load(p.string());
    if (img) {
        outSrcW = img->width;
        outSrcH = img->height;
        DownscaleRGBA8(*img, kThumbMaxDim);
    }
    return img;
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

    // Lazy-load: read pixels (preview-sized) → GPU texture → ImGui descriptor.
    uint32_t srcW = 0, srcH = 0;
    auto img = LoadThumbPixels(absPath, srcW, srcH);
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

    // Entry keeps the SOURCE resolution — the inspector displays it as "W × H".
    m_thumbCache.Put(key, ThumbEntry{handle, id,
                                     srcW ? srcW : img->width,
                                     srcH ? srcH : img->height});
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
