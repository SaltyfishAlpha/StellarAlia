#include "resource/loaders/ImageLoader.hpp"
#include "core/logs/Log.hpp"

#include <stb_image.h>  // declarations only — implementation is in StbImpl.cpp

namespace StellarAlia::Resource {

std::optional<ImageData> ImageLoader::Load(const std::string& path) {
    stbi_set_flip_vertically_on_load(false);

    int w, h, ch;
    uint8_t* raw = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!raw) {
        SA_LOG_ERROR("ImageLoader: failed to load '{}' — {}", path, stbi_failure_reason());
        return std::nullopt;
    }

    ImageData img;
    img.path     = path;
    img.width    = static_cast<uint32_t>(w);
    img.height   = static_cast<uint32_t>(h);
    img.channels = 4;   // forced RGBA
    img.isHDR    = false;

    const size_t byteCount = static_cast<size_t>(w) * h * 4;
    img.pixels.assign(raw, raw + byteCount);
    stbi_image_free(raw);

    SA_LOG_DEBUG("ImageLoader: loaded '{}' {}x{} RGBA", path, w, h);
    return img;
}

std::optional<ImageData> ImageLoader::LoadHDR(const std::string& path) {
    stbi_set_flip_vertically_on_load(false);

    int w, h, ch;
    float* raw = stbi_loadf(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!raw) {
        SA_LOG_ERROR("ImageLoader: failed to load HDR '{}' — {}", path, stbi_failure_reason());
        return std::nullopt;
    }

    ImageData img;
    img.path     = path;
    img.width    = static_cast<uint32_t>(w);
    img.height   = static_cast<uint32_t>(h);
    img.channels = 4;   // forced RGBA to match CookedTextureFormat::RGBA32F
    img.isHDR    = true;

    const size_t floatCount = static_cast<size_t>(w) * h * 4;
    img.pixelsHDR.assign(raw, raw + floatCount);
    stbi_image_free(raw);

    SA_LOG_DEBUG("ImageLoader: loaded HDR '{}' {}x{} RGBA float", path, w, h);
    return img;
}

std::optional<ImageData> ImageLoader::LoadFromMemory(const uint8_t* data,
                                                      size_t          byteLen,
                                                      const std::string& debugName) {
    int w, h, ch;
    uint8_t* raw = stbi_load_from_memory(
        data, static_cast<int>(byteLen), &w, &h, &ch, STBI_rgb_alpha);

    if (!raw) {
        SA_LOG_ERROR("ImageLoader: failed to load from memory '{}' — {}",
                     debugName, stbi_failure_reason());
        return std::nullopt;
    }

    ImageData img;
    img.path     = debugName;
    img.width    = static_cast<uint32_t>(w);
    img.height   = static_cast<uint32_t>(h);
    img.channels = 4;
    img.isHDR    = false;

    const size_t byteCount = static_cast<size_t>(w) * h * 4;
    img.pixels.assign(raw, raw + byteCount);
    stbi_image_free(raw);

    SA_LOG_DEBUG("ImageLoader: loaded from memory '{}' {}x{} RGBA", debugName, w, h);
    return img;
}

} // namespace StellarAlia::Resource
