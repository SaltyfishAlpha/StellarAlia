#include "shader_cook/ShaderCookLib.hpp"

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

namespace StellarAlia::ShaderCook {

// ── String helpers ────────────────────────────────────────────────────────────

static std::string Trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

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

static std::string SnakeToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

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
static std::string Q(const fs::path& p) {
    return '"' + p.generic_string() + '"';
}
static std::string Q(const std::string& s) {
    return '"' + s + '"';
}

// Execute a shell command and return its exit code.
// On Windows, std::system() calls cmd.exe /C <cmd>.  When <cmd> starts with a
// double-quoted executable path and contains further quoted arguments, cmd.exe
// strips the first and last double-quote.  Wrapping the entire command in an
// extra pair of outer quotes causes cmd.exe to strip those and leave the inner
// quotes intact.
static int Exec(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

// ── .saglsl parser ────────────────────────────────────────────────────────────

struct ShaderEntry {
    std::string shaderName;
    std::string shadingModel;
    std::string snakeName;
    std::string vertShader;
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

    enum class Mode { Header, GBuffer, Lighting } mode = Mode::Header;
    std::string line;

    while (std::getline(f, line)) {
        const std::string trimmed = Trim(line);

        if (mode == Mode::Header) {
            if (trimmed.rfind("// @", 0) == 0) {
                const std::string rest = Trim(trimmed.substr(4));
                const auto sp = rest.find(' ');
                if (sp != std::string::npos) {
                    const std::string key = Trim(rest.substr(0, sp));
                    std::string val       = Trim(rest.substr(sp + 1));
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
            continue;
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

// ── Dispatch generation ───────────────────────────────────────────────────────

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

// ── Shader compilation ────────────────────────────────────────────────────────

static bool CompileEntry(const ShaderEntry& entry,
                          const fs::path& spvOutDir,
                          const CookConfig& cfg) {
    fs::create_directories(spvOutDir);

    const std::string stem  = entry.snakeName + ".gbuffer";
    const fs::path fragSrc  = spvOutDir / (stem + ".frag");
    const fs::path spvPath  = spvOutDir / (stem + ".frag.spv");
    const fs::path reflPath = spvOutDir / (stem + ".frag.refl");

    {
        std::ofstream f(fragSrc);
        if (!f) {
            SA_LOG_ERROR("ShaderCook: cannot write '{}'", fragSrc.string());
            return false;
        }
        f << entry.gbufferGlsl;
    }

    if (!cfg.force && fs::exists(spvPath) && fs::exists(reflPath)) {
        const auto srcMtime = fs::last_write_time(entry.sourcePath);
        if (fs::last_write_time(spvPath) >= srcMtime &&
            fs::last_write_time(reflPath) >= srcMtime) {
            SA_LOG_INFO("ShaderCook: '{}' up to date", entry.snakeName);
            return true;
        }
    }

    std::string includes;
    for (const auto& inc : cfg.includePaths)
        includes += " -I" + Q(inc);

    const std::string glslcCmd =
        Q(cfg.glslcPath)
        + " -fshader-stage=frag"
        + includes
        + " " + Q(fragSrc)
        + " -o " + Q(spvPath);

    SA_LOG_INFO("ShaderCook: compiling {}.frag ...", stem);
    if (const int ret = Exec(glslcCmd); ret != 0) {
        SA_LOG_ERROR("ShaderCook: glslc failed (exit {}): {}", ret, glslcCmd);
        std::error_code ec;
        fs::remove(spvPath,  ec);
        fs::remove(reflPath, ec);
        return false;
    }

    if (!cfg.reflToolPath.empty()) {
        const std::string reflCmd =
            Q(cfg.reflToolPath)
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

        // Inject shadingModel + vertShader into the .refl.
        {
            RHI::ShaderReflection refl;
            if (RHI::ShaderReflectionIO::LoadFromFile(reflPath, refl)) {
                refl.SetMeta("shadingModel", entry.shadingModel);
                refl.SetMeta("vertShader",   entry.vertShader);
                if (!RHI::ShaderReflectionIO::SaveToFile(reflPath, refl))
                    SA_LOG_WARN("ShaderCook: could not re-write '{}'", reflPath.string());
            } else {
                SA_LOG_WARN("ShaderCook: could not read '{}' for metadata injection", reflPath.string());
            }
        }
    }

    SA_LOG_INFO("ShaderCook: cooked '{}' → {}", entry.shadingModel, spvPath.filename().string());
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool HasSaglslFiles(const std::filesystem::path& scanDir) {
    if (!fs::is_directory(scanDir)) return false;
    for (const auto& de : fs::recursive_directory_iterator(
             scanDir, fs::directory_options::skip_permission_denied))
        if (de.path().extension() == ".saglsl")
            return true;
    return false;
}

CookResult CookDirectory(const fs::path& scanDir,
                          const fs::path& spvOutDir,
                          const fs::path& dispatchOutDir,
                          const CookConfig& cfg) {
    CookResult result;

    // Collect .saglsl files
    std::vector<fs::path> sources;
    if (fs::is_directory(scanDir)) {
        for (const auto& de : fs::recursive_directory_iterator(
                 scanDir, fs::directory_options::skip_permission_denied))
            if (de.path().extension() == ".saglsl")
                sources.push_back(de.path());
    }

    if (sources.empty()) {
        // Generate empty dispatch so including shaders always compile.
        GenerateDispatch({}, dispatchOutDir);
        return result;  // success=true, modelCount=0
    }

    // Parse
    std::vector<ShaderEntry> entries;
    for (const auto& src : sources) {
        ShaderEntry entry;
        std::string err;
        if (!ParseSaGlsl(src, entry, err)) {
            SA_LOG_ERROR("ShaderCook: parse error: {}", err);
            result.failedModels.push_back(src.stem().string());
        } else {
            entries.push_back(std::move(entry));
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.shadingModel < b.shadingModel; });

    // First-pass dispatch: all parsed entries get IDs before any compilation,
    // so each gbuffer shader can include shading_model_ids.glsl with its own ID.
    if (!GenerateDispatch(entries, dispatchOutDir)) {
        result.success = false;
        return result;
    }

    // Compile entries
    std::vector<ShaderEntry> goodEntries;
    for (const auto& entry : entries) {
        if (!CompileEntry(entry, spvOutDir, cfg)) {
            result.failedModels.push_back(entry.shadingModel);
        } else {
            goodEntries.push_back(entry);
        }
    }

    // Second-pass dispatch: only successful entries, so the lighting pass
    // never includes a broken evaluator.
    if (!result.failedModels.empty())
        GenerateDispatch(goodEntries, dispatchOutDir);

    // Write / clear cook_errors.txt
    const fs::path errorManifest = dispatchOutDir / "cook_errors.txt";
    {
        std::error_code ec;
        if (result.failedModels.empty()) {
            fs::remove(errorManifest, ec);
        } else {
            std::ofstream mf(errorManifest);
            for (const auto& m : result.failedModels)
                mf << m << '\n';
        }
    }

    result.modelCount = static_cast<int>(goodEntries.size());
    return result;
}

bool RecompileDeferredLighting(const fs::path&              fragSrcPath,
                                const fs::path&              dispatchDir,
                                const fs::path&              outSpvPath,
                                const std::string&           glslcPath,
                                const std::vector<std::string>& includePaths) {
    if (glslcPath.empty()) {
        SA_LOG_WARN("ShaderCook: glslc path empty — cannot recompile deferred_lighting.frag");
        return false;
    }
    if (!fs::exists(fragSrcPath)) {
        SA_LOG_ERROR("ShaderCook: deferred_lighting.frag not found at '{}'", fragSrcPath.string());
        return false;
    }

    std::error_code ec;
    fs::create_directories(outSpvPath.parent_path(), ec);

    std::string includes = " -I" + Q(dispatchDir);
    for (const auto& inc : includePaths)
        includes += " -I" + Q(inc);

    const std::string cmd =
        Q(glslcPath)
        + " -fshader-stage=frag"
        + includes
        + " " + Q(fragSrcPath)
        + " -o " + Q(outSpvPath);

    SA_LOG_INFO("ShaderCook: recompiling deferred_lighting.frag with project dispatch...");
    if (const int ret = Exec(cmd); ret != 0) {
        SA_LOG_ERROR("ShaderCook: glslc deferred_lighting.frag failed (exit {})", ret);
        return false;
    }

    SA_LOG_INFO("ShaderCook: deferred_lighting.frag.spv written to '{}'", outSpvPath.string());
    return true;
}

// ── .saeffect parser + compiler (Issue #88 ScreenEffect) ───────────────────────
//
// A .saeffect declares one screen pass: header annotations (@Effect/@Stage/
// @Inject/@In/@Out) + a single `#pragma sa_section fragment|compute` body.
// @Param values are inline UBO-member annotations (set=2 binding=0), parsed by
// ShaderReflectTool from the source, exactly as materials do — no cook handling.
// Unlike .saglsl there is no dispatch generation; each effect is standalone.

struct EffectEntry {
    std::string name;                 // @Effect
    std::string stage;                // "fragment" | "compute" (from #pragma sa_section)
    std::string inject;               // @Inject
    std::vector<std::string> ins;     // @In tokens, e.g. "hdr:sampled"
    std::vector<std::string> outs;    // @Out tokens, e.g. "hdr"
    std::string body;                 // shader GLSL
    std::string stem;                 // source filename incl. ext, e.g. "grayscale.saeffect"
    fs::path    sourcePath;
};

// Normalize "hdr : sampled" → "hdr:sampled"; "hdr" → "hdr".
static std::string NormalizeResourceToken(const std::string& raw) {
    const auto colon = raw.find(':');
    if (colon == std::string::npos) return Trim(raw);
    return Trim(raw.substr(0, colon)) + ":" + Trim(raw.substr(colon + 1));
}

static bool ParseSaEffect(const fs::path& path, EffectEntry& out, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path.string(); return false; }

    out = {};
    out.sourcePath = path;
    out.stem       = path.filename().string();  // "grayscale.saeffect"

    enum class Mode { Header, Body } mode = Mode::Header;
    std::string line;
    std::string headerStage;  // optional @Stage; validated against section marker

    while (std::getline(f, line)) {
        const std::string trimmed = Trim(line);

        if (mode == Mode::Header) {
            if (trimmed.rfind("// @", 0) == 0) {
                const std::string rest = Trim(trimmed.substr(4));
                const auto sp = rest.find(' ');
                if (sp != std::string::npos) {
                    const std::string key = Trim(rest.substr(0, sp));
                    std::string val       = Trim(rest.substr(sp + 1));
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                        val = val.substr(1, val.size() - 2);
                    if      (key == "Effect") out.name    = val;
                    else if (key == "Stage")  headerStage = val;
                    else if (key == "Inject") out.inject  = val;
                    else if (key == "In")     out.ins.push_back(NormalizeResourceToken(val));
                    else if (key == "Out")    out.outs.push_back(NormalizeResourceToken(val));
                }
                continue;
            }
            if (trimmed == "#pragma sa_section fragment") { out.stage = "fragment"; mode = Mode::Body; continue; }
            if (trimmed == "#pragma sa_section compute")  { out.stage = "compute";  mode = Mode::Body; continue; }
            continue;
        }

        if (trimmed == "#pragma sa_end_section") { mode = Mode::Header; continue; }
        out.body += line + '\n';
    }

    if (out.name.empty())   { err = "missing @Effect in " + path.string(); return false; }
    if (out.inject.empty()) { err = "missing @Inject in " + path.string(); return false; }
    if (out.stage.empty())  { err = "missing '#pragma sa_section fragment|compute' in " + path.string(); return false; }
    if (out.body.empty())   { err = "empty shader body in " + path.string(); return false; }
    if (!headerStage.empty() && headerStage != out.stage)
        SA_LOG_WARN("ShaderCook: @Stage '{}' disagrees with section '{}' in {} — using section",
                    headerStage, out.stage, path.filename().string());
    return true;
}

static bool CompileEffectEntry(const EffectEntry& entry,
                               const fs::path& spvOutDir,
                               const CookConfig& cfg) {
    fs::create_directories(spvOutDir);

    const bool        isCompute = (entry.stage == "compute");
    const std::string kind      = isCompute ? "comp" : "frag";
    const fs::path    src       = spvOutDir / (entry.stem + "." + kind);
    const fs::path    spvPath   = spvOutDir / (entry.stem + "." + kind + ".spv");
    const fs::path    reflPath  = spvOutDir / (entry.stem + "." + kind + ".refl");

    {
        std::ofstream f(src);
        if (!f) { SA_LOG_ERROR("ShaderCook: cannot write '{}'", src.string()); return false; }
        f << entry.body;
    }

    if (!cfg.force && fs::exists(spvPath) && fs::exists(reflPath)) {
        const auto srcMtime = fs::last_write_time(entry.sourcePath);
        if (fs::last_write_time(spvPath) >= srcMtime &&
            fs::last_write_time(reflPath) >= srcMtime) {
            SA_LOG_INFO("ShaderCook: effect '{}' up to date", entry.name);
            return true;
        }
    }

    std::string includes;
    for (const auto& inc : cfg.includePaths)
        includes += " -I" + Q(inc);

    const std::string glslcCmd =
        Q(cfg.glslcPath)
        + (isCompute ? " -fshader-stage=comp" : " -fshader-stage=frag")
        + includes
        + " " + Q(src)
        + " -o " + Q(spvPath);

    SA_LOG_INFO("ShaderCook: compiling effect {}.{} ...", entry.stem, kind);
    if (const int ret = Exec(glslcCmd); ret != 0) {
        SA_LOG_ERROR("ShaderCook: glslc failed (exit {}): {}", ret, glslcCmd);
        std::error_code ec; fs::remove(spvPath, ec); fs::remove(reflPath, ec);
        return false;
    }

    if (!cfg.reflToolPath.empty()) {
        const std::string reflCmd =
            Q(cfg.reflToolPath)
            + " --spv "  + Q(spvPath)
            + " --out "  + Q(reflPath)
            + " --glsl " + Q(src);
        if (const int ret = Exec(reflCmd); ret != 0) {
            SA_LOG_ERROR("ShaderCook: ShaderReflectTool failed (exit {})", ret);
            std::error_code ec; fs::remove(spvPath, ec); fs::remove(reflPath, ec);
            return false;
        }

        auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) { if (i) s += ','; s += v[i]; }
            return s;
        };

        // Standard .refl + metadata keys (mirrors materials' shadingModel injection).
        RHI::ShaderReflection refl;
        if (RHI::ShaderReflectionIO::LoadFromFile(reflPath, refl)) {
            refl.SetMeta("effect", entry.name);
            refl.SetMeta("stage",  entry.stage);
            refl.SetMeta("inject", entry.inject);
            refl.SetMeta("in",     join(entry.ins));
            refl.SetMeta("out",    join(entry.outs));
            if (!RHI::ShaderReflectionIO::SaveToFile(reflPath, refl))
                SA_LOG_WARN("ShaderCook: could not re-write '{}'", reflPath.string());
        } else {
            SA_LOG_WARN("ShaderCook: could not read '{}' for metadata injection", reflPath.string());
        }
    }

    SA_LOG_INFO("ShaderCook: cooked effect '{}' → {}", entry.name, spvPath.filename().string());
    return true;
}

bool HasSaeffectFiles(const fs::path& scanDir) {
    if (!fs::is_directory(scanDir)) return false;
    for (const auto& de : fs::recursive_directory_iterator(
             scanDir, fs::directory_options::skip_permission_denied))
        if (de.path().extension() == ".saeffect")
            return true;
    return false;
}

CookResult CookEffects(const fs::path& scanDir,
                       const fs::path& spvOutDir,
                       const CookConfig& cfg) {
    CookResult result;

    std::vector<fs::path> sources;
    if (fs::is_directory(scanDir)) {
        for (const auto& de : fs::recursive_directory_iterator(
                 scanDir, fs::directory_options::skip_permission_denied))
            if (de.path().extension() == ".saeffect")
                sources.push_back(de.path());
    }
    if (sources.empty()) return result;  // success=true, modelCount=0

    std::vector<EffectEntry> good;
    for (const auto& src : sources) {
        EffectEntry entry;
        std::string err;
        if (!ParseSaEffect(src, entry, err)) {
            SA_LOG_ERROR("ShaderCook: parse error: {}", err);
            result.failedModels.push_back(src.stem().string());
            continue;
        }
        if (!CompileEffectEntry(entry, spvOutDir, cfg)) {
            result.failedModels.push_back(entry.name);
            continue;
        }
        good.push_back(std::move(entry));
    }

    result.modelCount = static_cast<int>(good.size());
    return result;
}

} // namespace StellarAlia::ShaderCook
