// StellarAliaShaderCook — CLI wrapper around ShaderCookLib.
//
// Usage:
//   StellarAliaShaderCook
//       --scan-dir     <dir>   directory containing .saglsl files
//       --spv-out      <dir>   output directory for .spv / .refl
//       --dispatch-out <dir>   output directory for generated GLSL dispatch
//       --glslc        <path>  path to glslc executable
//       --reflect-tool <path>  path to ShaderReflectTool executable
//       --include      <dir>   additional glslc -I path (repeatable)
//       [--force]              recompile even if outputs are up to date

#include "shader_cook/ShaderCookLib.hpp"

#include "core/logs/Log.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    using namespace StellarAlia;
    Core::Log::Initialize();

    fs::path scanDir, spvOutDir, dispatchOutDir;
    ShaderCook::CookConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nextArg = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            SA_LOG_ERROR("ShaderCook: argument {} requires a value", a);
            std::exit(1);
        };

        if      (a == "--scan-dir")     scanDir          = nextArg();
        else if (a == "--spv-out")      spvOutDir        = nextArg();
        else if (a == "--dispatch-out") dispatchOutDir   = nextArg();
        else if (a == "--glslc")        cfg.glslcPath    = nextArg();
        else if (a == "--reflect-tool") cfg.reflToolPath = nextArg();
        else if (a == "--include")      cfg.includePaths.push_back(nextArg());
        else if (a == "--force")        cfg.force        = true;
        else {
            SA_LOG_ERROR("ShaderCook: unknown argument '{}'", a);
            std::exit(1);
        }
    }

    bool ok = true;
    if (scanDir.empty())       { SA_LOG_ERROR("ShaderCook: --scan-dir required");     ok = false; }
    if (spvOutDir.empty())     { SA_LOG_ERROR("ShaderCook: --spv-out required");      ok = false; }
    if (dispatchOutDir.empty()){ SA_LOG_ERROR("ShaderCook: --dispatch-out required"); ok = false; }
    if (cfg.glslcPath.empty()) { SA_LOG_ERROR("ShaderCook: --glslc required");        ok = false; }
    if (!ok) { Core::Log::Shutdown(); return 1; }

    // Auto-detect ShaderReflectTool alongside own executable.
    if (cfg.reflToolPath.empty()) {
        fs::path own = fs::path(argv[0]).parent_path();
#ifdef _WIN32
        fs::path candidate = own / "ShaderReflectTool.exe";
#else
        fs::path candidate = own / "ShaderReflectTool";
#endif
        if (fs::exists(candidate))
            cfg.reflToolPath = candidate.string();
        else {
            SA_LOG_ERROR("ShaderCook: --reflect-tool not specified and ShaderReflectTool not found alongside executable");
            Core::Log::Shutdown();
            return 1;
        }
    }

    ShaderCook::CookDirectory(scanDir, spvOutDir, dispatchOutDir, cfg);

    Core::Log::Shutdown();
    return 0;
}
