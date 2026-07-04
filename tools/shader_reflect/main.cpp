// ShaderReflectTool
//
// Reads a compiled SPIR-V file, extracts descriptor bindings and push-constant
// information via SPIRV-Cross, and writes a binary .refl sidecar understood by
// StellarAlia::RHI::ShaderReflectionIO.
//
// Usage:  ShaderReflectTool --spv <input.spv> --out <output.refl> [--glsl <source.glsl>]
//
// When --glsl is provided, the tool also parses GLSL @Type("Display Name") = defaults
// annotations from source comments and embeds them in the .refl (version 4).
// Stage is inferred from the SPIR-V execution model embedded in the binary.

#include "core/logs/Log.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"

// spirv-cross-core provides spirv_cross::Compiler — sufficient for resource
// reflection without pulling in any cross-compilation backend.
#include <spirv_cross.hpp>
#include <spirv_common.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;

// ── GLSL annotation parser ────────────────────────────────────────────────────

namespace {

struct GLSLAnnotation {
    ParamUIType uiType          = ParamUIType::Inferred;
    std::string displayName;
    float       minValue        = 0.f;
    float       maxValue        = 1.f;
    float       defaultValue[4] = {};
    bool        isTexture       = false;
};

// Parse "@Type(...) = defaults" from a GLSL comment string.
// Returns true and fills `out` if a recognised annotation is found.
static bool ParseAnnotation(const std::string& comment, GLSLAnnotation& out) {
    const size_t at = comment.find('@');
    if (at == std::string::npos) return false;

    size_t i = at + 1;
    const size_t nameStart = i;
    while (i < comment.size() && (std::isalpha((unsigned char)comment[i]) ||
                                   std::isdigit((unsigned char)comment[i])))
        ++i;
    const std::string typeName = comment.substr(nameStart, i - nameStart);
    if (i >= comment.size() || comment[i] != '(') return false;
    ++i; // skip '('

    ParamUIType uiType = ParamUIType::Inferred;
    bool isTexture = false;
    if      (typeName == "Float")   uiType = ParamUIType::Float;
    else if (typeName == "Vec2")    uiType = ParamUIType::Vec2;
    else if (typeName == "Vec3")    uiType = ParamUIType::Vec3;
    else if (typeName == "Vec4")    uiType = ParamUIType::Vec4;
    else if (typeName == "Color3")  uiType = ParamUIType::Color3;
    else if (typeName == "Color4")  uiType = ParamUIType::Color4;
    else if (typeName == "Range")   uiType = ParamUIType::Range;
    else if (typeName == "Texture") isTexture = true;
    else return false;

    out.uiType    = uiType;
    out.isTexture = isTexture;

    float minV = 0.f, maxV = 1.f;
    if (uiType == ParamUIType::Range) {
        char* end;
        minV = std::strtof(comment.c_str() + i, &end);
        i    = static_cast<size_t>(end - comment.c_str());
        while (i < comment.size() && (comment[i] == ',' || comment[i] == ' ')) ++i;
        maxV = std::strtof(comment.c_str() + i, &end);
        i    = static_cast<size_t>(end - comment.c_str());
        while (i < comment.size() && (comment[i] == ',' || comment[i] == ' ')) ++i;
    }
    out.minValue = minV;
    out.maxValue = maxV;

    // Quoted display name
    const size_t q1 = comment.find('"', i);
    if (q1 != std::string::npos) {
        const size_t q2 = comment.find('"', q1 + 1);
        if (q2 != std::string::npos) {
            out.displayName = comment.substr(q1 + 1, q2 - q1 - 1);
            i = q2 + 1;
        }
    }

    // Default values after '='
    const size_t eq = comment.find('=', i);
    if (eq != std::string::npos) {
        size_t j = eq + 1;
        while (j < comment.size() && (comment[j] == ' ' || comment[j] == '\t')) ++j;
        int di = 0;
        while (j < comment.size() && di < 4) {
            char* end;
            out.defaultValue[di++] = std::strtof(comment.c_str() + j, &end);
            if (end == comment.c_str() + j) break;
            j = static_cast<size_t>(end - comment.c_str());
            while (j < comment.size() && (comment[j] == ',' || comment[j] == ' ')) ++j;
        }
    }

    return true;
}

// Extract the last identifier before ';' (handles arrays like float arr[4]).
static std::string ExtractVarName(const std::string& decl) {
    size_t end = decl.find(';');
    if (end == std::string::npos) end = decl.size();
    while (end > 0 && std::isspace((unsigned char)decl[end - 1])) --end;
    // Skip array dimension
    if (end > 0 && decl[end - 1] == ']') {
        const size_t lb = decl.rfind('[', end);
        if (lb != std::string::npos) end = lb;
        while (end > 0 && std::isspace((unsigned char)decl[end - 1])) --end;
    }
    size_t start = end;
    while (start > 0 && (std::isalnum((unsigned char)decl[start - 1]) ||
                         decl[start - 1] == '_'))
        --start;
    if (start >= end) return {};
    return decl.substr(start, end - start);
}

// Parse a GLSL source file and return a map of variable name → annotation.
// Follows #include "..." directives (resolved relative to the including file):
// annotated blocks shared via include (e.g. material_params_pbr.glsl, Issue #56)
// must still reach the .refl — SPIR-V carries no comments.
static void ParseGLSLAnnotationsInto(const fs::path& path,
                                     std::unordered_map<std::string, GLSLAnnotation>& result,
                                     int depth) {
    if (depth > 8) return;
    std::ifstream f(path);
    if (!f) return;

    std::string line;
    while (std::getline(f, line)) {
        const size_t incPos = line.find("#include");
        if (incPos != std::string::npos) {
            const size_t q0 = line.find('"', incPos);
            const size_t q1 = (q0 == std::string::npos) ? std::string::npos
                                                        : line.find('"', q0 + 1);
            if (q1 != std::string::npos) {
                const fs::path inc =
                    path.parent_path() / line.substr(q0 + 1, q1 - q0 - 1);
                if (fs::exists(inc)) ParseGLSLAnnotationsInto(inc, result, depth + 1);
            }
            continue;
        }

        const size_t commentPos = line.find("//");
        if (commentPos == std::string::npos) continue;
        const std::string comment = line.substr(commentPos + 2);
        if (comment.find('@') == std::string::npos) continue;

        GLSLAnnotation ann;
        if (!ParseAnnotation(comment, ann)) continue;

        const std::string varName = ExtractVarName(line.substr(0, commentPos));
        if (varName.empty()) continue;
        result[varName] = ann;
    }
}

static std::unordered_map<std::string, GLSLAnnotation>
ParseGLSLAnnotations(const fs::path& path) {
    std::unordered_map<std::string, GLSLAnnotation> result;
    ParseGLSLAnnotationsInto(path, result, 0);
    return result;
}

} // anonymous namespace

// ── helpers ───────────────────────────────────────────────────────────────────

static std::vector<uint32_t> ReadSpv(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        SA_LOG_ERROR("ShaderReflectTool: cannot open '{}'", path.string());
        return {};
    }
    const auto byteSize = static_cast<size_t>(f.tellg());
    if (byteSize == 0 || byteSize % 4 != 0) {
        SA_LOG_ERROR("ShaderReflectTool: '{}' has invalid size {}", path.string(), byteSize);
        return {};
    }
    f.seekg(0);
    std::vector<uint32_t> words(byteSize / 4);
    f.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(byteSize));
    return words;
}

static RHIShaderStage StageFromModel(spv::ExecutionModel model) {
    switch (model) {
        case spv::ExecutionModelVertex:    return RHIShaderStage::Vertex;
        case spv::ExecutionModelFragment:  return RHIShaderStage::Fragment;
        case spv::ExecutionModelGLCompute: return RHIShaderStage::Compute;
        default:                           return RHIShaderStage::None;
    }
}

// Map a SPIR-V vertex input type (float / vec2 / vec3 / vec4) to RHIVertexFormat.
// Returns Undefined for unsupported types (int/uint inputs, matrices, etc.).
static RHIVertexFormat MapSpirTypeToVertexFormat(const spirv_cross::SPIRType& t) {
    if (t.basetype != spirv_cross::SPIRType::Float) return RHIVertexFormat::Undefined;
    if (t.columns != 1)                              return RHIVertexFormat::Undefined;
    switch (t.vecsize) {
        case 1: return RHIVertexFormat::R32_SFLOAT;
        case 2: return RHIVertexFormat::R32G32_SFLOAT;
        case 3: return RHIVertexFormat::R32G32B32_SFLOAT;
        case 4: return RHIVertexFormat::R32G32B32A32_SFLOAT;
        default: return RHIVertexFormat::Undefined;
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Core::Log::Initialize();

    fs::path spvPath, outPath, glslPath;
    for (int i = 1; i < argc - 1; ++i) {
        const std::string_view arg(argv[i]);
        if      (arg == "--spv")  spvPath  = argv[++i];
        else if (arg == "--out")  outPath  = argv[++i];
        else if (arg == "--glsl") glslPath = argv[++i];
    }

    if (spvPath.empty() || outPath.empty()) {
        SA_LOG_ERROR("Usage: ShaderReflectTool --spv <file.spv> --out <file.refl> [--glsl <source.glsl>]");
        Core::Log::Shutdown();
        return 1;
    }

    // ── Read SPIR-V ───────────────────────────────────────────────────────────
    const auto words = ReadSpv(spvPath);
    if (words.empty()) {
        Core::Log::Shutdown();
        return 1;
    }

    // ── Compile + reflect ─────────────────────────────────────────────────────
    spirv_cross::Compiler compiler(words);

    // Infer stage from the first entry point's execution model.
    const auto entryPoints = compiler.get_entry_points_and_stages();
    if (entryPoints.empty()) {
        SA_LOG_ERROR("ShaderReflectTool: no entry points found in '{}'", spvPath.string());
        Core::Log::Shutdown();
        return 1;
    }
    const RHIShaderStage stage = StageFromModel(entryPoints[0].execution_model);
    if (stage == RHIShaderStage::None) {
        SA_LOG_ERROR("ShaderReflectTool: unsupported execution model in '{}'", spvPath.string());
        Core::Log::Shutdown();
        return 1;
    }

    const spirv_cross::ShaderResources res = compiler.get_shader_resources();
    ShaderReflection refl;

    // Helper: build one ShaderBindingDesc from a spirv_cross::Resource.
    auto addBinding = [&](const spirv_cross::Resource& r, RHIDescriptorType type) {
        ShaderBindingDesc bd;
        bd.set     = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        bd.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        bd.type    = type;
        bd.stages  = stage;
        bd.name    = r.name;

        // Array size (first dimension; 0 in the array vector = not an array → 1).
        const auto& typeInfo = compiler.get_type(r.type_id);
        bd.arraySize = typeInfo.array.empty() ? 1u : typeInfo.array[0];

        // Block size + per-member layout for buffer types (UBO / SSBO).
        if (type == RHIDescriptorType::UniformBuffer ||
            type == RHIDescriptorType::StorageBuffer) {
            const auto& baseType = compiler.get_type(r.base_type_id);
            bd.blockSize = static_cast<uint32_t>(
                compiler.get_declared_struct_size(baseType));

            const uint32_t memberCount =
                static_cast<uint32_t>(baseType.member_types.size());
            bd.members.reserve(memberCount);
            for (uint32_t m = 0; m < memberCount; ++m) {
                ShaderMemberDesc md;
                md.name   = compiler.get_member_name(baseType.self, m);
                md.offset = compiler.get_member_decoration(
                    baseType.self, m, spv::DecorationOffset);
                md.size   = static_cast<uint32_t>(
                    compiler.get_declared_struct_member_size(baseType, m));
                bd.members.push_back(std::move(md));
            }
        }

        refl.bindings.push_back(std::move(bd));
    };

    // Uniform buffers (UBOs) ── set=N binding=M type=UniformBuffer
    for (const auto& r : res.uniform_buffers)
        addBinding(r, RHIDescriptorType::UniformBuffer);

    // Combined image+sampler (sampler2D / samplerCube in GLSL)
    for (const auto& r : res.sampled_images) {
        const auto& typeInfo = compiler.get_type(r.type_id);
        const RHIDescriptorType dtype =
            (typeInfo.image.dim == spv::DimCube) ? RHIDescriptorType::TextureCube
                                                 : RHIDescriptorType::Texture2D;
        addBinding(r, dtype);
    }

    // Separate images (texture2D / textureCube without sampler)
    for (const auto& r : res.separate_images) {
        const auto& typeInfo = compiler.get_type(r.type_id);
        const RHIDescriptorType dtype =
            (typeInfo.image.dim == spv::DimCube) ? RHIDescriptorType::TextureCube
                                                 : RHIDescriptorType::Texture2D;
        addBinding(r, dtype);
    }

    // Separate samplers
    for (const auto& r : res.separate_samplers)
        addBinding(r, RHIDescriptorType::Sampler);

    // Storage buffers (SSBOs)
    for (const auto& r : res.storage_buffers)
        addBinding(r, RHIDescriptorType::StorageBuffer);

    // Storage images
    for (const auto& r : res.storage_images)
        addBinding(r, RHIDescriptorType::StorageImage);

    // Push constants — take the first (and typically only) block.
    for (const auto& r : res.push_constant_buffers) {
        const auto& baseType = compiler.get_type(r.base_type_id);
        refl.pushConstantSize   = static_cast<uint32_t>(
            compiler.get_declared_struct_size(baseType));
        refl.pushConstantStages = stage;
        break; // one push-constant block per stage
    }

    // Vertex stage inputs — drive VkVertexInputAttributeDescription per shader.
    // Note: SPIR-V optimizer strips declared-but-unused `in` variables, so
    // res.stage_inputs already reflects only the locations actually consumed.
    if (stage == RHIShaderStage::Vertex) {
        for (const auto& r : res.stage_inputs) {
            ShaderVertexInputDesc vd;
            vd.location = compiler.get_decoration(r.id, spv::DecorationLocation);
            vd.format   = MapSpirTypeToVertexFormat(compiler.get_type(r.type_id));
            if (vd.format == RHIVertexFormat::Undefined) {
                SA_LOG_WARN("ShaderReflectTool: unsupported vertex input '{}' (loc {}) — skipped",
                            r.name, vd.location);
                continue;
            }
            refl.vertexInputs.push_back(vd);
        }
        std::sort(refl.vertexInputs.begin(), refl.vertexInputs.end(),
                  [](const ShaderVertexInputDesc& a, const ShaderVertexInputDesc& b) {
                      return a.location < b.location;
                  });
    }

    // ── Apply GLSL annotations ────────────────────────────────────────────────
    if (!glslPath.empty()) {
        const auto annotations = ParseGLSLAnnotations(glslPath);
        for (auto& bd : refl.bindings) {
            // Texture / sampler display name
            auto texIt = annotations.find(bd.name);
            if (texIt != annotations.end() && texIt->second.isTexture)
                bd.displayName = texIt->second.displayName;

            // UBO member metadata
            for (auto& md : bd.members) {
                auto it = annotations.find(md.name);
                if (it == annotations.end()) continue;
                const GLSLAnnotation& ann = it->second;
                if (ann.isTexture) continue;
                md.uiType      = ann.uiType;
                md.displayName = ann.displayName;
                md.minValue    = ann.minValue;
                md.maxValue    = ann.maxValue;
                std::copy(std::begin(ann.defaultValue), std::end(ann.defaultValue),
                          std::begin(md.defaultValue));
            }
        }
        SA_LOG_INFO("ShaderReflectTool: applied {} annotations from '{}'",
                    annotations.size(), glslPath.filename().string());
    }

    // ── Write .refl ───────────────────────────────────────────────────────────
    if (!ShaderReflectionIO::SaveToFile(outPath, refl)) {
        SA_LOG_ERROR("ShaderReflectTool: failed to write '{}'", outPath.string());
        Core::Log::Shutdown();
        return 1;
    }

    SA_LOG_INFO("ShaderReflectTool: '{}' → '{}' ({} bindings, push={}B)",
                spvPath.filename().string(),
                outPath.filename().string(),
                refl.bindings.size(),
                refl.pushConstantSize);

    Core::Log::Shutdown();
    return 0;
}
