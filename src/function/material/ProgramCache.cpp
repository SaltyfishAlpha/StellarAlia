#include "function/material/ProgramCache.hpp"

#include <fstream>

#include "core/logs/Log.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"

namespace StellarAlia {

namespace {

std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

// Resolve a shader file in primaryDir, falling back to fallbackDir if missing.
std::string Resolve(const std::string& primaryDir, const std::string& fallbackDir,
                    const std::string& rel) {
    std::string p = primaryDir + "/" + rel;
    if (std::ifstream(p, std::ios::binary)) return p;
    if (!fallbackDir.empty()) return fallbackDir + "/" + rel;
    return p;
}

} // namespace

void ProgramCache::Init(RHI::IRHIDevice*         device,
                        RHI::RHIDescLayoutHandle frameLayout,
                        RHI::RHIDescLayoutHandle bindlessLayout,
                        std::string              shaderDir) {
    m_device         = device;
    m_frameLayout    = frameLayout;
    m_bindlessLayout = bindlessLayout;
    m_shaderDir      = std::move(shaderDir);
}

void ProgramCache::Shutdown() {
    for (auto& [k, e] : m_compute)  if (e.prog) e.prog->Unload(m_device);
    for (auto& [k, e] : m_graphics) if (e.prog) e.prog->Unload(m_device);
    m_compute.clear();
    m_graphics.clear();
    m_device = nullptr;
}

ComputeProgram* ProgramCache::GetCompute(const std::string& stem,
                                         bool useFrameLayout,
                                         bool projectScope,
                                         const std::string& primaryDir,
                                         const std::string& fallbackDir) {
    if (auto it = m_compute.find(stem); it != m_compute.end())
        return it->second.prog.get();

    // Empty primaryDir → engine builtin dir (existing callers, e.g. SSR/AutoExposure).
    const std::string spvPath  = primaryDir.empty() ? (m_shaderDir + "/" + stem + ".comp.spv")
                                                     : Resolve(primaryDir, fallbackDir, stem + ".comp.spv");
    const std::string reflPath = primaryDir.empty() ? (m_shaderDir + "/" + stem + ".comp.refl")
                                                     : Resolve(primaryDir, fallbackDir, stem + ".comp.refl");
    const auto spv = LoadSpv(spvPath);
    if (spv.empty()) {
        SA_LOG_WARN("ProgramCache: compute spv not found '{}.comp.spv'", stem);
        return nullptr;
    }
    RHI::ShaderReflection refl;
    if (!RHI::ShaderReflectionIO::LoadFromFile(reflPath, refl))
        SA_LOG_WARN("ProgramCache: compute refl not found '{}.comp.refl'", stem);

    auto prog = std::make_unique<ComputeProgram>();
    ComputeProgram::Desc d{spv, refl, useFrameLayout ? m_frameLayout : RHI::RHIDescLayoutHandle{}};
    if (!prog->Load(m_device, d)) {
        SA_LOG_WARN("ProgramCache: ComputeProgram load failed '{}'", stem);
        return nullptr;
    }
    ComputeProgram* raw = prog.get();
    m_compute.emplace(stem, Entry<ComputeProgram>{std::move(prog), projectScope});
    return raw;
}

ShaderProgram* ProgramCache::GetGraphics(const std::string& key,
                                         const std::string& vertStem,
                                         const std::string& fragStem,
                                         const std::string& primaryDir,
                                         const std::string& fallbackDir,
                                         bool               projectScope) {
    if (auto it = m_graphics.find(key); it != m_graphics.end())
        return it->second.prog.get();

    const auto vertSpv = LoadSpv(Resolve(primaryDir, fallbackDir, vertStem + ".vert.spv"));
    const auto fragSpv = LoadSpv(Resolve(primaryDir, fallbackDir, fragStem + ".frag.spv"));
    if (vertSpv.empty() || fragSpv.empty()) {
        SA_LOG_ERROR("ProgramCache: graphics spv not found ({} / {})", vertStem, fragStem);
        return nullptr;
    }
    RHI::ShaderReflection vertRefl, fragRefl;
    if (!RHI::ShaderReflectionIO::LoadFromFile(Resolve(primaryDir, fallbackDir, vertStem + ".vert.refl"), vertRefl) ||
        !RHI::ShaderReflectionIO::LoadFromFile(Resolve(primaryDir, fallbackDir, fragStem + ".frag.refl"), fragRefl)) {
        SA_LOG_ERROR("ProgramCache: graphics refl not found ({} / {})", vertStem, fragStem);
        return nullptr;
    }

    auto prog = std::make_unique<ShaderProgram>();
    ShaderProgram::Desc pd;
    pd.vertSpv = vertSpv; pd.vertRefl = vertRefl;
    pd.fragSpv = fragSpv; pd.fragRefl = fragRefl;
    pd.frameLayout    = m_frameLayout;
    pd.bindlessLayout = m_bindlessLayout;
    if (!prog->Load(m_device, pd)) {
        SA_LOG_ERROR("ProgramCache: ShaderProgram load failed '{}'", key);
        return nullptr;
    }
    ShaderProgram* raw = prog.get();
    m_graphics.emplace(key, Entry<ShaderProgram>{std::move(prog), projectScope});
    return raw;
}

bool ProgramCache::ReloadGraphicsFrag(const std::string&           key,
                                      std::span<const uint8_t>     fragSpv,
                                      const RHI::ShaderReflection& fragRefl) {
    auto it = m_graphics.find(key);
    if (it == m_graphics.end() || !it->second.prog) return false;
    return it->second.prog->ReloadFragShader(m_device, fragSpv, fragRefl);
}

void ProgramCache::ClearProjectPrograms() {
    for (auto it = m_compute.begin(); it != m_compute.end();) {
        if (it->second.project) { if (it->second.prog) it->second.prog->Unload(m_device); it = m_compute.erase(it); }
        else ++it;
    }
    for (auto it = m_graphics.begin(); it != m_graphics.end();) {
        if (it->second.project) { if (it->second.prog) it->second.prog->Unload(m_device); it = m_graphics.erase(it); }
        else ++it;
    }
}

} // namespace StellarAlia
