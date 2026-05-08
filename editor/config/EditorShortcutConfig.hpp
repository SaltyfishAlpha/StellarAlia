#pragma once

#include "function/input/ActionMapDef.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace StellarAlia::Editor {

// Stores user overrides for userConfigurable actions.
// Each override replaces bindings[0] of the matching ActionDef;
// Gamepad bindings (index > 0) are left untouched.
class EditorShortcutConfig {
public:
    // Load from JSON. Silently uses defaults if the file does not exist.
    // Stores the path for subsequent Save() / Reload() calls.
    void Load(const std::filesystem::path& configPath);

    // Re-read overrides from the path supplied to Load(), discarding unsaved changes.
    // No-op if Load() was never called.
    void Reload();

    // Write current overrides back to the path supplied to Load().
    void Save() const;

    // Export overrides to an arbitrary path (does not change the active config path).
    void ExportTo(const std::filesystem::path& path) const;

    // Import overrides from an arbitrary path, replacing all current overrides.
    // Also switches the active config path so Save() / Reload() target the new file.
    void ImportFrom(const std::filesystem::path& path);

    bool IsDirty()    const { return m_dirty; }
    void ClearDirty()       { m_dirty = false; }

    const std::filesystem::path& GetConfigPath() const { return m_configPath; }

    // Return a copy of 'defaults' with overrides applied to bindings[0].
    std::vector<ActionMapDef> ApplyTo(const std::vector<ActionMapDef>& defaults) const;

    void SetOverride(const std::string& actionName, BindingDef binding);
    void ClearOverride(const std::string& actionName);

    // Returns nullptr when no override is set (caller uses the default binding).
    const BindingDef* GetOverride(const std::string& actionName) const;

private:
    std::unordered_map<std::string, BindingDef> m_overrides;
    std::filesystem::path                        m_configPath;
    bool                                         m_dirty = false;
};

} // namespace StellarAlia::Editor
