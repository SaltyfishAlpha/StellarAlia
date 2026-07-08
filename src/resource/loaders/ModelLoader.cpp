#include "resource/loaders/ModelLoader.hpp"

#include "resource/loaders/FbxLoader.hpp"
#include "resource/loaders/GltfLoader.hpp"
#include "resource/loaders/ObjLoader.hpp"

#include "core/logs/Log.hpp"

#include <algorithm>
#include <filesystem>

namespace StellarAlia::Resource {

static std::string LowerExt(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return ext;
}

bool ModelLoader::SupportsExtension(std::string ext) {
    const std::string e = LowerExt(std::move(ext));
    return e == ".gltf" || e == ".glb" || e == ".vrm" || e == ".obj" || e == ".fbx";
}

std::optional<SceneData> ModelLoader::Load(const std::string& path) {
    const std::string ext = LowerExt(std::filesystem::path(path).extension().string());

    if (ext == ".gltf" || ext == ".glb" || ext == ".vrm")
        return GltfLoader::Load(path);
    if (ext == ".obj")
        return ObjLoader::Load(path);
    if (ext == ".fbx")
        return FbxLoader::Load(path);

    SA_LOG_ERROR("ModelLoader: unsupported model format '{}' ({})", ext, path);
    return std::nullopt;
}

} // namespace StellarAlia::Resource
