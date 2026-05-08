// StellarAliaShaderCook
//
// Processes .saglsl unified shader source files into compiled SPIR-V, reflection
// data, and the deferred shading dispatch GLSL used by the lighting pass.
//
// A .saglsl file contains two named sections separated by #pragma markers:
//
//   // @ShaderName  "My Shader"      human-readable label
//   // @ShadingModel MyShader        CamelCase type name (used in .mat files)
//   // @VertShader   deferred_geometry  (optional, default shown)
//
//   #pragma sa_section gbuffer
//   ... complete GLSL fragment shader (compiled to SPIR-V) ...
//   #pragma sa_end_section
//
//   #pragma sa_section lighting
//   vec3 EvaluateShading(GBufferData gbuf) { ... }
//   #pragma sa_end_section
//
// Usage:
//   StellarAliaShaderCook
//       --scan-dir     <dir>          directory containing .saglsl files
//       --spv-out      <dir>          output directory for .spv / .refl
//       --dispatch-out <dir>          output directory for generated GLSL dispatch
//       --glslc        <path>         path to glslc executable
//       --reflect-tool <path>         path to ShaderReflectTool executable
//       --include      <dir>          additional glslc -I path (repeatable)
//       [--force]                     recompile even if outputs are up to date

#include "core/logs/Log.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ─── String helpers ───────────────────────────────────────────────────────────

static std::string Trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// "SimpleAlbedo" → "simple_albedo"
static std::string CamelToSnake(const std::string& camel) {
    std::string r;
    for (size_t i = 0; i < camel.size(); ++i) {
        const char c = camel[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)))
            r += '_';
        r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}

// "simple_albedo" → "SIMPLE_ALBEDO"
static std::string SnakeToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// "simple_albedo" → "SimpleAlbedo"
static std::string SnakeToCamel(const std::string& snake) {
    std::string r;
    bool cap = true;
    for (const char c : snake) {
        if (c == '_') { cap = true; }
        else if (cap) { r += static_cast<char>(std::toupper(static_cast<unsigned char>(c))); cap = false; }
        else          { r += c; }
    }
    return r;
}

// Wrap a path in double-quotes for shell commands.
// Uses generic_string() (forward slashes) to avoid Windows mixed-separator
// issues when a CMake-supplied path (forward slashes) is extended via operator/.
static std::string Q(const fs::path& p) {
    return '"' + p.generic_string() + '"';
}

// Execute a shell command.
// On Windows, std::system() calls cmd.exe /C <cmd>.  When <cmd> starts with a
// double-quoted executable path and contains further quoted arguments, cmd.exe
// strips the *first* and *last* double-quote in the string, corrupting every
// remaining quote.  Wrapping the entire command in an extra pair of outer quotes
// causes cmd.exe to strip those outer quotes and leave the inner quotes intact.
static int Exec(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

// ─── .saglsl parser ───────────────────────────────────────────────────────────

struct ShaderEntry {
    std::string shaderName;    // @ShaderName value
    std::string shadingModel;  // @ShadingModel value  (e.g. "SimpleAlbedo")
    std::string snakeName;     // CamelToSnake(shadingModel) (e.g. "simple_albedo")
    std::string vertShader;    // @VertShader value    (default: "deferred_geometry")
    std::string gbufferGlsl;
    std::string lightingGlsl;
    fs::path    sourcePath;
};

static bool ParseSaGlsl(const fs::path& path, ShaderEntry& out, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path.string(); return false; }

    out = {};
    out.vertShader  = "deferred_geometry";
    out.sourcePath  = path;

    enum class Mode { Header, GBuffer, Lighting, Done } mode = Mode::Header;
    std::string line;

    while (std::getline(f, line)) {
        const std::string trimmed = Trim(line);

        // Header annotations (only before first #pragma)
        if (mode == Mode::Header) {
            if (trimmed.rfind("// @", 0) == 0) {
                const std::string rest = Trim(trimmed.substr(4));
                const auto sp = rest.find(' ');
                if (sp != std::string::npos) {
                    const std::string key = Trim(rest.substr(0, sp));
                    std::string val       = Trim(rest.substr(sp + 1));
                    // strip optional surrounding quotes
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                        val = val.substr(1, val.size() - 2);
                    if      (key == "ShaderName")   out.shaderName   = val;
                    else if (key == "ShadingModel")  out.shadingModel = val;
                    else if (key == "VertShader")    out.vertShader   = val;
                }
                continue;
            }
            if (trimmed == "#pragma sa_section gbuffer")  { mode = Mode::GBuffer;  continue; }
            if (trimmed == "#pragma sa_section lighting") { mode = Mode::Lighting; continue; }
            continue; // skip blank / comment lines before first section
        }

        if (trimmed == "#pragma sa_end_section") { mode = Mode::Header; continue; }
        if (trimmed == "#pragma sa_section gbuffer")  { mode = Mode::GBuffer;  continue; }
        if (trimmed == "#pragma sa_section lighting") { mode = Mode::Lighting; continue; }

        if (mode == Mode::GBuffer)  out.gbufferGlsl  += line + '\n';
        if (mode == Mode::Lighting) out.lightingGlsl += line + '\n';
    }

    if (out.shadingModel.empty()) { err = "missing @ShadingModel in " + path.string(); return false; }
    if (out.gbufferGlsl.empty())  { err = "missing #pragma sa_section gbuffer in " + path.string(); return false; }
    if (out.lightingGlsl.empty()) { err = "missing #pragma sa_section lighting in " + path.string(); return false; }

    out.snakeName  = CamelToSnake(out.shadingModel);
    if (out.shaderName.empty()) out.shaderName = out.shadingModel;
    return true;
}

// ─── Dispatch generation ─────────────────────────────────────────────────────
// C++ port of cmake/GenerateShadingDispatch.cmake

static bool GenerateDispatch(const std::vector<ShaderEntry>& entries,
                              const fs::path& dispatchDir) {
    fs::create_directories(dispatchDir);
    fs::create_directories(dispatchDir / "evaluators");

    std::string idsStr;
    idsStr += "// AUTO-GENERATED by StellarAliaShaderCook — do not edit\n";
    idsStr += "// Shading model IDs written to RT2.a by *.gbuffer.frag shaders.\n";
    idsStr += "#define SHADING_MODEL_PBR 0u\n";

    std::string dispStr;
    dispStr += "// AUTO-GENERATED by StellarAliaShaderCook — do not edit\n";
    dispStr += "// Requires: GBufferData struct and shading_model_ids.glsl already in scope.\n\n";

    std::string switchStr;

    uint32_t id = 1;
    for (const auto& e : entries) {
        // Copy evaluator into dispatch-out/evaluators/
        const fs::path evalDst = dispatchDir / "evaluators" / (e.snakeName + ".lighting.glsl");
        {
            std::ofstream ef(evalDst);
            if (!ef) {
                SA_LOG_ERROR("ShaderCook: cannot write evaluator '{}'", evalDst.string());
                return false;
            }
            ef << e.lightingGlsl;
        }

        const std::string camel = SnakeToCamel(e.snakeName);
        const std::string upper = SnakeToUpper(e.snakeName);

        idsStr += "#define SHADING_MODEL_" + upper + " " + std::to_string(id) + "u\n";

        dispStr += "// " + e.snakeName + " — model " + std::to_string(id) + "\n";
        dispStr += "#define EvaluateShading Evaluate_" + camel + "\n";
        dispStr += "#include \"evaluators/" + e.snakeName + ".lighting.glsl\"\n";
        dispStr += "#undef  EvaluateShading\n\n";

        switchStr += "        case SHADING_MODEL_" + upper
                   + ": { out_color = Evaluate_" + camel + "(gbuf); return true; }\n";

        ++id;
    }

    dispStr += "// Returns true for custom (non-PBR) shading models.\n";
    dispStr += "bool DispatchShadingModel(uint modelID, GBufferData gbuf, out vec3 out_color) {\n";
    dispStr += "    switch (modelID) {\n";
    dispStr += switchStr;
    dispStr += "    }\n";
    dispStr += "    return false;\n";
    dispStr += "}\n";

    auto writeIfChanged = [](const fs::path& p, const std::string& content) -> bool {
        // Read existing to avoid unnecessary writes (preserves mtime when unchanged).
        if (fs::exists(p)) {
            std::ifstream in(p);
            std::string existing((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
            if (existing == content) return true;
        }
        std::ofstream out(p);
        if (!out) return false;
        out << content;
        return out.good();
    };

    if (!writeIfChanged(dispatchDir / "shading_model_ids.glsl", idsStr)) {
        SA_LOG_ERROR("ShaderCook: cannot write shading_model_ids.glsl");
        return false;
    }
    if (!writeIfChanged(dispatchDir / "shading_dispatch.glsl", dispStr)) {
        SA_LOG_ERROR("ShaderCook: cannot write shading_dispatch.glsl");
        return false;
    }

    SA_LOG_INFO("ShaderCook: dispatch updated ({} custom model(s))", entries.size());
    return true;
}

// ─── Shader compilation ───────────────────────────────────────────────────────

static bool CompileEntry(const ShaderEntry& entry,
                          const fs::path& spvOutDir,
                          const std::string& glslcPath,
                          const std::string& reflToolPath,
                          const std::vector<std::string>& includePaths,
                          bool force) {
    fs::create_directories(spvOutDir);

    // Output paths follow the convention expected by RegisterTypeFromShaders:
    //   fragShader = "simple_albedo.gbuffer"  →  simple_albedo.gbuffer.frag.spv
    const std::string stem  = entry.snakeName + ".gbuffer";
    const fs::path fragSrc  = spvOutDir / (stem + ".frag");       // extracted GLSL
    const fs::path spvPath  = spvOutDir / (stem + ".frag.spv");
    const fs::path reflPath = spvOutDir / (stem + ".frag.refl");

    // Write extracted .frag source (also serves as --glsl arg for reflection).
    {
        std::ofstream f(fragSrc);
        if (!f) {
            SA_LOG_ERROR("ShaderCook: cannot write '{}'", fragSrc.string());
            return false;
        }
        f << entry.gbufferGlsl;
    }

    if (!force && fs::exists(spvPath) && fs::exists(reflPath)) {
        const auto srcMtime = fs::last_write_time(entry.sourcePath);
        if (fs::last_write_time(spvPath) >= srcMtime &&
            fs::last_write_time(reflPath) >= srcMtime) {
            SA_LOG_INFO("ShaderCook: '{}' up to date", entry.snakeName);
            return true;
        }
    }

    // Build glslc include args
    std::string includes;
    for (const auto& inc : includePaths)
        includes += " -I\"" + inc + "\"";

    // Compile GLSL → SPIR-V
    const std::string glslcCmd =
        Q(glslcPath)
        + " -fshader-stage=frag"
        + includes
        + " " + Q(fragSrc)
        + " -o " + Q(spvPath);

    SA_LOG_INFO("ShaderCook: compiling {}.frag ...", stem);
    if (const int ret = Exec(glslcCmd); ret != 0) {
        SA_LOG_ERROR("ShaderCook: glslc failed (exit {}): {}", ret, glslcCmd);
        // Delete stale outputs so the runtime does not load a broken shader.
        std::error_code ec;
        fs::remove(spvPath,  ec);
        fs::remove(reflPath, ec);
        return false;
    }

    // Reflect SPIR-V → .refl
    const std::string reflCmd =
        Q(reflToolPath)
        + " --spv "  + Q(spvPath)
        + " --out "  + Q(reflPath)
        + " --glsl " + Q(fragSrc);

    if (const int ret = Exec(reflCmd); ret != 0) {
        SA_LOG_ERROR("ShaderCook: ShaderReflectTool failed (exit {})", ret);
        std::error_code ec;
        fs::remove(spvPath,  ec);
        fs::remove(reflPath, ec);
        return false;
    }

    // Inject shadingModel + vertShader into the .refl so MaterialManager can
    // auto-register the type at runtime by scanning *.gbuffer.frag.refl files.
    {
        using namespace StellarAlia::RHI;
        ShaderReflection refl;
        if (ShaderReflectionIO::LoadFromFile(reflPath, refl)) {
            refl.shadingModel = entry.shadingModel;
            refl.vertShader   = entry.vertShader;
            if (!ShaderReflectionIO::SaveToFile(reflPath, refl)) {
                SA_LOG_WARN("ShaderCook: could not re-write '{}'", reflPath.string());
            }
        } else {
            SA_LOG_WARN("ShaderCook: could not read '{}' for metadata injection", reflPath.string());
        }
    }

    SA_LOG_INFO("ShaderCook: cooked '{}' → {}", entry.shadingModel, spvPath.filename().string());
    return true;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    using namespace StellarAlia;
    Core::Log::Initialize();

    // ── Argument parsing ───────────────────────────────────────────────────────
    fs::path              scanDir, spvOutDir, dispatchOutDir;
    std::string           glslcPath, reflToolPath;
    std::vector<std::string> includePaths;
    bool force = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nextArg = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            SA_LOG_ERROR("ShaderCook: argument {} requires a value", a);
            std::exit(1);
        };

        if      (a == "--scan-dir")     scanDir      = nextArg();
        else if (a == "--spv-out")      spvOutDir    = nextArg();
        else if (a == "--dispatch-out") dispatchOutDir = nextArg();
        else if (a == "--glslc")        glslcPath    = nextArg();
        else if (a == "--reflect-tool") reflToolPath = nextArg();
        else if (a == "--include")      includePaths.push_back(nextArg());
        else if (a == "--force")        force = true;
        else {
            SA_LOG_ERROR("ShaderCook: unknown argument '{}'", a);
            std::exit(1);
        }
    }

    // Validate required args
    bool ok = true;
    if (scanDir.empty())      { SA_LOG_ERROR("ShaderCook: --scan-dir required");     ok = false; }
    if (spvOutDir.empty())    { SA_LOG_ERROR("ShaderCook: --spv-out required");      ok = false; }
    if (dispatchOutDir.empty()){ SA_LOG_ERROR("ShaderCook: --dispatch-out required"); ok = false; }
    if (glslcPath.empty())    { SA_LOG_ERROR("ShaderCook: --glslc required");        ok = false; }
    if (!ok) return 1;

    // Auto-detect ShaderReflectTool alongside own executable if not specified.
    if (reflToolPath.empty()) {
        fs::path own = fs::path(argv[0]).parent_path();
#ifdef _WIN32
        fs::path candidate = own / "ShaderReflectTool.exe";
#else
        fs::path candidate = own / "ShaderReflectTool";
#endif
        if (fs::exists(candidate))
            reflToolPath = candidate.string();
        else {
            SA_LOG_ERROR("ShaderCook: --reflect-tool not specified and ShaderReflectTool not found alongside executable");
            return 1;
        }
    }

    // ── Scan for .saglsl files ─────────────────────────────────────────────────
    std::vector<fs::path> sources;
    if (fs::is_directory(scanDir)) {
        for (const auto& de : fs::recursive_directory_iterator(
                 scanDir, fs::directory_options::skip_permission_denied))
            if (de.path().extension() == ".saglsl")
                sources.push_back(de.path());
    }

    if (sources.empty()) {
        SA_LOG_INFO("ShaderCook: no .saglsl files found in '{}' — generating empty dispatch",
                    scanDir.string());
        // Still generate an empty dispatch so including shaders compile.
        GenerateDispatch({}, dispatchOutDir);
        return 0;
    }

    // ── Parse all .saglsl files ────────────────────────────────────────────────
    std::vector<ShaderEntry> entries;
    std::vector<fs::path> failedSources;
    bool anyFailed = false;

    for (const auto& src : sources) {
        ShaderEntry entry;
        std::string err;
        if (!ParseSaGlsl(src, entry, err)) {
            SA_LOG_ERROR("ShaderCook: parse error: {}", err);
            anyFailed = true;
            failedSources.push_back(src);
            continue;
        }
        entries.push_back(std::move(entry));
    }

    // Sort alphabetically by shading model name for stable ID assignment.
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  return a.shadingModel < b.shadingModel;
              });

    // ── First-pass dispatch: generate IDs for all parsed entries so each gbuffer
    // shader can include shading_model_ids.glsl with its own ID defined.
    if (!GenerateDispatch(entries, dispatchOutDir)) return 1;

    // ── Compile each entry ─────────────────────────────────────────────────────
    // Failed entries are written to cook_errors.txt so the editor can surface
    // per-shader diagnostics without parsing stdout.
    std::vector<ShaderEntry> goodEntries;
    for (const auto& entry : entries) {
        if (!CompileEntry(entry, spvOutDir, glslcPath, reflToolPath, includePaths, force)) {
            anyFailed = true;
            failedSources.push_back(entry.sourcePath);
        } else {
            goodEntries.push_back(entry);
        }
    }

    // ── Second-pass dispatch: regenerate with only successful entries so that
    // builtin shaders (deferred_geometry, deferred_lighting) never include a
    // broken evaluator and can always compile even when user shaders are broken.
    if (anyFailed) {
        GenerateDispatch(goodEntries, dispatchOutDir);
    }

    // Write / clear the error manifest in dispatchOutDir.
    const fs::path errorManifest = dispatchOutDir / "cook_errors.txt";
    {
        std::error_code ec;
        if (failedSources.empty()) {
            fs::remove(errorManifest, ec);
        } else {
            std::ofstream mf(errorManifest);
            for (const auto& p : failedSources)
                mf << p.generic_string() << '\n';
        }
    }

    Core::Log::Shutdown();
    return 0; // errors are reported via cook_errors.txt manifest, not exit code
}
