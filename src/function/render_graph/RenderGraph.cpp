#include "function/render_graph/RenderGraph.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>

namespace StellarAlia {

static uint64_t CalcTextureBytes(const RHI::RHITextureDesc& desc) {
    using F = RHI::RHIFormat;

    // Block-compressed: 4×4 texels per block, fixed block size in bytes.
    auto bcBytes = [&](uint64_t blockBytes) -> uint64_t {
        uint64_t total = 0;
        uint32_t w = desc.width, h = desc.height;
        for (uint32_t m = 0; m < desc.mipLevels; ++m) {
            uint32_t bw = std::max(1u, (w + 3) / 4);
            uint32_t bh = std::max(1u, (h + 3) / 4);
            total += static_cast<uint64_t>(bw) * bh * blockBytes * (desc.cubemap ? 6u : 1u);
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        }
        return total;
    };
    switch (desc.format) {
        case F::BC1_UNORM: return bcBytes(8);
        case F::BC3_UNORM: return bcBytes(16);
        case F::BC5_UNORM: return bcBytes(16);
        case F::BC7_UNORM: return bcBytes(16);
        default: break;
    }

    uint64_t bpp = 0;
    switch (desc.format) {
        case F::RGBA8_UNORM: case F::RGBA8_SRGB:
        case F::BGRA8_UNORM: case F::BGRA8_SRGB:
        case F::RG16F:       case F::R32F:
        case F::D32F:        case F::D24_S8:      bpp = 4; break;
        case F::RGBA16F:     case F::RG32F:        bpp = 8; break;
        case F::RGBA32F:                           bpp = 16; break;
        case F::R8_UNORM:                          bpp = 1; break;
        case F::D16_UNORM:                         bpp = 2; break;
        default:                                   bpp = 4; break;
    }
    uint64_t total = 0;
    uint32_t w = desc.width, h = desc.height;
    for (uint32_t m = 0; m < desc.mipLevels; ++m) {
        total += static_cast<uint64_t>(w) * h * bpp * (desc.cubemap ? 6u : 1u);
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    return total;
}

static const char* FormatName(RHI::RHIFormat f) {
    using F = RHI::RHIFormat;
    switch (f) {
        case F::RGBA8_UNORM:  return "RGBA8";
        case F::RGBA8_SRGB:   return "RGBA8_SRGB";
        case F::BGRA8_UNORM:  return "BGRA8";
        case F::BGRA8_SRGB:   return "BGRA8_SRGB";
        case F::RGBA16F:      return "RGBA16F";
        case F::RGBA32F:      return "RGBA32F";
        case F::RG16F:        return "RG16F";
        case F::RG32F:        return "RG32F";
        case F::R8_UNORM:     return "R8";
        case F::R32F:         return "R32F";
        case F::D32F:         return "D32F";
        case F::D24_S8:       return "D24_S8";
        case F::D16_UNORM:    return "D16";
        case F::BC1_UNORM:    return "BC1";
        case F::BC3_UNORM:    return "BC3";
        case F::BC5_UNORM:    return "BC5";
        case F::BC7_UNORM:    return "BC7";
        default:              return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RGResources
// ─────────────────────────────────────────────────────────────────────────────
RHI::RHITextureHandle RGResources::Get(RGTextureHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_resolved.size())
        return {};
    return m_resolved[handle.index];
}

// ─────────────────────────────────────────────────────────────────────────────
// RGPassBuilder
// ─────────────────────────────────────────────────────────────────────────────
void RGPassBuilder::Read(RGTextureHandle tex) {
    m_reads.push_back(tex);
}

void RGPassBuilder::Write(RGTextureHandle tex) {
    m_writes.push_back({tex, RHI::RHIResourceState::RenderTarget});
}

void RGPassBuilder::WriteDepth(RGTextureHandle tex) {
    m_writes.push_back({tex, RHI::RHIResourceState::DepthWrite});
}

void RGPassBuilder::WriteUAV(RGTextureHandle tex) {
    m_writes.push_back({tex, RHI::RHIResourceState::UnorderedAccess});
}

void RGPassBuilder::ReadUAV(RGTextureHandle tex) {
    // Same barrier as Read — ShaderRead covers both sampled and post-UAV reads.
    m_reads.push_back(tex);
}

// ─────────────────────────────────────────────────────────────────────────────
// RenderGraph
// ─────────────────────────────────────────────────────────────────────────────
void RenderGraph::Reset() {
    m_textures.clear();
    m_passes.clear();
    m_sortedPassIndices.clear();
}

RGTextureHandle RenderGraph::CreateTexture(const std::string& name,
                                            const RHI::RHITextureDesc& desc) {
    RGTextureHandle h{static_cast<uint32_t>(m_textures.size())};
    TextureEntry entry{};
    entry.name       = name;
    entry.desc       = desc;
    entry.isImported = false;
    m_textures.push_back(std::move(entry));
    return h;
}

RGTextureHandle RenderGraph::ImportTexture(const std::string& name,
                                            RHI::RHITextureHandle  handle,
                                            RHI::RHIResourceState  initialState,
                                            RHI::RHIResourceState  finalState) {
    RGTextureHandle h{static_cast<uint32_t>(m_textures.size())};
    TextureEntry entry{};
    entry.name       = name;
    entry.imported   = handle;
    entry.initState  = initialState;
    entry.finalState = finalState;
    entry.isImported = true;
    m_textures.push_back(std::move(entry));
    return h;
}

void RenderGraph::AddPass(const std::string& name, SetupFn setup, ExecuteFn execute) {
    RGPassBuilder builder;
    setup(builder);

    PassEntry pass{};
    pass.name    = name;
    pass.reads   = std::move(builder.m_reads);
    pass.writes  = std::move(builder.m_writes);
    pass.execute = std::move(execute);
    m_passes.push_back(std::move(pass));
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile — Kahn's topological sort over texture read/write dependencies
// ─────────────────────────────────────────────────────────────────────────────
void RenderGraph::Compile() {
    const uint32_t passCount = static_cast<uint32_t>(m_passes.size());
    if (passCount == 0) return;

    // Build directed edges: for each pass p that reads texture t, find the
    // most recent pass q < p that writes t and add edge q → p.
    // Using "most recent writer before p" (not "last global writer") avoids
    // false cycles in ping-pong patterns where two passes alternate writing
    // the same texture pair (e.g. separable blur: A writes T1, B writes T2,
    // A reads T2, B reads T1 — global-last-writer would see A→B and B→A).
    std::vector<uint32_t>              inDegree(passCount, 0);
    std::vector<std::vector<uint32_t>> dependents(passCount);

    for (uint32_t p = 0; p < passCount; p++) {
        for (auto rgt : m_passes[p].reads) {
            if (!rgt.IsValid()) continue;
            // Scan backwards to find the most recent writer before p.
            int32_t writer = -1;
            for (int32_t q = static_cast<int32_t>(p) - 1; q >= 0; --q) {
                for (const auto& we : m_passes[q].writes) {
                    if (we.handle.IsValid() && we.handle.index == rgt.index) {
                        writer = q;
                        break;
                    }
                }
                if (writer >= 0) break;
            }
            if (writer < 0) continue;
            dependents[static_cast<uint32_t>(writer)].push_back(p);
            inDegree[p]++;
        }
    }

    // Kahn's BFS
    std::vector<uint32_t> queue;
    queue.reserve(passCount);
    for (uint32_t p = 0; p < passCount; p++)
        if (inDegree[p] == 0) queue.push_back(p);

    m_sortedPassIndices.clear();
    m_sortedPassIndices.reserve(passCount);
    for (uint32_t qi = 0; qi < static_cast<uint32_t>(queue.size()); qi++) {
        uint32_t p = queue[qi];
        m_sortedPassIndices.push_back(p);
        for (uint32_t dep : dependents[p])
            if (--inDegree[dep] == 0)
                queue.push_back(dep);
    }

    if (m_sortedPassIndices.size() != passCount) {
        SA_LOG_WARN("RenderGraph::Compile — cycle detected, falling back to declaration order");
        m_sortedPassIndices.resize(passCount);
        for (uint32_t i = 0; i < passCount; i++) m_sortedPassIndices[i] = i;
        return;
    }

    SA_LOG_DEBUG("RenderGraph::Compile - {} passes sorted", passCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute — emit barriers and call execute lambdas in sorted order
// ─────────────────────────────────────────────────────────────────────────────
void RenderGraph::Execute(RHI::IRHIDevice& /*device*/, RHI::IRHICommandList& cmd) {
    const uint32_t texCount = static_cast<uint32_t>(m_textures.size());

    // Build the resolved texture table (RGTextureHandle → RHITextureHandle).
    // Transient textures are not yet backed by real GPU memory (Stage 3).
    RGResources resources;
    resources.m_resolved.resize(texCount);
    for (uint32_t i = 0; i < texCount; i++) {
        auto& t = m_textures[i];
        resources.m_resolved[i] = t.isImported ? t.imported : RHI::RHITextureHandle{};
    }

    // Track current per-texture resource state.
    std::vector<RHI::RHIResourceState> states(texCount, RHI::RHIResourceState::Undefined);
    for (uint32_t i = 0; i < texCount; i++)
        if (m_textures[i].isImported)
            states[i] = m_textures[i].initState;

    // Track which textures have been written by any previous pass this frame.
    // Even when the target state hasn't changed (e.g. RenderTarget→RenderTarget),
    // a barrier is required between two consecutive vkCmdBeginRendering blocks
    // on the same attachment to establish a memory dependency.
    std::vector<bool> writtenInPreviousPass(texCount, false);

    // Execute each pass in topological order.
    for (uint32_t pi : m_sortedPassIndices) {
        const auto& pass = m_passes[pi];

        // Emit read barriers.
        for (auto rgt : pass.reads) {
            if (!rgt.IsValid()) continue;
            auto rhi = resources.m_resolved[rgt.index];
            if (!rhi.IsValid()) continue;
            constexpr auto kReadState = RHI::RHIResourceState::ShaderRead;
            if (states[rgt.index] != kReadState) {
                cmd.TransitionTexture(rhi, states[rgt.index], kReadState);
                states[rgt.index] = kReadState;
            }
        }

        // Emit write barriers.
        // Always barrier if (a) state needs to change OR (b) a previous pass
        // already wrote this texture — even a same-layout transition acts as
        // the memory dependency required between render pass instances.
        for (auto& we : pass.writes) {
            if (!we.handle.IsValid()) continue;
            auto rhi = resources.m_resolved[we.handle.index];
            if (!rhi.IsValid()) continue;
            const bool stateChanges   = (states[we.handle.index] != we.targetState);
            const bool needsMemoryDep = writtenInPreviousPass[we.handle.index];
            if (stateChanges || needsMemoryDep) {
                cmd.TransitionTexture(rhi, states[we.handle.index], we.targetState);
                states[we.handle.index] = we.targetState;
            }
            writtenInPreviousPass[we.handle.index] = true;
        }

        SA_LOG_DEBUG("RenderGraph: executing pass '{}'", pass.name);
        pass.execute(cmd, resources);
    }

    // Emit final transitions for imported textures.
    for (uint32_t i = 0; i < texCount; i++) {
        auto& t = m_textures[i];
        if (!t.isImported) continue;
        if (t.finalState == RHI::RHIResourceState::Undefined) continue;
        if (states[i] == t.finalState) continue;
        auto rhi = resources.m_resolved[i];
        if (!rhi.IsValid()) continue;
        cmd.TransitionTexture(rhi, states[i], t.finalState);
        states[i] = t.finalState;
    }

    // Fill per-frame stats (logical only; physical == logical until aliasing #16).
    m_lastStats = {};
    m_lastStats.entries.reserve(texCount);
    for (uint32_t i = 0; i < texCount; i++) {
        const auto& t = m_textures[i];
        if (t.isImported) {
            m_lastStats.importedCount++;
        } else {
            const uint64_t bytes = CalcTextureBytes(t.desc);
            m_lastStats.transientCount++;
            m_lastStats.transientBytesLogical += bytes;
            RGStats::Entry e;
            e.name      = t.name;
            e.width     = t.desc.width;
            e.height    = t.desc.height;
            e.mipLevels = t.desc.mipLevels;
            e.formatStr = FormatName(t.desc.format);
            e.bytes     = bytes;
            m_lastStats.entries.push_back(e);
        }
    }
    m_lastStats.physicalSlotCount    = m_lastStats.transientCount;
    m_lastStats.transientBytesPhysical = m_lastStats.transientBytesLogical;
}

} // namespace StellarAlia
