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

RHI::RHIBufferHandle RGResources::GetBuffer(RGBufferHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_resolvedBuffers.size())
        return {};
    return m_resolvedBuffers[handle.index];
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

void RGPassBuilder::ReadDepthStencil(RGTextureHandle tex) {
    // Recorded as a "write" so the barrier path transitions to the explicit
    // DepthRead state (the plain read path hardcodes ShaderRead). No content
    // is modified; ordering against the depth producer comes from the pass's
    // other read edges.
    m_writes.push_back({tex, RHI::RHIResourceState::DepthRead});
}

void RGPassBuilder::WriteUAV(RGTextureHandle tex) {
    m_writes.push_back({tex, RHI::RHIResourceState::UnorderedAccess});
}

void RGPassBuilder::ReadUAV(RGTextureHandle tex) {
    // Same barrier as Read — ShaderRead covers both sampled and post-UAV reads.
    m_reads.push_back(tex);
}

void RGPassBuilder::ReadBuffer(RGBufferHandle buf) {
    m_bufferAccesses.push_back({buf, RHI::RHIBufferState::StorageRead});
}

void RGPassBuilder::WriteBuffer(RGBufferHandle buf) {
    m_bufferAccesses.push_back({buf, RHI::RHIBufferState::StorageWrite});
}

void RGPassBuilder::ReadIndirectBuffer(RGBufferHandle buf) {
    m_bufferAccesses.push_back({buf, RHI::RHIBufferState::IndirectRead});
}

// ─────────────────────────────────────────────────────────────────────────────
// RenderGraph
// ─────────────────────────────────────────────────────────────────────────────
void RenderGraph::Reset() {
    m_textures.clear();
    m_buffers.clear();
    m_passes.clear();
    m_sortedPassIndices.clear();
    // m_slots / m_bufferSlots intentionally NOT cleared — physical handles persist.
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

RGBufferHandle RenderGraph::CreateBuffer(const std::string& name, const RGBufferDesc& desc) {
    RGBufferHandle h{static_cast<uint32_t>(m_buffers.size())};
    BufferEntry entry{};
    entry.name       = name;
    entry.desc       = desc;
    entry.isImported = false;
    m_buffers.push_back(std::move(entry));
    return h;
}

RGBufferHandle RenderGraph::ImportBuffer(const std::string& name,
                                          RHI::RHIBufferHandle handle,
                                          RHI::RHIBufferState  initialState) {
    RGBufferHandle h{static_cast<uint32_t>(m_buffers.size())};
    BufferEntry entry{};
    entry.name       = name;
    entry.imported   = handle;
    entry.initState  = initialState;
    entry.isImported = true;
    m_buffers.push_back(std::move(entry));
    return h;
}

RHI::RHIBufferHandle RenderGraph::GetResolvedBufferHandle(RGBufferHandle h) const {
    if (!h.IsValid() || h.index >= static_cast<uint32_t>(m_buffers.size())) return {};
    const auto& b = m_buffers[h.index];
    if (b.isImported) return b.imported;
    if (b.slotIndex < 0 || b.slotIndex >= static_cast<int>(m_bufferSlots.size())) return {};
    return m_bufferSlots[b.slotIndex].handle;
}

void RenderGraph::AddPass(const std::string& name, SetupFn setup, ExecuteFn execute) {
    RGPassBuilder builder;
    setup(builder);

    PassEntry pass{};
    pass.name           = name;
    pass.reads          = std::move(builder.m_reads);
    pass.writes         = std::move(builder.m_writes);
    pass.bufferAccesses = std::move(builder.m_bufferAccesses);
    pass.execute        = std::move(execute);
    m_passes.push_back(std::move(pass));
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile — Kahn's topological sort over texture/buffer dependencies,
//           followed by lifetime analysis and greedy slot coloring.
// ─────────────────────────────────────────────────────────────────────────────
void RenderGraph::Compile() {
    const uint32_t passCount = static_cast<uint32_t>(m_passes.size());
    if (passCount == 0) return;

    // Build directed edges: for each pass p that reads texture t, find the
    // most recent pass q < p that writes t and add edge q → p.
    // Using "most recent writer before p" (not "last global writer") avoids
    // false cycles in ping-pong patterns where two passes alternate writing
    // the same texture pair.
    std::vector<uint32_t>              inDegree(passCount, 0);
    std::vector<std::vector<uint32_t>> dependents(passCount);

    auto addEdge = [&](uint32_t from, uint32_t to) {
        dependents[from].push_back(to);
        inDegree[to]++;
    };

    // Texture read-after-write edges.
    for (uint32_t p = 0; p < passCount; p++) {
        for (auto rgt : m_passes[p].reads) {
            if (!rgt.IsValid()) continue;
            int32_t writer = -1;
            for (int32_t q = static_cast<int32_t>(p) - 1; q >= 0; --q) {
                for (const auto& we : m_passes[q].writes) {
                    if (we.handle.IsValid() && we.handle.index == rgt.index) {
                        writer = q; break;
                    }
                }
                if (writer >= 0) break;
            }
            if (writer >= 0) addEdge(static_cast<uint32_t>(writer), p);
        }
    }

    // Buffer dependency edges: any access to the same buffer must be ordered
    // after the most recent previous access to the same buffer handle.
    for (uint32_t p = 0; p < passCount; p++) {
        for (const auto& ba : m_passes[p].bufferAccesses) {
            if (!ba.handle.IsValid()) continue;
            int32_t prev = -1;
            for (int32_t q = static_cast<int32_t>(p) - 1; q >= 0; --q) {
                for (const auto& qba : m_passes[q].bufferAccesses) {
                    if (qba.handle.IsValid() && qba.handle.index == ba.handle.index) {
                        prev = q; break;
                    }
                }
                if (prev >= 0) break;
            }
            if (prev >= 0) addEdge(static_cast<uint32_t>(prev), p);
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
    }

    // ── Phase A — texture lifetime analysis ──────────────────────────────────
    for (auto& t : m_textures) {
        t.firstWritePass = -1;
        t.lastReadPass   = -1;
        t.slotIndex      = -1;
    }
    const int sortedN = static_cast<int>(m_sortedPassIndices.size());
    for (int si = 0; si < sortedN; ++si) {
        const auto& pass = m_passes[m_sortedPassIndices[si]];
        for (const auto& we : pass.writes) {
            if (!we.handle.IsValid()) continue;
            auto& t = m_textures[we.handle.index];
            if (t.firstWritePass < 0) t.firstWritePass = si;
            t.lastReadPass = si;
        }
        for (auto rgt : pass.reads) {
            if (!rgt.IsValid()) continue;
            m_textures[rgt.index].lastReadPass = si;
        }
    }

    // ── Phase A — buffer lifetime analysis ───────────────────────────────────
    for (auto& b : m_buffers) {
        b.firstWritePass = -1;
        b.lastReadPass   = -1;
        b.slotIndex      = -1;
    }
    for (int si = 0; si < sortedN; ++si) {
        const auto& pass = m_passes[m_sortedPassIndices[si]];
        for (const auto& ba : pass.bufferAccesses) {
            if (!ba.handle.IsValid()) continue;
            auto& b = m_buffers[ba.handle.index];
            const bool isWrite = (ba.state == RHI::RHIBufferState::StorageWrite
                               || ba.state == RHI::RHIBufferState::CopyDst);
            if (isWrite && b.firstWritePass < 0) b.firstWritePass = si;
            b.lastReadPass = si;
        }
    }

    // ── Phase B — texture greedy interval coloring ───────────────────────────
    struct CandEntry { int fw; uint32_t ti; };
    std::vector<CandEntry> cands;
    cands.reserve(m_textures.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_textures.size()); ++i) {
        const auto& t = m_textures[i];
        if (!t.isImported && t.firstWritePass >= 0)
            cands.push_back({t.firstWritePass, i});
    }
    std::sort(cands.begin(), cands.end(),
              [](const CandEntry& a, const CandEntry& b) { return a.fw < b.fw; });

    for (auto& s : m_slots) s.freeAfterPass = -1;

    auto texCompatible = [](const RHI::RHITextureDesc& slot, const RHI::RHITextureDesc& tex) {
        return slot.format    == tex.format
            && slot.width     == tex.width
            && slot.height    == tex.height
            && slot.mipLevels == tex.mipLevels
            && (static_cast<uint32_t>(slot.usage) & static_cast<uint32_t>(tex.usage))
               == static_cast<uint32_t>(tex.usage);
    };

    for (const auto& c : cands) {
        auto& t = m_textures[c.ti];
        bool assigned = false;
        for (int si = 0; si < static_cast<int>(m_slots.size()); ++si) {
            auto& slot = m_slots[si];
            if (texCompatible(slot.desc, t.desc) && slot.freeAfterPass < t.firstWritePass) {
                t.slotIndex        = si;
                slot.freeAfterPass = t.lastReadPass;
                assigned           = true;
                break;
            }
        }
        if (!assigned) {
            t.slotIndex = static_cast<int>(m_slots.size());
            RGPhysicalSlot ns{};
            ns.desc          = t.desc;
            ns.freeAfterPass = t.lastReadPass;
            m_slots.push_back(std::move(ns));
        }
    }

    // ── Phase B — buffer greedy interval coloring ─────────────────────────────
    struct BufCandEntry { int fw; uint32_t bi; };
    std::vector<BufCandEntry> bufCands;
    bufCands.reserve(m_buffers.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_buffers.size()); ++i) {
        const auto& b = m_buffers[i];
        // Buffers with clearOnCreate participate from firstWritePass=0 even if
        // only accessed by a single pass. Imported buffers skip slot assignment.
        if (!b.isImported && (b.firstWritePass >= 0 || b.desc.clearOnCreate))
            bufCands.push_back({b.firstWritePass >= 0 ? b.firstWritePass : 0, i});
    }
    std::sort(bufCands.begin(), bufCands.end(),
              [](const BufCandEntry& a, const BufCandEntry& b) { return a.fw < b.fw; });

    for (auto& s : m_bufferSlots) s.freeAfterPass = -1;

    auto bufCompatible = [](const RGBufferDesc& slot, const RGBufferDesc& req) {
        return slot.size       >= req.size
            && slot.cpuVisible == req.cpuVisible
            && (static_cast<uint32_t>(slot.usage) & static_cast<uint32_t>(req.usage))
               == static_cast<uint32_t>(req.usage);
    };

    for (const auto& c : bufCands) {
        auto& b = m_buffers[c.bi];
        const int fw = b.firstWritePass >= 0 ? b.firstWritePass : c.fw;
        bool assigned = false;
        for (int si = 0; si < static_cast<int>(m_bufferSlots.size()); ++si) {
            auto& slot = m_bufferSlots[si];
            if (bufCompatible(slot.desc, b.desc) && slot.freeAfterPass < fw) {
                b.slotIndex        = si;
                slot.freeAfterPass = b.lastReadPass;
                assigned           = true;
                break;
            }
        }
        if (!assigned) {
            b.slotIndex = static_cast<int>(m_bufferSlots.size());
            RGPhysicalBufferSlot ns{};
            ns.desc          = b.desc;
            ns.freeAfterPass = b.lastReadPass;
            m_bufferSlots.push_back(std::move(ns));
        }
    }

    SA_LOG_TRACE("RenderGraph::Compile — {} passes, {} tex logical ({} slots), {} buf logical ({} slots)",
                 passCount,
                 static_cast<uint32_t>(cands.size()),
                 static_cast<uint32_t>(m_slots.size()),
                 static_cast<uint32_t>(bufCands.size()),
                 static_cast<uint32_t>(m_bufferSlots.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// AllocateSlots / InvalidateSlots / InvalidateBufferSlots / GetResolvedHandle
// ─────────────────────────────────────────────────────────────────────────────
void RenderGraph::AllocateSlots(RHI::IRHIDevice& device) {
    for (auto& slot : m_slots) {
        if (!slot.handle.IsValid())
            slot.handle = device.CreateTexture(slot.desc);
    }
    for (auto& slot : m_bufferSlots) {
        if (!slot.handle.IsValid()) {
            RHI::RHIBufferDesc rhiDesc{};
            rhiDesc.size       = slot.desc.size;
            rhiDesc.usage      = slot.desc.usage;
            rhiDesc.cpuVisible = slot.desc.cpuVisible;
            rhiDesc.debugName  = slot.desc.debugName;
            slot.handle = device.CreateBuffer(rhiDesc);
        }
    }
}

void RenderGraph::InvalidateSlots(RHI::IRHIDevice& device) {
    for (auto& slot : m_slots)
        if (slot.handle.IsValid())
            device.DestroyTexture(slot.handle);
    m_slots.clear();
}

void RenderGraph::InvalidateBufferSlots(RHI::IRHIDevice& device) {
    for (auto& slot : m_bufferSlots)
        if (slot.handle.IsValid())
            device.DestroyBuffer(slot.handle);
    m_bufferSlots.clear();
}

RHI::RHITextureHandle RenderGraph::GetResolvedHandle(RGTextureHandle h) const {
    if (!h.IsValid() || h.index >= static_cast<uint32_t>(m_textures.size())) return {};
    const auto& t = m_textures[h.index];
    if (t.isImported) return t.imported;
    if (t.slotIndex < 0 || t.slotIndex >= static_cast<int>(m_slots.size())) return {};
    return m_slots[t.slotIndex].handle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute — emit barriers and call execute lambdas in sorted order
// ─────────────────────────────────────────────────────────────────────────────
void RenderGraph::Execute(RHI::IRHIDevice& device, RHI::IRHICommandList& cmd) {
    // Ensure all transient textures/buffers have backing GPU memory.
    AllocateSlots(device);

    const uint32_t texCount = static_cast<uint32_t>(m_textures.size());
    const uint32_t bufCount = static_cast<uint32_t>(m_buffers.size());

    // ── Resolved handle tables ────────────────────────────────────────────────
    RGResources resources;
    resources.m_resolved.resize(texCount);
    for (uint32_t i = 0; i < texCount; i++) {
        const auto& t = m_textures[i];
        if (t.isImported) {
            resources.m_resolved[i] = t.imported;
        } else if (t.slotIndex >= 0 && t.slotIndex < static_cast<int>(m_slots.size())) {
            resources.m_resolved[i] = m_slots[t.slotIndex].handle;
        } else {
            resources.m_resolved[i] = {};
        }
    }
    resources.m_resolvedBuffers.resize(bufCount);
    for (uint32_t i = 0; i < bufCount; i++) {
        const auto& b = m_buffers[i];
        if (b.isImported) {
            resources.m_resolvedBuffers[i] = b.imported;
        } else if (b.slotIndex >= 0 && b.slotIndex < static_cast<int>(m_bufferSlots.size())) {
            resources.m_resolvedBuffers[i] = m_bufferSlots[b.slotIndex].handle;
        } else {
            resources.m_resolvedBuffers[i] = {};
        }
    }

    // ── Texture state tracking ────────────────────────────────────────────────
    // State is per physical resource (slot for transients, logical for imported).
    std::vector<RHI::RHIResourceState> states(texCount, RHI::RHIResourceState::Undefined);
    for (uint32_t i = 0; i < texCount; i++)
        if (m_textures[i].isImported)
            states[i] = m_textures[i].initState;

    std::vector<RHI::RHIResourceState> slotStates(
        m_slots.size(), RHI::RHIResourceState::Undefined);

    auto getTexState = [&](uint32_t idx) -> RHI::RHIResourceState& {
        const auto& t = m_textures[idx];
        if (t.isImported) return states[idx];
        return slotStates[static_cast<uint32_t>(t.slotIndex)];
    };

    std::vector<bool> writtenImportedTex(texCount, false);
    std::vector<bool> writtenSlot(m_slots.size(), false);

    auto wasTexWritten = [&](uint32_t idx) -> bool {
        const auto& t = m_textures[idx];
        if (t.isImported) return writtenImportedTex[idx];
        return writtenSlot[static_cast<uint32_t>(t.slotIndex)];
    };
    auto markTexWritten = [&](uint32_t idx) {
        const auto& t = m_textures[idx];
        if (t.isImported) writtenImportedTex[idx] = true;
        else              writtenSlot[static_cast<uint32_t>(t.slotIndex)] = true;
    };

    // ── Buffer state tracking ─────────────────────────────────────────────────
    std::vector<RHI::RHIBufferState> bufStates(bufCount, RHI::RHIBufferState::Undefined);
    for (uint32_t i = 0; i < bufCount; i++)
        if (m_buffers[i].isImported)
            bufStates[i] = m_buffers[i].initState;

    std::vector<RHI::RHIBufferState> bufSlotStates(
        m_bufferSlots.size(), RHI::RHIBufferState::Undefined);

    auto getBufState = [&](uint32_t idx) -> RHI::RHIBufferState& {
        const auto& b = m_buffers[idx];
        if (b.isImported) return bufStates[idx];
        return bufSlotStates[static_cast<uint32_t>(b.slotIndex)];
    };

    // clearPending: tracks which transient buffers with clearOnCreate still need
    // their zero-fill before first use.
    std::vector<bool> clearPending(bufCount, false);
    for (uint32_t i = 0; i < bufCount; i++)
        if (!m_buffers[i].isImported && m_buffers[i].desc.clearOnCreate)
            clearPending[i] = true;

    std::vector<bool> writtenImportedBuf(bufCount, false);
    std::vector<bool> writtenBufSlot(m_bufferSlots.size(), false);

    auto wasBufWritten = [&](uint32_t idx) -> bool {
        const auto& b = m_buffers[idx];
        if (b.isImported) return writtenImportedBuf[idx];
        return writtenBufSlot[static_cast<uint32_t>(b.slotIndex)];
    };
    auto markBufWritten = [&](uint32_t idx) {
        const auto& b = m_buffers[idx];
        if (b.isImported) writtenImportedBuf[idx] = true;
        else              writtenBufSlot[static_cast<uint32_t>(b.slotIndex)] = true;
    };

    auto isBufWrite = [](RHI::RHIBufferState s) {
        return s == RHI::RHIBufferState::StorageWrite
            || s == RHI::RHIBufferState::CopyDst;
    };

    // ── Pass execution ────────────────────────────────────────────────────────
    for (uint32_t pi : m_sortedPassIndices) {
        const auto& pass = m_passes[pi];

        // ── Texture read barriers ─────────────────────────────────────────────
        // When the same texture appears in both reads and writes for this pass,
        // the Read is an "ordering-only" dependency (e.g. compositing onto the
        // swapchain after Tonemap): the topo sort needs the edge, but we must
        // NOT transition to SHADER_READ — the texture may not have been created
        // with SAMPLED_BIT (the swapchain isn't), and the Write block below will
        // emit the required write-after-write memory barrier.
        for (auto rgt : pass.reads) {
            if (!rgt.IsValid()) continue;
            auto rhi = resources.m_resolved[rgt.index];
            if (!rhi.IsValid()) continue;
            bool alsoWritten = false;
            for (const auto& we : pass.writes) {
                if (we.handle.IsValid() && we.handle.index == rgt.index) {
                    alsoWritten = true;
                    break;
                }
            }
            if (alsoWritten) continue;
            constexpr auto kReadState = RHI::RHIResourceState::ShaderRead;
            auto& cur = getTexState(rgt.index);
            if (cur != kReadState) {
                cmd.TransitionTexture(rhi, cur, kReadState);
                cur = kReadState;
            }
        }

        // ── Texture write barriers ────────────────────────────────────────────
        for (auto& we : pass.writes) {
            if (!we.handle.IsValid()) continue;
            auto rhi = resources.m_resolved[we.handle.index];
            if (!rhi.IsValid()) continue;
            auto& cur = getTexState(we.handle.index);
            if (cur != we.targetState || wasTexWritten(we.handle.index)) {
                cmd.TransitionTexture(rhi, cur, we.targetState);
                cur = we.targetState;
            }
            markTexWritten(we.handle.index);
        }

        // ── Buffer barriers (+ clearOnCreate zero-fill) ───────────────────────
        for (const auto& ba : pass.bufferAccesses) {
            if (!ba.handle.IsValid()) continue;
            auto rhi = resources.m_resolvedBuffers[ba.handle.index];
            if (!rhi.IsValid()) continue;
            auto& cur = getBufState(ba.handle.index);

            if (clearPending[ba.handle.index]) {
                // Zero-fill, then transition from CopyDst to the required state.
                clearPending[ba.handle.index] = false;
                cmd.FillBuffer(rhi, 0, m_buffers[ba.handle.index].desc.size, 0u);
                cmd.BufferBarrier(rhi, RHI::RHIBufferState::CopyDst, ba.state);
                cur = ba.state;
            } else if (cur != ba.state || wasBufWritten(ba.handle.index)) {
                cmd.BufferBarrier(rhi, cur, ba.state);
                cur = ba.state;
            }
            if (isBufWrite(ba.state)) markBufWritten(ba.handle.index);
        }

        SA_LOG_TRACE("RenderGraph: executing pass '{}'", pass.name);
        pass.execute(cmd, resources);
    }

    // ── Epilogue: final transitions for imported textures ─────────────────────
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

    // ── Stats ─────────────────────────────────────────────────────────────────
    m_lastStats = {};
    m_lastStats.entries.reserve(texCount);
    for (uint32_t i = 0; i < texCount; i++) {
        const auto& t = m_textures[i];
        if (t.isImported) {
            m_lastStats.importedCount++;
            if (const RHI::RHITextureDesc* d = device.GetTextureDesc(t.imported))
                m_lastStats.importedBytesLogical += CalcTextureBytes(*d);
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
            e.slotIndex = t.slotIndex;
            m_lastStats.entries.push_back(e);
        }
    }
    // Count only physical texture slots actually used by a logical texture this frame.
    // m_slots persists across frames (peak-pool semantics), so reporting m_slots.size()
    // directly would surface stale slots from previous frames as "physical" — e.g.
    // toggling DoF off leaves DoF-shaped slots in the pool but they no longer back any
    // logical texture this frame.
    std::vector<bool> usedTexSlots(m_slots.size(), false);
    for (uint32_t i = 0; i < texCount; i++) {
        const auto& t = m_textures[i];
        if (!t.isImported && t.slotIndex >= 0 &&
            t.slotIndex < static_cast<int>(m_slots.size()))
            usedTexSlots[static_cast<uint32_t>(t.slotIndex)] = true;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i) {
        if (usedTexSlots[i]) {
            m_lastStats.physicalSlotCount++;
            m_lastStats.transientBytesPhysical += CalcTextureBytes(m_slots[i].desc);
        }
    }

    m_lastStats.bufferEntries.reserve(bufCount);
    for (uint32_t i = 0; i < bufCount; i++) {
        const auto& b = m_buffers[i];
        if (b.isImported) {
            m_lastStats.importedBufferCount++;
        } else {
            m_lastStats.transientBufferCount++;
            m_lastStats.transientBufferBytesLogical += b.desc.size;
            RGStats::BufferEntry be;
            be.name      = b.name;
            be.bytes     = b.desc.size;
            be.slotIndex = b.slotIndex;
            m_lastStats.bufferEntries.push_back(be);
        }
    }
    // Count only physical buffer slots actually used by a logical buffer this frame,
    // so physicalBufferSlotCount = 0 when no transient buffers are created (e.g. AE off).
    std::vector<bool> usedBufSlots(m_bufferSlots.size(), false);
    for (uint32_t i = 0; i < bufCount; i++) {
        const auto& b = m_buffers[i];
        if (!b.isImported && b.slotIndex >= 0 &&
            b.slotIndex < static_cast<int>(m_bufferSlots.size()))
            usedBufSlots[static_cast<uint32_t>(b.slotIndex)] = true;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_bufferSlots.size()); ++i) {
        if (usedBufSlots[i]) {
            m_lastStats.physicalBufferSlotCount++;
            m_lastStats.transientBufferBytesPhysical += m_bufferSlots[i].desc.size;
        }
    }

    m_lastMemStats = device.GetMemoryStats();
}

} // namespace StellarAlia
