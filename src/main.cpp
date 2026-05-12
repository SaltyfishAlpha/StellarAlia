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
#ifdef NDEBUG
    desc.validation   = false;
#else
    desc.validation   = true;
#endif
    desc.engineAssetsDir    = StellarAliaApp::ASSETS_DIR;
    desc.engineCookCacheDir = StellarAliaApp::COOK_CACHE_DIR;
    desc.shaderDir          = StellarAliaApp::BUILTIN_SHADER_DIR;
    // projectDir and cookCacheDir are left empty so the editor's project
    // browser is shown on startup.  Override with SA_PROJECT_DIR at build
    // time for quick iteration on a specific project:
#ifdef SA_DEBUG_PROJECT
    desc.projectDir   = StellarAliaApp::PROJECT_DIR;
    desc.cookCacheDir = StellarAliaApp::COOK_CACHE_DIR;
#endif

    if (!app.Init(desc))
        return 1;

    app.Run();
    app.Shutdown();
    return 0;
}
