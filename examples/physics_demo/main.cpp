// PhysicsDemo
//
// Loads physics_test.sascene — a ground plane (static) + five cubes (dynamic) —
// and immediately starts the physics simulation so you can watch them fall and
// collide.  Physics debug overlays (collider shapes + velocity arrows) are
// enabled by default so the Jolt integration is easy to verify visually.
//
// Prerequisites:
//   1. Run CookAssets so that cube.gltf and plane.gltf are cooked to .samesh.
//   2. Run ibl_demo at least once if you want IBL; the scene has no world
//      settings so it renders without a skybox.

#include "core/logs/Log.hpp"
#include "engine/AppMode.hpp"
#include "engine/Application.hpp"
#include "engine/EnginePlayState.hpp"
#include "function/renderer/CameraData.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "PhysicsDemoPath.hpp"

#include <filesystem>

namespace fs = std::filesystem;
using namespace StellarAlia;

// ── PhysicsDemoMode ───────────────────────────────────────────────────────────

class PhysicsDemoMode final : public AppMode {
public:
    void OnAttach(Application& app) override {
        m_app = &app;

        const fs::path scenePath =
            fs::path(PhysicsDemo::SA_ASSETS_DIR) / "scenes" / "physics_test.sascene";

        Scene& scene = app.GetScene();
        if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
            SA_LOG_CRITICAL("PhysicsDemo: failed to load '{}'", scenePath.string());
            return;
        }
        SA_LOG_INFO("PhysicsDemo: loaded '{}'", scenePath.string());

        // Enable collider shape + velocity arrow overlays for visual verification.
        auto& phys = app.GetPhysicsDebugSettings();
        phys.shapes   = true;
        phys.velocity = true;

        // Start simulation immediately — physics only ticks in Playing state.
        app.SetPlayState(EnginePlayState::Playing);
    }

    void OnDetach() override { m_app = nullptr; }

    void OnUpdate(float /*dt*/) override {}

    [[nodiscard]] CameraData GetCameraData(float aspectRatio) const override {
        constexpr uint32_t kRefW = 1920;
        const uint32_t refH = (aspectRatio > 0.f)
            ? static_cast<uint32_t>(static_cast<float>(kRefW) / aspectRatio)
            : 1080u;
        return SceneRenderer::ExtractCamera(m_app->GetScene(), kRefW, refH);
    }

private:
    Application* m_app = nullptr;
};

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    Application app(std::make_unique<PhysicsDemoMode>());

    Application::Desc desc;
    desc.width        = 1280;
    desc.height       = 720;
    desc.title        = "PhysicsDemo";
    desc.vsync        = true;
#ifdef NDEBUG
    desc.validation   = false;
#else
    desc.validation   = true;
#endif
    desc.assetsDir    = PhysicsDemo::SA_ASSETS_DIR;
    desc.cookCacheDir = PhysicsDemo::COOK_CACHE_DIR;
    desc.shaderDir    = PhysicsDemo::BUILTIN_SHADER_DIR;

    if (!app.Init(desc))
        return 1;

    app.Run();
    app.Shutdown();
    return 0;
}
