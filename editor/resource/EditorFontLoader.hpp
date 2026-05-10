#pragma once

#include <filesystem>
#include <imgui.h>

namespace StellarAlia::Editor {

struct EditorFonts {
    ImFont* ui    = nullptr; // regular UI font (nullptr = ImGui built-in default)
    ImFont* icons = nullptr; // FA6 Solid icon font (nullptr if file not found)
};

// Loads editor fonts into ImGuiIO::Fonts in the correct order.
// Must be called before the first ImGui frame / backend font upload.
class EditorFontLoader {
public:
    static EditorFonts Load(ImGuiIO&                     io,
                            const std::filesystem::path& assetsDir,
                            float uiSize   = 15.f,
                            float iconSize = 20.f);
};

} // namespace StellarAlia::Editor
