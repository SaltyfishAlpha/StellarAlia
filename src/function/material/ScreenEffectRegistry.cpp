#include "function/material/ScreenEffectRegistry.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "core/logs/Log.hpp"
#include "function/material/ComputeProgram.hpp"
#include "function/material/ProgramCache.hpp"
#include "function/material/ShaderProgram.hpp"
#include "function/renderer/RenderFeature.hpp"   // FeatureInitContext
#include "platform/rhi/ShaderReflectionIO.hpp"

namespace StellarAlia {

namespace fs = std::filesystem;

// ── Injection-point enum string mapping ───────────────────────────────────────
const char* EffectInjectName(EffectInject e) noexcept {
    switch (e) {
        case EffectInject::AfterLighting: return "AfterLighting";
        case EffectInject::AfterTAA:      return "AfterTAA";
        case EffectInject::BeforeTonemap: return "BeforeTonemap";
        case EffectInject::AfterTonemap:  return "AfterTonemap";
    }
    return "AfterTonemap";
}

bool ParseEffectInject(std::string_view s, EffectInject& out) noexcept {
    if (s == "AfterLighting") { out = EffectInject::AfterLighting; return true; }
    if (s == "AfterTAA")      { out = EffectInject::AfterTAA;      return true; }
    if (s == "BeforeTonemap") { out = EffectInject::BeforeTonemap; return true; }
    if (s == "AfterTonemap")  { out = EffectInject::AfterTonemap;  return true; }
    return false;
}

namespace {

// Parse "hdr:sampled,depth:sampled" (or "hdr,ldr") into resource entries.
// kind suffix optional; `defaultStorage` used when no ":kind".
void ParseResources(const std::string& csv, std::vector<ScreenEffectResource>& out,
                    bool defaultStorage) {
    size_t i = 0;
    while (i < csv.size()) {
        size_t comma = csv.find(',', i);
        std::string tok = csv.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
        i = (comma == std::string::npos) ? csv.size() : comma + 1;
        if (tok.empty()) continue;
        ScreenEffectResource r;
        if (size_t colon = tok.find(':'); colon != std::string::npos) {
            r.name    = tok.substr(0, colon);
            r.storage = (tok.substr(colon + 1) == "storage");
        } else {
            r.name    = tok;
            r.storage = defaultStorage;
        }
        if (!r.name.empty()) out.push_back(std::move(r));
    }
}

// Extract @Param layout from the set=2 binding=0 EffectParams UBO (same convention
// as MaterialParams). Reuses ShaderMemberDesc annotation metadata → ParamDef.
void ExtractParams(const RHI::ShaderReflection& merged, ScreenEffectType& eff) {
    auto ubo = merged.FindBinding(2, 0);
    if (!ubo || ubo->type != RHI::RHIDescriptorType::UniformBuffer) return;
    eff.paramUboSize = ubo->blockSize;
    for (const auto& m : ubo->members) {
        if (!m.name.empty() && m.name[0] == '_') continue;  // padding
        ParamDef pd;
        pd.name        = m.name;
        pd.offset      = m.offset;
        pd.size        = m.size;
        pd.uiType      = m.uiType;
        pd.displayName = m.displayName;
        pd.minValue    = m.minValue;
        pd.maxValue    = m.maxValue;
        std::copy(std::begin(m.defaultValue), std::end(m.defaultValue), std::begin(pd.defaultValue));
        eff.params.push_back(std::move(pd));
    }
    // Seed the global param blob with defaults.
    eff.paramBlob.assign(eff.paramUboSize, 0u);
    for (const auto& p : eff.params)
        if (p.offset + p.size <= eff.paramUboSize)
            std::memcpy(eff.paramBlob.data() + p.offset, p.defaultValue,
                        std::min<size_t>(p.size, sizeof(p.defaultValue)));
}

// "grayscale.saeffect.frag.refl" → stem "grayscale.saeffect" (strip .frag/.comp + .refl)
std::string DeriveStem(const fs::path& reflPath) {
    std::string s = reflPath.filename().string();      // grayscale.saeffect.frag.refl
    auto drop = [&](const char* suffix) {
        const size_t n = std::char_traits<char>::length(suffix);
        if (s.size() >= n && s.compare(s.size() - n, n, suffix) == 0) s.resize(s.size() - n);
    };
    drop(".refl");
    drop(".frag");
    drop(".comp");
    return s;
}

} // namespace

void ScreenEffectRegistry::Scan(const std::string& cookDir, const FeatureInitContext& ctx,
                                bool isProjectType) {
    if (cookDir.empty() || !fs::is_directory(cookDir)) return;

    for (const auto& entry : fs::directory_iterator(cookDir)) {
        const fs::path& p = entry.path();
        if (p.extension() != ".refl") continue;

        RHI::ShaderReflection refl;
        if (!RHI::ShaderReflectionIO::LoadFromFile(p, refl)) continue;

        const std::string injectStr = refl.GetMeta("inject");
        if (injectStr.empty()) continue;  // not a ScreenEffect (e.g. material/builtin .refl)

        EffectInject inject;
        if (!ParseEffectInject(injectStr, inject)) {
            SA_LOG_WARN("ScreenEffect: unknown @Inject '{}' in {}", injectStr, p.filename().string());
            continue;
        }
        const std::string name = refl.GetMeta("effect");
        if (name.empty() || Find(name)) continue;  // unnamed or already registered

        auto eff = std::make_unique<ScreenEffectType>();
        eff->name          = name;
        eff->inject        = inject;
        eff->isCompute     = (refl.GetMeta("stage") == "compute");
        eff->isProjectType = isProjectType;
        ParseResources(refl.GetMeta("in"),  eff->ins,  /*defaultStorage=*/false);
        ParseResources(refl.GetMeta("out"), eff->outs, /*defaultStorage=*/true);

        const std::string stem = DeriveStem(p);

        if (eff->isCompute) {
            // Issue #91: dir-aware GetCompute — resolve the project's cooked .comp
            // in ctx.shaderDir first, then the engine builtin dir (ctx.engineShaderDir).
            eff->computeProg = ctx.programs->GetCompute(stem, /*useFrameLayout=*/true, isProjectType,
                                                        ctx.shaderDir, ctx.engineShaderDir);
            if (!eff->computeProg) { SA_LOG_WARN("ScreenEffect '{}': compute program load failed", name); continue; }
            eff->descSet = ctx.device->AllocateDescriptorSet(eff->computeProg->GetLayout(2));
            ExtractParams(eff->computeProg->GetReflection(), *eff);
        } else {
            eff->graphicsProg = ctx.programs->GetGraphics(name, "fullscreen_tri", stem,
                                                          ctx.shaderDir, ctx.engineShaderDir, isProjectType);
            if (!eff->graphicsProg) { SA_LOG_WARN("ScreenEffect '{}': fragment program load failed", name); continue; }
            eff->descSet = ctx.device->AllocateDescriptorSet(eff->graphicsProg->GetMaterialLayout());
            ExtractParams(eff->graphicsProg->GetMergedReflection(), *eff);
        }

        // Per-effect EffectParams UBO at set=2 binding=0 (global "instance" values).
        if (eff->paramUboSize > 0) {
            RHI::RHIBufferDesc bd{};
            bd.size       = eff->paramUboSize;
            bd.usage      = RHI::RHIBufferUsage::Uniform;
            bd.cpuVisible = true;
            bd.debugName  = "EffectParams";
            eff->paramUbo = ctx.device->CreateBuffer(bd);
            ctx.device->UploadBufferData(eff->paramUbo, eff->paramBlob.data(), eff->paramUboSize);
            ctx.device->WriteDescriptorBuffer(eff->descSet, 0, eff->paramUbo, 0, eff->paramUboSize);
        }

        SA_LOG_INFO("ScreenEffect: registered '{}' (stage={}, inject={})",
                    name, eff->isCompute ? "compute" : "fragment", EffectInjectName(inject));
        m_effects.push_back(std::move(eff));
    }
}

std::vector<ScreenEffectType*> ScreenEffectRegistry::GetByInject(EffectInject inject) {
    std::vector<ScreenEffectType*> out;
    for (auto& e : m_effects)
        if (e->enabled && e->inject == inject) out.push_back(e.get());
    return out;
}

ScreenEffectType* ScreenEffectRegistry::Find(std::string_view name) {
    for (auto& e : m_effects)
        if (e->name == name) return e.get();
    return nullptr;
}

void ScreenEffectRegistry::ClearProjectEffects(RHI::IRHIDevice* device) {
    // Programs themselves are freed by ProgramCache::ClearProjectPrograms; here we
    // drop the project-scoped type entries and free their per-effect GPU resources.
    std::erase_if(m_effects, [device](const std::unique_ptr<ScreenEffectType>& e) {
        if (!e->isProjectType) return false;
        if (device) {
            if (e->paramUbo.IsValid())  device->DestroyBuffer(e->paramUbo);
            if (e->descSet.IsValid())   device->FreeDescriptorSet(e->descSet);
        }
        return true;
    });
}

} // namespace StellarAlia
