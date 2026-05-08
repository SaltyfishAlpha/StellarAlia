#pragma once

#include "ui/IAssetInspector.hpp"

#include <filesystem>
#include <string>

namespace StellarAlia::Editor {

// Fallback — filename, extension, file size.
class DefaultAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
};

// .txt .md .saglsl .glsl .vert .frag .comp .hlsl — read-only text content.
class TextAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
private:
    std::filesystem::path m_lastPath;
    std::string           m_content;
    bool                  m_truncated = false;
};

// .samat — shader type, parameter table, texture slots.
class MaterialAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
};

// .sascene — scene name, entity count, root-entity list.
class SceneAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
};

// .gltf .glb — mesh / node / material counts (gltf only), file size.
class ModelAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
};

// .png .jpg .hdr — file size; thumbnail deferred to when texture infra is ready.
class ImageAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
};

} // namespace StellarAlia::Editor
