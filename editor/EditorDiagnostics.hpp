#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace StellarAlia::Editor {

enum class DiagLevel  : uint8_t { Info = 0, Warning = 1, Error = 2 };

// Source tag — used for filtering, display icons, and ClearSource().
// Extend here when new subsystems (e.g. Script) are added.
enum class DiagSource : uint8_t {
    ShaderCook,
    Material,
    Scene,
    Script,
    Runtime,
};

struct Diagnostic {
    DiagLevel             level     = DiagLevel::Info;
    DiagSource            source    = DiagSource::Runtime;
    std::string           message;
    std::filesystem::path assetPath; // optional — enables context in ConsolePanel
};

// ─────────────────────────────────────────────────────────────────────────────
// EditorDiagnostics
//
// Lightweight push/query bus owned by EditorMode and passed to panels that
// either produce errors (AssetsPanel, cook callback) or consume them
// (ConsolePanel, PlaybackPanel, EditorUI badge).
//
// Designed to host future sources (script compilation, physics validation …)
// without changing the consumer API — just push a Diagnostic with the new
// DiagSource tag.
// ─────────────────────────────────────────────────────────────────────────────
class EditorDiagnostics {
public:
    void Push(Diagnostic d);

    // Remove all items.
    void Clear();

    // Remove all items whose source matches. Use after a successful cook to
    // clear stale ShaderCook errors without touching material or scene errors.
    void ClearSource(DiagSource source);

    [[nodiscard]] bool HasErrors()    const;
    [[nodiscard]] bool HasWarnings()  const;
    [[nodiscard]] int  ErrorCount()   const;
    [[nodiscard]] int  WarningCount() const;
    [[nodiscard]] const std::vector<Diagnostic>& All() const { return m_items; }

private:
    std::vector<Diagnostic> m_items;
};

} // namespace StellarAlia::Editor
