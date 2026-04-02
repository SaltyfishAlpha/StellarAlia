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
// RGResources
//
// Passed to each execute lambda. Resolves an RGTextureHandle into the backing
// RHITextureHandle so the lambda can call RHI APIs (BeginRenderPass, etc.).
// ─────────────────────────────────────────────────────────────────────────────
class RGResources {
public:
    [[nodiscard]] RHI::RHITextureHandle Get(RGTextureHandle handle) const;

private:
    friend class RenderGraph;
    std::vector<RHI::RHITextureHandle> m_resolved;
};

// ─────────────────────────────────────────────────────────────────────────────
// RGPassBuilder
//
// Passed to the setup lambda of AddPass(). Declares which textures a pass
// reads or writes so Compile() can build the dependency graph.
// ─────────────────────────────────────────────────────────────────────────────
class RGPassBuilder {
public:
    // Read: expects the texture in ShaderRead state before the pass.
    void Read(RGTextureHandle tex);

    // Write: expects the texture in RenderTarget (color attachment) state.
    void Write(RGTextureHandle tex);

    // WriteDepth: expects the texture in DepthWrite state.
    void WriteDepth(RGTextureHandle tex);

    // WriteUAV: expects the texture in UnorderedAccess (storage image) state.
    // Use this for compute pass outputs.
    void WriteUAV(RGTextureHandle tex);

    // ReadUAV: transitions to ShaderRead before the pass.
    // Use this when a compute-written texture is consumed by a later pass as SRV.
    // (Equivalent to Read — listed separately for call-site clarity.)
    void ReadUAV(RGTextureHandle tex);

private:
    friend class RenderGraph;
    std::vector<RGTextureHandle> m_reads;
    std::vector<RGWriteEntry>    m_writes;
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
class RenderGraph {
public:
    using SetupFn   = std::function<void(RGPassBuilder&)>;
    using ExecuteFn = std::function<void(RHI::IRHICommandList&, const RGResources&)>;

    // Discard all passes and textures — call once per frame before rebuilding.
    void Reset();

    // Declare a transient texture (GPU allocation deferred to Stage 3).
    RGTextureHandle CreateTexture(const std::string& name, const RHI::RHITextureDesc& desc);

    // Import an external texture (e.g., swapchain image).
    // initialState: what state the texture is in when Execute starts.
    // finalState:   what state to leave it in after all passes (Undefined = no epilogue barrier).
    RGTextureHandle ImportTexture(const std::string& name,
                                  RHI::RHITextureHandle handle,
                                  RHI::RHIResourceState initialState,
                                  RHI::RHIResourceState finalState);

    // Register a pass.
    void AddPass(const std::string& name, SetupFn setup, ExecuteFn execute);

    // Topological sort (Kahn's algorithm) on read/write dependencies.
    void Compile();

    // Emit barriers and invoke execute lambdas in sorted order.
    void Execute(RHI::IRHIDevice& device, RHI::IRHICommandList& cmd);

private:
    struct TextureEntry {
        std::string            name;
        RHI::RHITextureDesc    desc       = {};
        RHI::RHITextureHandle  imported   = {};
        RHI::RHIResourceState  initState  = RHI::RHIResourceState::Undefined;
        RHI::RHIResourceState  finalState = RHI::RHIResourceState::Undefined;
        bool                   isImported = false;
    };

    struct PassEntry {
        std::string                  name;
        std::vector<RGTextureHandle> reads;
        std::vector<RGWriteEntry>    writes;
        ExecuteFn                    execute;
    };

    std::vector<TextureEntry> m_textures;
    std::vector<PassEntry>    m_passes;
    std::vector<uint32_t>     m_sortedPassIndices;
};

} // namespace StellarAlia
