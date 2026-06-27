#pragma once

#include <filesystem>

namespace StellarAlia {

class InputSystem;

// Project-side .sainputmap discovery & registration.
//
// LoadAll() scans projectDir for *.sainputmap.sameta sidecars, resolves each to
// its source (or cooked) JSON payload, parses via ActionMapJsonParser, and feeds
// the results into inputSystem.RegisterMaps(). When the InputSystem's map stack
// is empty, the first registered map is auto-pushed so scripts can read actions
// immediately on project load.
//
// Called from Application::UpdateProjectPaths() each time a project is opened
// or switched. Re-running it on the same project replaces same-name defs in
// the InputSystem registry (RegisterMaps replaces by name).
struct InputMapLoader {
    static void LoadAll(const std::filesystem::path& projectDir,
                        const std::filesystem::path& cookCacheDir,
                        InputSystem&                 inputSystem);
};

} // namespace StellarAlia
