// ShaderReflectTool
//
// Reads a compiled SPIR-V file, extracts descriptor bindings and push-constant
// information via SPIRV-Cross, and writes a binary .refl sidecar understood by
// StellarAlia::RHI::ShaderReflectionIO.
//
// Usage:  ShaderReflectTool --spv <input.spv> --out <output.refl>
// Stage is inferred from the SPIR-V execution model embedded in the binary.

#include "core/logs/Log.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"

// spirv-cross-core provides spirv_cross::Compiler — sufficient for resource
// reflection without pulling in any cross-compilation backend.
#include <spirv_cross.hpp>
#include <spirv_common.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;

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

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Core::Log::Initialize();

    fs::path spvPath, outPath;
    for (int i = 1; i < argc - 1; ++i) {
        const std::string_view arg(argv[i]);
        if      (arg == "--spv") spvPath = argv[++i];
        else if (arg == "--out") outPath = argv[++i];
    }

    if (spvPath.empty() || outPath.empty()) {
        SA_LOG_ERROR("Usage: ShaderReflectTool --spv <file.spv> --out <file.refl>");
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
