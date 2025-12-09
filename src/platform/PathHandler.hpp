#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace StellarAlia::Platform {

class PathHandler {
public:
    // Return path of 'target' relative to 'base'. If not possible, returns target.
    static std::filesystem::path GetRelativePath(const std::filesystem::path& base,
                                                 const std::filesystem::path& target);

    // Split path into segments.
    static std::vector<std::string> GetPathSegments(const std::filesystem::path& path);

    // Return the file extension without the leading dot; empty if none.
    static std::string GetFileExtension(const std::filesystem::path& path);

    // Return filename without extension.
    static std::string GetFilePureName(const std::filesystem::path& path);
};

}  // namespace StellarAlia::Platform

