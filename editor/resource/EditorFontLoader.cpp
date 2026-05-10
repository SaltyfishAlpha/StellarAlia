#include "resource/EditorFontLoader.hpp"
#include "ui/IconsFontAwesome6.h"
#include "core/logs/Log.hpp"

namespace StellarAlia::Editor {

EditorFonts EditorFontLoader::Load(ImGuiIO&                     io,
                                   const std::filesystem::path& assetsDir,
                                   float                        uiSize,
                                   float                        iconSize) {
    EditorFonts result;

    // ── 1. Regular UI font ────────────────────────────────────────────────────
    // The first font added becomes the ImGui default. Try a custom TTF first;
    // fall back to the built-in bitmap font so ASCII text is always readable.
    if (!assetsDir.empty()) {
        const auto path = assetsDir / "fonts" / "ui.ttf";
        if (std::filesystem::exists(path))
            result.ui = io.Fonts->AddFontFromFileTTF(path.string().c_str(), uiSize);
    }
    if (!result.ui)
        result.ui = io.Fonts->AddFontDefault();

    // ── 2. FA6 Solid icon font (standalone — used with PushFont / AddText) ───
    if (!assetsDir.empty()) {
        const auto path = assetsDir / "fonts" / "Font Awesome 6 Free-Solid-900.otf";
        if (std::filesystem::exists(path)) {
            static const ImWchar kFA6Ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
            ImFontConfig cfg;
            cfg.MergeMode  = false;
            cfg.PixelSnapH = true;
            result.icons = io.Fonts->AddFontFromFileTTF(
                path.string().c_str(), iconSize, &cfg, kFA6Ranges);
            if (!result.icons)
                SA_LOG_WARN("EditorFontLoader: failed to load FA6 font from '{}'",
                            path.string());
        } else {
            SA_LOG_WARN("EditorFontLoader: FA6 font not found at '{}'", path.string());
        }
    }

    return result;
}

} // namespace StellarAlia::Editor
