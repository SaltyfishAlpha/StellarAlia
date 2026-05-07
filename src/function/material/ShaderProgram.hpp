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
//   - A pipeline cache: AttachmentKey → RHIPipelineHandle (lazy creation)
//
// The pipeline layout combines set=0 (frame uniforms, supplied externally) and
// set=1 (material, owned here).
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
    [[nodiscard]] const RHI::ShaderReflection& GetMergedReflection() const { return m_merged; }
    [[nodiscard]] bool IsLoaded() const { return m_vertShader.IsValid(); }

private:
    RHI::RHIShaderHandle     m_vertShader;
    RHI::RHIShaderHandle     m_fragShader;
    RHI::RHIDescLayoutHandle m_frameLayout;
    RHI::RHIDescLayoutHandle m_materialLayout;
    RHI::ShaderReflection    m_merged;

    std::unordered_map<AttachmentKey, RHI::RHIPipelineHandle, AttachmentKeyHash> m_pipelineCache;
};

} // namespace StellarAlia
