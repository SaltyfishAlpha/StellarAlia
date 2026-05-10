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
        RHI::RHIDescLayoutHandle  frameLayout;  // set=0 from FrameUniformsBuffer
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
    // attachment formats and render state.
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
    [[nodiscard]] RHI::RHIDescLayoutHandle  GetSet2Layout()         const { return m_set2Layout; }
    [[nodiscard]] const RHI::ShaderReflection& GetMergedReflection() const { return m_merged; }
    [[nodiscard]] bool IsLoaded() const { return m_vertShader.IsValid(); }

private:
    RHI::RHIShaderHandle     m_vertShader;
    RHI::RHIShaderHandle     m_fragShader;
    RHI::RHIDescLayoutHandle m_frameLayout;
    RHI::RHIDescLayoutHandle m_materialLayout;
    RHI::RHIDescLayoutHandle m_set2Layout;
    RHI::ShaderReflection    m_merged;

    std::unordered_map<AttachmentKey, RHI::RHIPipelineHandle, AttachmentKeyHash> m_pipelineCache;
};

} // namespace StellarAlia
