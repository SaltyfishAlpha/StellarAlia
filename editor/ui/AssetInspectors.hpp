#pragma once

#include "ui/IAssetInspector.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace StellarAlia::Editor {

struct EditorContext;

// Fallback — filename, extension, file size.
class DefaultAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
};

// .txt .md .saglsl .saeffect .glsl .vert .frag .comp .hlsl — read-only text content.
class TextAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
private:
    std::filesystem::path m_lastPath;
    std::string           m_content;
    bool                  m_truncated = false;
};

// .samat — editable material source (Issue #101 / #99): render state combos,
// reflected parameter widgets, texture pickers; Save writes back + recooks +
// evicts the cached instance. Falls back to read-only display when the shader
// type is not registered or no EditorContext is wired.
class MaterialAssetInspector : public IAssetInspector {
public:
    MaterialAssetInspector();
    ~MaterialAssetInspector() override;
    void Draw(const std::filesystem::path& path) override;
    void SetContext(EditorContext* ctx) { m_ctx = ctx; }
private:
    void DrawReadOnly(const nlohmann::json& j) const;
    void Save(const std::filesystem::path& path);

    EditorContext*                  m_ctx = nullptr;
    std::filesystem::path           m_lastPath;
    std::unique_ptr<nlohmann::json> m_doc;       // parsed .samat; null = parse error
    bool                            m_dirty = false;
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

// .png .jpg .hdr — file size + thumbnail preview via EditorIconCache.
class ImageAssetInspector : public IAssetInspector {
public:
    void Draw(const std::filesystem::path& path) override;
    void SetIconCache(class EditorIconCache* cache) { m_iconCache = cache; }
private:
    EditorIconCache* m_iconCache = nullptr;
};

} // namespace StellarAlia::Editor
