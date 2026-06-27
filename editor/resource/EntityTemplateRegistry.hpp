#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace StellarAlia::Editor {

// Metadata for a single entity template file.
struct TemplateEntry {
    std::string           category;  // subdirectory name inside entities/; empty = top-level
    std::string           label;     // filename without extension
    std::filesystem::path path;      // absolute path to the .sascene template
};

// ─────────────────────────────────────────────────────────────────────────────
// EntityTemplateRegistry
//
// Scans engineAssetsDir/templates/entities/ for .sascene files and exposes
// them as a flat list of TemplateEntry values.  The SceneHierarchyPanel reads
// Entries() to build its spawn menu without any hardcoded CreateKind enum.
//
// Directory layout:
//   templates/entities/
//     Camera.sascene            → category="", label="Camera"
//     3D Object/
//       Cube.sascene            → category="3D Object", label="Cube"
//     Light/
//       Point Light.sascene     → category="Light", label="Point Light"
//
// Only one level of subdirectories is recognised; deeper nesting is ignored.
// ─────────────────────────────────────────────────────────────────────────────
class EntityTemplateRegistry {
public:
    // Populate entries by scanning engineAssetsDir/templates/entities/.
    // Also resolves the default scene template path.
    // Safe to call multiple times (replaces previous state).
    void Scan(const std::filesystem::path& engineAssetsDir);

    const std::vector<TemplateEntry>& Entries() const { return m_entries; }

    // Absolute path to templates/scenes/default.sascene, or empty if not found.
    const std::filesystem::path& DefaultScenePath()   const { return m_defaultScene; }
    // Absolute path to templates/scripts/NewScript.cs, or empty if not found.
    const std::filesystem::path& ScriptTemplatePath() const { return m_scriptTemplate; }
    // Absolute path to templates/materials/PBR Default.samat, or empty if not found.
    const std::filesystem::path& MatTemplatePath()    const { return m_matTemplate; }
    // Absolute path to templates/shaders/NewShader.saglsl, or empty if not found.
    const std::filesystem::path& ShaderTemplatePath() const { return m_shaderTemplate; }
    // Absolute path to templates/inputmaps/Controls.sainputmap, or empty if not found.
    const std::filesystem::path& InputMapTemplatePath() const { return m_inputMapTemplate; }

private:
    std::vector<TemplateEntry> m_entries;
    std::filesystem::path      m_defaultScene;
    std::filesystem::path      m_scriptTemplate;
    std::filesystem::path      m_matTemplate;
    std::filesystem::path      m_shaderTemplate;
    std::filesystem::path      m_inputMapTemplate;
};

} // namespace StellarAlia::Editor
