#include "platform/PathHandler.hpp"

namespace StellarAlia::Platform {

std::filesystem::path PathHandler::GetRelativePath(const std::filesystem::path& base,
                                                   const std::filesystem::path& target) {
    std::error_code ec;
    auto rel = std::filesystem::relative(target, base, ec);
    if (ec) {
        return target;
    }
    return rel;
}

std::vector<std::string> PathHandler::GetPathSegments(const std::filesystem::path& path) {
    std::vector<std::string> segments;
    for (const auto& part : path) {
        if (!part.empty()) {
            segments.push_back(part.string());
        }
    }
    return segments;
}

std::string PathHandler::GetFileExtension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    return ext;
}

std::string PathHandler::GetFilePureName(const std::filesystem::path& path) {
    return path.stem().string();
}

}  // namespace StellarAlia::Platform

