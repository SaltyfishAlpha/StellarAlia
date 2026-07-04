#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/IRHICommandList.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// RGTextureHandle
//
// Opaque index into the RenderGraph's texture table for a single frame.
// NOT the same as RHITextureHandle — it is frame-local and must not be stored
// across frames.
// ─────────────────────────────────────────────────────────────────────────────
struct RGTextureHandle {
    static constexpr uint32_t INVALID = ~0u;
    uint32_t index = INVALID;

    [[nodiscard]] bool IsValid() const noexcept { return index != INVALID; }
    explicit operator bool() const noexcept { return IsValid(); }
    bool operator==(const RGTextureHandle&) const noexcept = default;
};

// Write declaration: handle + the state the texture must be in before the pass.
struct RGWriteEntry {
    RGTextureHandle       handle;
    RHI::RHIResourceState targetState = RHI::RHIResourceState::RenderTarget;
};

// ─────────────────────────────────────────────────────────────────────────────
// RGBufferHandle
//
// Opaque index into the RenderGraph's buffer table for a single frame.
// NOT the same as RHIBufferHandle — it is frame-local and must not be stored
// across frames.
// ─────────────────────────────────────────────────────────────────────────────
struct RGBufferHandle {
    static constexpr uint32_t INVALID = ~0u;
    uint32_t index = INVALID;

    [[nodiscard]] bool IsValid() const noexcept { return index != INVALID; }
    explicit operator bool() const noexcept { return IsValid(); }
    bool operator==(const RGBufferHandle&) const noexcept = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// RGBufferDesc — transient buffer creation parameters.
//
// clearOnCreate: if true, RG emits FillBuffer(0) + barrier(CopyDst→target)
// before the buffer's first write pass — equivalent to loadOp=CLEAR for
// render targets. Required for histogram SSBOs and similar per-frame data.
// ─────────────────────────────────────────────────────────────────────────────
struct RGBufferDesc {
    uint64_t            size          = 0;
    RHI::RHIBufferUsage usage         = RHI::RHIBufferUsage::None;
    bool                cpuVisible    = false;
    bool                clearOnCreate = false;
    const char*         debugName     = nullptr;
};

// Buffer access declaration: which buffer + required pipeline state.
struct RGBufferAccess {
    RGBufferHandle      handle;
    RHI::RHIBufferState state = RHI::RHIBufferState::StorageRead;
};

// ─────────────────────────────────────────────────────────────────────────────
// RGResources
//
// Passed to each execute lambda. Resolves RG handles into backing RHI handles
// so the lambda can call RHI APIs (BeginRenderPass, SetDescriptorSet, etc.).
// ─────────────────────────────────────────────────────────────────────────────
class RGResources {
public:
    [[nodiscard]] RHI::RHITextureHandle Get(RGTextureHandle handle) const;
    [[nodiscard]] RHI::RHIBufferHandle  GetBuffer(RGBufferHandle handle) const;

private:
    friend class RenderGraph;
    std::vector<RHI::RHITextureHandle> m_resolved;
    std::vector<RHI::RHIBufferHandle>  m_resolvedBuffers;
};

// ─────────────────────────────────────────────────────────────────────────────
// RGPassBuilder
//
// Passed to the setup lambda of AddPass(). Declares which textures/buffers a
// pass reads or writes so Compile() can build the dependency graph.
// ─────────────────────────────────────────────────────────────────────────────
class RGPassBuilder {
public:
    // ── Texture declarations ──────────────────────────────────────────────────
    // Read: expects the texture in ShaderRead state before the pass.
    void Read(RGTextureHandle tex);

    // Write: expects the texture in RenderTarget (color attachment) state.
    void Write(RGTextureHandle tex);

    // WriteDepth: expects the texture in DepthWrite state.
    void WriteDepth(RGTextureHandle tex);

    // ReadDepthStencil (Issue #56): expects the texture in DepthRead state
    // (DEPTH_STENCIL_READ_ONLY) — lets a pass bind depth+stencil as a read-only
    // attachment (stencil test) while simultaneously sampling the depth plane.
    void ReadDepthStencil(RGTextureHandle tex);

    // WriteUAV: expects the texture in UnorderedAccess (storage image) state.
    // Use this for compute pass outputs.
    void WriteUAV(RGTextureHandle tex);

    // ReadUAV: transitions to ShaderRead before the pass.
    // Use this when a compute-written texture is consumed by a later pass as SRV.
    // (Equivalent to Read — listed separately for call-site clarity.)
    void ReadUAV(RGTextureHandle tex);

    // ── Buffer declarations ───────────────────────────────────────────────────
    // ReadBuffer: transitions to StorageRead before the pass (SSBO read-only).
    void ReadBuffer(RGBufferHandle buf);

    // WriteBuffer: transitions to StorageWrite before the pass (SSBO read+write).
    void WriteBuffer(RGBufferHandle buf);

    // ReadIndirectBuffer: transitions to IndirectRead for draw/dispatch indirect.
    void ReadIndirectBuffer(RGBufferHandle buf);

private:
    friend class RenderGraph;
    std::vector<RGTextureHandle> m_reads;
    std::vector<RGWriteEntry>    m_writes;
    std::vector<RGBufferAccess>  m_bufferAccesses;
};

// ─────────────────────────────────────────────────────────────────────────────
// RenderGraph
//
// Framegraph-style graph for a single frame. Typical usage:
//
//   rg.Reset();
//   auto rgSwapchain = rg.ImportTexture("swapchain",
//       device.GetSwapchainTexture(),
//       RHIResourceState::RenderTarget, RHIResourceState::RenderTarget);
//
//   rg.AddPass("ForwardPass",
//       [&](RGPassBuilder& b) { b.Write(rgSwapchain); },
//       [&](IRHICommandList& cmd, const RGResources& res) {
//           cmd.BeginRenderPass({...res.Get(rgSwapchain)...});
//           // draw calls
//           cmd.EndRenderPass();
//       });
//
//   rg.Compile();          // topological sort
//   rg.Execute(dev, *cmd); // emit barriers + execute lambdas
//
// ─────────────────────────────────────────────────────────────────────────────
// Per-frame render graph statistics.
struct RGStats {
    // ── Texture stats ──────────────────────────────────────────────────────────
    uint32_t transientCount              = 0;
    uint32_t importedCount               = 0;
    uint32_t physicalSlotCount           = 0;
    uint64_t transientBytesLogical       = 0;
    uint64_t transientBytesPhysical      = 0;
    uint64_t importedBytesLogical        = 0;

    // ── Buffer stats ───────────────────────────────────────────────────────────
    uint32_t transientBufferCount        = 0;
    uint32_t importedBufferCount         = 0;
    uint32_t physicalBufferSlotCount     = 0;
    uint64_t transientBufferBytesLogical  = 0;
    uint64_t transientBufferBytesPhysical = 0;

    struct Entry {
        std::string name;
        uint32_t    width      = 0;
        uint32_t    height     = 0;
        uint32_t    mipLevels  = 1;
        const char* formatStr  = nullptr;
        uint64_t    bytes      = 0;
        int         slotIndex  = -1;
    };
    std::vector<Entry> entries; // transient textures only

    struct BufferEntry {
        std::string name;
        uint64_t    bytes     = 0;
        int         slotIndex = -1;
    };
    std::vector<BufferEntry> bufferEntries; // transient buffers only
};

// Persistent physical GPU texture slot shared by aliased transient textures.
// Survives RenderGraph::Reset(); destroyed only via InvalidateSlots().
struct RGPhysicalSlot {
    RHI::RHITextureDesc   desc;
    RHI::RHITextureHandle handle;
    int                   freeAfterPass = -1;
};

// Persistent physical GPU buffer slot shared by aliased transient buffers.
// Survives RenderGraph::Reset(); destroyed only via InvalidateBufferSlots().
struct RGPhysicalBufferSlot {
    RGBufferDesc         desc;
    RHI::RHIBufferHandle handle;
    int                  freeAfterPass = -1;
};

class RenderGraph {
public:
    using SetupFn   = std::function<void(RGPassBuilder&)>;
    using ExecuteFn = std::function<void(RHI::IRHICommandList&, const RGResources&)>;

    // Discard all passes, textures, and buffers — call once per frame.
    // Slot pools (m_slots, m_bufferSlots) are NOT cleared; they persist for reuse.
    void Reset();

    // ── Texture API ───────────────────────────────────────────────────────────

    // Declare a transient texture (GPU allocation deferred to AllocateSlots).
    RGTextureHandle CreateTexture(const std::string& name, const RHI::RHITextureDesc& desc);

    // Import an external texture (e.g., swapchain image).
    // initialState: what state the texture is in when Execute starts.
    // finalState:   what state to leave it in after all passes (Undefined = no epilogue barrier).
    RGTextureHandle ImportTexture(const std::string& name,
                                  RHI::RHITextureHandle handle,
                                  RHI::RHIResourceState initialState,
                                  RHI::RHIResourceState finalState);

    // Returns the physical RHI handle for any texture after AllocateSlots.
    [[nodiscard]] RHI::RHITextureHandle GetResolvedHandle(RGTextureHandle h) const;

    // ── Buffer API ────────────────────────────────────────────────────────────

    // Declare a transient buffer (GPU allocation deferred to AllocateSlots).
    // If desc.clearOnCreate is true, RG zero-fills the buffer before its first
    // write pass each frame (equivalent to loadOp=CLEAR for render targets).
    RGBufferHandle CreateBuffer(const std::string& name, const RGBufferDesc& desc);

    // Import a persistent buffer (e.g., multi-frame accumulation SSBOs).
    // initialState: the buffer's access state entering this frame.
    RGBufferHandle ImportBuffer(const std::string& name,
                                RHI::RHIBufferHandle handle,
                                RHI::RHIBufferState  initialState);

    // Returns the physical RHI handle for any buffer after AllocateSlots.
    [[nodiscard]] RHI::RHIBufferHandle GetResolvedBufferHandle(RGBufferHandle h) const;

    // ── Pass Registration ─────────────────────────────────────────────────────

    // Register a pass.
    void AddPass(const std::string& name, SetupFn setup, ExecuteFn execute);

    // ── Compilation & Execution ───────────────────────────────────────────────

    // Topological sort + lifetime analysis + greedy slot assignment.
    void Compile();

    // Create or reuse GPU resources for physical slots assigned by Compile().
    // Must be called after Compile(). Execute() calls this automatically.
    void AllocateSlots(RHI::IRHIDevice& device);

    // Destroy all texture slot GPU resources and clear the texture slot pool.
    // Call on viewport resize before the next frame's Reset/CreateTexture sequence.
    void InvalidateSlots(RHI::IRHIDevice& device);

    // Destroy all buffer slot GPU resources and clear the buffer slot pool.
    // Call on shutdown or when persistent buffer parameters change.
    void InvalidateBufferSlots(RHI::IRHIDevice& device);

    // Emit barriers and invoke execute lambdas in sorted order.
    // Internally calls AllocateSlots(device).
    void Execute(RHI::IRHIDevice& device, RHI::IRHICommandList& cmd);

    [[nodiscard]] const RGStats&              GetLastFrameStats()  const { return m_lastStats; }
    [[nodiscard]] const RHI::RHIMemoryStats&  GetLastMemoryStats() const { return m_lastMemStats; }

private:
    struct TextureEntry {
        std::string            name;
        RHI::RHITextureDesc    desc       = {};
        RHI::RHITextureHandle  imported   = {};
        RHI::RHIResourceState  initState  = RHI::RHIResourceState::Undefined;
        RHI::RHIResourceState  finalState = RHI::RHIResourceState::Undefined;
        bool                   isImported = false;
        int firstWritePass = -1;
        int lastReadPass   = -1;
        int slotIndex      = -1;
    };

    struct BufferEntry {
        std::string           name;
        RGBufferDesc          desc      = {};
        RHI::RHIBufferHandle  imported  = {};
        RHI::RHIBufferState   initState = RHI::RHIBufferState::Undefined;
        bool                  isImported = false;
        int firstWritePass = -1;
        int lastReadPass   = -1;
        int slotIndex      = -1;
    };

    struct PassEntry {
        std::string                  name;
        std::vector<RGTextureHandle> reads;
        std::vector<RGWriteEntry>    writes;
        std::vector<RGBufferAccess>  bufferAccesses;
        ExecuteFn                    execute;
    };

    std::vector<TextureEntry>       m_textures;
    std::vector<BufferEntry>        m_buffers;
    std::vector<PassEntry>          m_passes;
    std::vector<uint32_t>           m_sortedPassIndices;
    std::vector<RGPhysicalSlot>     m_slots;              // persistent across Reset()
    std::vector<RGPhysicalBufferSlot> m_bufferSlots;      // persistent across Reset()
    RGStats                         m_lastStats;
    RHI::RHIMemoryStats             m_lastMemStats;
};

} // namespace StellarAlia
