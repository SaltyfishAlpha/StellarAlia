#include "core/io/FileIO.hpp"

#include "core/logs/Log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>

namespace StellarAlia::IO {

std::optional<std::string> ReadText(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { SA_LOG_WARN("IO::ReadText: cannot open '{}'", path.string()); return std::nullopt; }
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool WriteText(const fs::path& path, std::string_view content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { SA_LOG_ERROR("IO::WriteText: cannot open '{}'", path.string()); return false; }
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f.good();
}

std::optional<std::vector<uint8_t>> ReadBytes(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_WARN("IO::ReadBytes: cannot open '{}'", path.string()); return std::nullopt; }
    const std::streamsize size = f.tellg();
    if (size < 0) { SA_LOG_WARN("IO::ReadBytes: tellg failed '{}'", path.string()); return std::nullopt; }
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    if (!f) { SA_LOG_WARN("IO::ReadBytes: short read '{}'", path.string()); return std::nullopt; }
    return data;
}

bool WriteBytes(const fs::path& path, std::span<const uint8_t> bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { SA_LOG_ERROR("IO::WriteBytes: cannot open '{}'", path.string()); return false; }
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

bool ReadJson(const fs::path& path, nlohmann::json& out) {
    const auto text = ReadText(path);
    if (!text) return false;
    try {
        out = nlohmann::json::parse(*text);
        return true;
    } catch (const std::exception& e) {
        SA_LOG_ERROR("IO::ReadJson: parse '{}' failed: {}", path.string(), e.what());
        return false;
    }
}

bool WriteJson(const fs::path& path, const nlohmann::json& j, int indent) {
    return WriteText(path, j.dump(indent));
}

bool Copy(const fs::path& src, const fs::path& dst, bool overwrite) {
    std::error_code ec;
    const auto opt = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
    fs::copy_file(src, dst, opt, ec);
    if (ec) { SA_LOG_WARN("IO::Copy '{}' -> '{}': {}", src.string(), dst.string(), ec.message()); return false; }
    return true;
}

bool Rename(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) { SA_LOG_WARN("IO::Rename '{}' -> '{}': {}", from.string(), to.string(), ec.message()); return false; }
    return true;
}

bool Remove(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);   // file or recursive dir; no error when already absent
    if (ec) { SA_LOG_WARN("IO::Remove '{}': {}", path.string(), ec.message()); return false; }
    return true;
}

bool EnsureDir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { SA_LOG_WARN("IO::EnsureDir '{}': {}", dir.string(), ec.message()); return false; }
    return true;
}

bool CopyTemplateReplacing(const fs::path& tmpl, const fs::path& dst,
                           std::string_view token, std::string_view value) {
    auto content = ReadText(tmpl);
    if (!content) return false;
    if (!token.empty()) {
        std::string& s = *content;
        for (std::string::size_type pos = 0; (pos = s.find(token, pos)) != std::string::npos; ) {
            s.replace(pos, token.size(), value);
            pos += value.size();
        }
    }
    return WriteText(dst, *content);
}

} // namespace StellarAlia::IO
