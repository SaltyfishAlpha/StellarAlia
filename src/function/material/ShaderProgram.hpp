#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include "function/material/AttachmentKey.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// ShaderProgram
//
// Wraps a compiled vert+frag shader pair with their merged reflection data.
// Owns:
//   - RHIShaderHandles (vert + frag)
//   - set=1 DescriptorSetLayout  (material-specific bindings)
//   - set=2 DescriptorSetLayout  (optional; created only when the shader uses set=2 bindings,
//                                  e.g. GPU skinning bone matrices + skin vertex SSBOs)
//   - A pipeline cache: AttachmentKey → RHIPipelineHandle (lazy creation)
//
// The pipeline layout combines set=0 (frame uniforms, supplied externally),
// set=1 (material, owned here), and optionally set=2 (GPU skinning, owned here).
// ─────────────────────────────────────────────────────────────────────────────
class ShaderProgram {
public:
    struct Desc {
        std::span<const uint8_t>  vertSpv;
        RHI::ShaderReflection     vertRefl;
        std::span<const uint8_t>  fragSpv;
        RHI::ShaderReflection     fragRefl;
        RHI::RHIDescLayoutHandle  frameLayout;     // set=0 from FrameUniformsBuffer
        // Issue #72: BindlessTextureHeap layout (set=3). Wired into the pipeline
        // layout only when the merged reflection references set=3 bindings.
        RHI::RHIDescLayoutHandle  bindlessLayout;
    };

    // Returns false if shader module creation fails.
    bool Load(RHI::IRHIDevice* device, const Desc& desc);

    // Destroys all owned GPU resources.
    void Unload(RHI::IRHIDevice* device);

    // Reload only the fragment shader (e.g. deferred_lighting dispatch changed).
    // Destroys all cached pipelines — they are recreated lazily on next use.
    // The vert shader, layouts, and merged reflection are unchanged.
    // Device must be idle before calling. Returns false on shader creation failure.
    bool ReloadFragShader(RHI::IRHIDevice* device,
                          std::span<const uint8_t>     fragSpv,
                          const RHI::ShaderReflection& fragRefl);

    // Returns (creating if necessary) a pipeline configured for the given
    // attachment formats and render state. Issue #56: the cache key includes
    // the FULL render state — the same shader can serve multiple permutations
    // (alpha modes, double-sided, EQUAL depth, stencil).
    RHI::RHIPipelineHandle GetOrCreatePipeline(
        RHI::IRHIDevice*           device,
        const AttachmentKey&       key,
        const PipelineRenderState& state);

    // Legacy convenience overload — packs the loose flags into a
    // PipelineRenderState. Existing callers compile unchanged.
    // noVertexInput: set true for fullscreen-triangle passes (skybox, post-fx)
    //   that generate vertex positions in the vertex shader without a VBO.
    RHI::RHIPipelineHandle GetOrCreatePipeline(
        RHI::IRHIDevice*       device,
        const AttachmentKey&   key,
        RHI::RHICullMode       cullMode      = RHI::RHICullMode::Back,
        RHI::RHIBlendMode      blendMode     = RHI::RHIBlendMode::Opaque,
        RHI::RHITopology       topology      = RHI::RHITopology::TriangleList,
        bool                   depthTest     = true,
        bool                   depthWrite    = true,
        bool                   noVertexInput = false);

    [[nodiscard]] RHI::RHIDescLayoutHandle  GetMaterialLayout()     const { return m_materialLayout; }
    [[nodiscard]] RHI::RHIDescLayoutHandle  GetSet3Layout()         const { return m_set3Layout; }
    [[nodiscard]] RHI::RHIDescLayoutHandle  GetBindlessLayout()     const { return m_bindlessLayout; }
    [[nodiscard]] bool                       UsesBindless()         const { return m_bindlessLayout.IsValid(); }
    [[nodiscard]] const RHI::ShaderReflection& GetMergedReflection() const { return m_merged; }
    [[nodiscard]] bool IsLoaded() const { return m_vertShader.IsValid(); }

private:
    // Issue #72 Step 6.5 set assignment:
    //   slot 0 = m_bindlessLayout  (BindlessTextureHeap, shared across all shaders)
    //   slot 1 = m_frameLayout     (FrameUniforms)
    //   slot 2 = m_materialLayout  (per-shader, reflected from set=2 bindings)
    //   slot 3 = m_set3Layout      (per-shader, reflected from set=3 — typically skin)
    RHI::RHIShaderHandle     m_vertShader;
    RHI::RHIShaderHandle     m_fragShader;
    RHI::RHIDescLayoutHandle m_bindlessLayout;   // slot 0; valid iff shader samples set=0 bindless heap
    RHI::RHIDescLayoutHandle m_frameLayout;       // slot 1
    RHI::RHIDescLayoutHandle m_materialLayout;    // slot 2
    RHI::RHIDescLayoutHandle m_set3Layout;        // slot 3
    RHI::ShaderReflection    m_merged;

    // Issue #56: keyed by attachments + full render state (was AttachmentKey only,
    // which returned the wrong pipeline when one shader was requested with
    // different cull/blend/depth/stencil states).
    std::unordered_map<PipelineStateKey, RHI::RHIPipelineHandle, PipelineStateKeyHash> m_pipelineCache;
};

} // namespace StellarAlia
