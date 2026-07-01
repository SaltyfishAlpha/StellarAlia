#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// FileIO (Issue #90) — the single home for file-stream boilerplate.
//
// Centralises "open + check + read-all / write-all + error log" and the common
// std::filesystem mutations so call sites across editor / tools / runtime stop
// hand-rolling ifstream/ofstream + ad-hoc error handling. All failures log via
// SA_LOG and return an empty optional / false (never throw). JSON helpers use
// out-params so this header stays light (json_fwd only; .cpp pulls full json).
// ─────────────────────────────────────────────────────────────────────────────

namespace StellarAlia::IO {

namespace fs = std::filesystem;

// ── Whole-file text / binary ──────────────────────────────────────────────────
[[nodiscard]] std::optional<std::string>          ReadText (const fs::path& path);
                            bool                   WriteText(const fs::path& path, std::string_view content);
[[nodiscard]] std::optional<std::vector<uint8_t>> ReadBytes(const fs::path& path);
                            bool                   WriteBytes(const fs::path& path, std::span<const uint8_t> bytes);

// ── JSON (nlohmann) — centralised parse/dump + exception capture ───────────────
[[nodiscard]] bool ReadJson (const fs::path& path, nlohmann::json& out);
              bool WriteJson(const fs::path& path, const nlohmann::json& j, int indent = 2);

// ── Filesystem mutations — unified std::error_code + logging, return success ───
bool Copy     (const fs::path& src, const fs::path& dst, bool overwrite = true);  // name avoids Win32 CopyFile macro
bool Rename   (const fs::path& from, const fs::path& to);
bool Remove   (const fs::path& path);   // file or recursive dir; success also when already absent
bool EnsureDir(const fs::path& dir);

// Copy `tmpl` → `dst`, replacing every occurrence of `token` with `value` (text).
// Used by AssetsPanel create/rename to instantiate .cs/.saglsl/.saeffect/… templates.
bool CopyTemplateReplacing(const fs::path& tmpl, const fs::path& dst,
                           std::string_view token, std::string_view value);

} // namespace StellarAlia::IO
