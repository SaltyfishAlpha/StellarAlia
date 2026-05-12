#pragma once

#include <span>

#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia::RHI {

// ─────────────────────────────────────────────────────────────────────────────
// IRHICommandList
//
// Hardware-agnostic command recording interface.
// One instance per frame (or per thread in a multi-threaded setup).
// The concrete implementation (VulkanCommandList, D3D12CommandList...) wraps
// the backend command buffer / command allocator.
//
// Lifetime contract:
//   Obtained from IRHIDevice::BeginFrame().
//   Valid until IRHIDevice::EndFrame() is called.
//   Do NOT cache across frames.
// ─────────────────────────────────────────────────────────────────────────────
class IRHICommandList {
public:
    virtual ~IRHICommandList() = default;

    // ── Render Pass ──────────────────────────────────────────────────────────
    // Uses dynamic rendering (VK_KHR_dynamic_rendering / D3D12 render targets).
    // No VkRenderPass / ID3D12RenderPass objects are exposed.

    virtual void BeginRenderPass(const RHIRenderPassDesc& desc) = 0;
    virtual void EndRenderPass()                                 = 0;

    // ── Pipeline State ────────────────────────────────────────────────────────
    virtual void SetViewport(const RHIViewport& viewport)                  = 0;
    virtual void SetScissor(const RHIScissor& scissor)                     = 0;
    virtual void SetPipeline(RHIPipelineHandle pipeline)                   = 0;
    virtual void SetDescriptorSet(uint32_t set, RHIDescSetHandle ds,
                                  std::span<const uint32_t> dynamicOffsets = {}) = 0;
    virtual void SetPushConstants(const void*   data,
                                  uint32_t      size,
                                  RHIShaderStage stages)                   = 0;

    // ── Vertex / Index Buffers ────────────────────────────────────────────────
    virtual void SetVertexBuffer(uint32_t       slot,
                                 RHIBufferHandle buffer,
                                 uint64_t        offset = 0)               = 0;
    virtual void SetIndexBuffer(RHIBufferHandle buffer,
                                uint64_t        offset  = 0,
                                bool            use16bit = false)          = 0;

    // ── Draw Calls ────────────────────────────────────────────────────────────
    virtual void Draw(uint32_t vertexCount,
                      uint32_t instanceCount = 1,
                      uint32_t firstVertex   = 0,
                      uint32_t firstInstance = 0)                          = 0;

    virtual void DrawIndexed(uint32_t indexCount,
                             uint32_t instanceCount = 1,
                             uint32_t firstIndex    = 0,
                             int32_t  vertexOffset  = 0,
                             uint32_t firstInstance = 0)                   = 0;

    // ── Compute ───────────────────────────────────────────────────────────────
    // Bind a compute pipeline (created via IRHIDevice::CreateComputePipeline).
    // After this call, SetDescriptorSet and Dispatch operate on the compute
    // bind point. SetPipeline (graphics) resets this back to graphics.
    virtual void SetComputePipeline(RHIPipelineHandle pipeline) = 0;

    virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    // ── Resource Barriers ─────────────────────────────────────────────────────
    // Called by RenderGraph::Execute() — not by Feature/Application code.
    // The backend translates RHIResourceState pairs into the correct API barriers.

    virtual void TransitionTexture(RHITextureHandle  texture,
                                   RHIResourceState  from,
                                   RHIResourceState  to)                   = 0;

    // Fill a buffer range with a 4-byte repeating pattern.
    // offset and size must be multiples of 4; VK_WHOLE_SIZE fills to the end.
    // Called internally by RenderGraph for clearOnCreate buffers.
    virtual void FillBuffer(RHIBufferHandle buffer,
                            uint64_t        offset,
                            uint64_t        size,
                            uint32_t        value)                         = 0;

    // Emit a pipeline barrier for a buffer state transition.
    // Called internally by RenderGraph — not by Feature/Application code.
    virtual void BufferBarrier(RHIBufferHandle buffer,
                               RHIBufferState  from,
                               RHIBufferState  to)                         = 0;

    // ── Mip Generation ───────────────────────────────────────────────────────
    // Generate all mip levels from mip 0 using linear filtering (blit chain).
    // Mip 0 must be in ShaderRead state on entry; all mips will be in ShaderRead
    // on return. The texture must have been created with CopySrc usage.
    virtual void GenerateMipmaps(RHITextureHandle texture) = 0;

    // ── Copy ──────────────────────────────────────────────────────────────────
    virtual void CopyBuffer(RHIBufferHandle src,
                            RHIBufferHandle dst,
                            uint64_t        srcOffset,
                            uint64_t        dstOffset,
                            uint64_t        size)                          = 0;

    virtual void CopyBufferToTexture(RHIBufferHandle  src,
                                     RHITextureHandle dst,
                                     uint32_t         mipLevel = 0,
                                     uint32_t         layer    = 0)        = 0;
};

} // namespace StellarAlia::RHI
