#include "function/render_graph/RenderGraph.hpp"
#include "core/logs/Log.hpp"

namespace StellarAlia {

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

    // For each texture, record which pass (index) last writes it (-1 = none).
    std::vector<int32_t> textureWriter(m_textures.size(), -1);
    for (uint32_t p = 0; p < passCount; p++)
        for (auto& we : m_passes[p].writes)
            if (we.handle.IsValid())
                textureWriter[we.handle.index] = static_cast<int32_t>(p);

    // Build directed edges: for each pass p that reads a texture written by q,
    // add an edge q → p (p depends on q).
    std::vector<uint32_t>              inDegree(passCount, 0);
    std::vector<std::vector<uint32_t>> dependents(passCount);

    for (uint32_t p = 0; p < passCount; p++) {
        for (auto rgt : m_passes[p].reads) {
            if (!rgt.IsValid()) continue;
            int32_t writer = textureWriter[rgt.index];
            if (writer < 0 || static_cast<uint32_t>(writer) == p) continue;
            dependents[writer].push_back(p);
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
        for (auto& we : pass.writes) {
            if (!we.handle.IsValid()) continue;
            auto rhi = resources.m_resolved[we.handle.index];
            if (!rhi.IsValid()) continue;
            if (states[we.handle.index] != we.targetState) {
                cmd.TransitionTexture(rhi, states[we.handle.index], we.targetState);
                states[we.handle.index] = we.targetState;
            }
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
}

} // namespace StellarAlia
