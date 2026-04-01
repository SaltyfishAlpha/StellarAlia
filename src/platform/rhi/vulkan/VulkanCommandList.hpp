#pragma once

#include <volk.h>
#include "platform/rhi/IRHICommandList.hpp"

namespace StellarAlia::RHI {

class VulkanDevice;

// ─────────────────────────────────────────────────────────────────────────────
// VulkanCommandList
//
// Wraps a VkCommandBuffer. Obtained from VulkanDevice::BeginFrame().
// Holds a non-owning back-pointer to VulkanDevice for handle → VkImage lookup.
// ─────────────────────────────────────────────────────────────────────────────
class VulkanCommandList final : public IRHICommandList {
public:
    void Bind(VkCommandBuffer cmd, VulkanDevice* device) {
        m_cmd    = cmd;
        m_device = device;
    }
    VkCommandBuffer GetVkCommandBuffer() const { return m_cmd; }

    // ── IRHICommandList ───────────────────────────────────────────────────────
    void BeginRenderPass(const RHIRenderPassDesc& desc) override;
    void EndRenderPass() override;

    void SetViewport(const RHIViewport& vp) override;
    void SetScissor(const RHIScissor& sc)   override;

    void SetPipeline(RHIPipelineHandle pipeline)             override;
    void SetDescriptorSet(uint32_t set, RHIDescSetHandle ds) override;
    void SetPushConstants(const void* data, uint32_t size,
                          RHIShaderStage stages)             override;

    void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer,
                         uint64_t offset = 0)                override;
    void SetIndexBuffer(RHIBufferHandle buffer,
                        uint64_t offset  = 0,
                        bool use16bit    = false)             override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex,  uint32_t firstInstance)  override;
    void DrawIndexed(uint32_t indexCount,  uint32_t instanceCount,
                     uint32_t firstIndex,  int32_t  vertexOffset,
                     uint32_t firstInstance)                   override;

    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

    void TransitionTexture(RHITextureHandle tex,
                           RHIResourceState from,
                           RHIResourceState to) override;

    void CopyBuffer(RHIBufferHandle src, RHIBufferHandle dst,
                    uint64_t srcOff, uint64_t dstOff,
                    uint64_t size) override;
    void CopyBufferToTexture(RHIBufferHandle src, RHITextureHandle dst,
                             uint32_t mipLevel, uint32_t layer) override;

private:
    VkCommandBuffer  m_cmd             = VK_NULL_HANDLE;
    VulkanDevice*    m_device          = nullptr;
    RHIPipelineHandle m_boundPipeline  = {};  // tracks bound pipeline for layout lookup
};

} // namespace StellarAlia::RHI
