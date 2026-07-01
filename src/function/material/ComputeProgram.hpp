#pragma once

#include <span>
#include <unordered_map>

#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// ComputeProgram
//
// Wraps a single compiled compute shader with its reflection data.
// Analogous to ShaderProgram but for compute:
//   - No vert/frag merge; one shader stage only.
//   - No AttachmentKey cache; compute pipelines are format-independent and
//     created once on the first GetPipeline() call.
//   - Owns DescriptorSetLayouts for every set index found in the reflection.
//   - Optionally accepts an external layout at set=1 (per-frame globals)
//     to mirror the engine-wide set=1 frame convention (frame_uniforms.glsl).
//
// Typical usage:
//
//   ComputeProgram prog;
//   prog.Load(device, {compSpv, compRefl});
//
//   RHIDescSetHandle ds = device->AllocateDescriptorSet(prog.GetLayout(0));
//   device->WriteDescriptorStorageImage(ds, 0, outputTex);
//
//   // In render loop:
//   cmd.SetComputePipeline(prog.GetPipeline(device));
//   cmd.SetDescriptorSet(0, ds);
//   cmd.SetPushConstants(...);
//   cmd.Dispatch(x, y, z);
// ─────────────────────────────────────────────────────────────────────────────
class ComputeProgram {
public:
    struct Desc {
        std::span<const uint8_t>  spv;
        RHI::ShaderReflection     refl;

        // Optional: caller-managed layout for set=1 (per-frame scene data
        // bound before the dispatch).  When valid, this layout occupies set=1
        // in the pipeline layout instead of the auto-derived one — matching the
        // engine-wide per-frame set convention (set=0 bindless, set=1 frame).
        RHI::RHIDescLayoutHandle  frameLayout = {};
    };

    // Compiles the shader module and creates descriptor set layouts from the
    // reflection.  Returns false on GPU resource creation failure.
    bool Load(RHI::IRHIDevice* device, const Desc& desc);

    // Destroys all owned GPU resources.
    void Unload(RHI::IRHIDevice* device);

    // Returns the compute pipeline handle, creating it on the first call.
    // Thread-safety: call only from the render thread.
    [[nodiscard]] RHI::RHIPipelineHandle GetPipeline(RHI::IRHIDevice* device);

    // Returns the auto-derived descriptor set layout for a given set index.
    // Returns an invalid handle if the reflection has no bindings for that set.
    [[nodiscard]] RHI::RHIDescLayoutHandle GetLayout(uint32_t setIndex) const;

    [[nodiscard]] const RHI::ShaderReflection& GetReflection() const { return m_refl; }
    [[nodiscard]] bool IsLoaded() const { return m_shader.IsValid(); }

private:
    RHI::RHIShaderHandle     m_shader;
    RHI::ShaderReflection    m_refl;
    RHI::RHIPipelineHandle   m_pipeline;
    RHI::RHIDescLayoutHandle m_frameLayout;

    // set index → auto-derived RHIDescLayoutHandle
    std::unordered_map<uint32_t, RHI::RHIDescLayoutHandle> m_layouts;
};

} // namespace StellarAlia
