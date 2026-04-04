#include "engine/Application.hpp"
#include "EditorMode.hpp"
#include "ApplicationPath.hpp"

int main() {
    StellarAlia::Application app(
        std::make_unique<StellarAlia::Editor::EditorMode>()
    );

    StellarAlia::Application::Desc desc{};
    desc.width        = 1280;
    desc.height       = 720;
    desc.title        = "StellarAlia Editor";
    desc.vsync        = true;
    desc.validation   = false;
    desc.assetsDir    = StellarAliaApp::ASSETS_DIR;
    desc.cookCacheDir = StellarAliaApp::COOK_CACHE_DIR;
    desc.shaderDir    = StellarAliaApp::BUILTIN_SHADER_DIR;

    if (!app.Init(desc))
        return 1;

    app.Run();
    app.Shutdown();
    return 0;
}
