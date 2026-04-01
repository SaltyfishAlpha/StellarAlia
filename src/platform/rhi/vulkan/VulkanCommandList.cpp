#include "platform/rhi/vulkan/VulkanCommandList.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/vulkan/VulkanUtils.hpp"
#include "core/logs/Log.hpp"

namespace StellarAlia::RHI {

void VulkanCommandList::BeginRenderPass(const RHIRenderPassDesc& desc) {
    // Build VkRenderingAttachmentInfo for each color attachment
    VkRenderingAttachmentInfo colorAttachments[8]{};
    for (uint32_t i = 0; i < desc.colorAttachmentCount; i++) {
        const auto& a = desc.colorAttachments[i];
        colorAttachments[i].sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView   = m_device->GetVkImageView(a.texture);
        colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[i].loadOp      = a.clearOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                        : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachments[i].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[i].clearValue.color = {
            a.clearColor[0], a.clearColor[1], a.clearColor[2], a.clearColor[3]};
    }

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea           = {{0, 0}, {desc.width, desc.height}};
    renderingInfo.layerCount           = 1;
    renderingInfo.colorAttachmentCount = desc.colorAttachmentCount;
    renderingInfo.pColorAttachments    = colorAttachments;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (desc.hasDepth) {
        depthAttachment.imageView   = m_device->GetVkImageView(desc.depthAttachment.texture);
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp      = desc.depthAttachment.clearOnLoad
                                          ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                          : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {desc.depthAttachment.clearDepth,
                                                   desc.depthAttachment.clearStencil};
        renderingInfo.pDepthAttachment = &depthAttachment;
    }

    vkCmdBeginRendering(m_cmd, &renderingInfo);
}

void VulkanCommandList::EndRenderPass() {
    vkCmdEndRendering(m_cmd);
}

void VulkanCommandList::SetViewport(const RHIViewport& vp) {
    VkViewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
    vkCmdSetViewport(m_cmd, 0, 1, &v);
}

void VulkanCommandList::SetScissor(const RHIScissor& sc) {
    VkRect2D r{{sc.offsetX, sc.offsetY}, {sc.width, sc.height}};
    vkCmdSetScissor(m_cmd, 0, 1, &r);
}

void VulkanCommandList::SetPipeline(RHIPipelineHandle pipeline) {
    VkPipeline       vkPipeline = m_device->GetVkPipeline(pipeline);
    VkPipelineLayout layout     = m_device->GetVkPipelineLayout(pipeline);
    if (vkPipeline == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) {
        SA_LOG_WARN("VulkanCommandList::SetPipeline — invalid pipeline handle");
        return;
    }
    m_boundPipeline = pipeline;
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
}

void VulkanCommandList::SetDescriptorSet(uint32_t set, RHIDescSetHandle ds) {
    VkPipelineLayout layout = m_device->GetVkPipelineLayout(m_boundPipeline);
    VkDescriptorSet  vkDs   = m_device->GetVkDescriptorSet(ds);
    if (layout == VK_NULL_HANDLE || vkDs == VK_NULL_HANDLE) return;
    vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout, set, 1, &vkDs, 0, nullptr);
}

void VulkanCommandList::SetPushConstants(const void* data, uint32_t size,
                                          RHIShaderStage stages) {
    VkPipelineLayout layout     = m_device->GetVkPipelineLayout(m_boundPipeline);
    VkShaderStageFlags vkStages = 0;
    if (HasStage(stages, RHIShaderStage::Vertex))   vkStages |= VK_SHADER_STAGE_VERTEX_BIT;
    if (HasStage(stages, RHIShaderStage::Fragment))  vkStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (HasStage(stages, RHIShaderStage::Compute))   vkStages |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (layout == VK_NULL_HANDLE || vkStages == 0 || size == 0) return;
    vkCmdPushConstants(m_cmd, layout, vkStages, 0, size, data);
}

void VulkanCommandList::SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer,
                                         uint64_t offset) {
    VkBuffer vkBuf = m_device->GetVkBuffer(buffer);
    if (vkBuf == VK_NULL_HANDLE) return;
    vkCmdBindVertexBuffers(m_cmd, slot, 1, &vkBuf, &offset);
}

void VulkanCommandList::SetIndexBuffer(RHIBufferHandle buffer, uint64_t offset,
                                        bool use16bit) {
    VkBuffer vkBuf = m_device->GetVkBuffer(buffer);
    if (vkBuf == VK_NULL_HANDLE) return;
    vkCmdBindIndexBuffer(m_cmd, vkBuf, offset,
                         use16bit ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                              uint32_t firstVertex,  uint32_t firstInstance) {
    vkCmdDraw(m_cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::DrawIndexed(uint32_t indexCount,  uint32_t instanceCount,
                                     uint32_t firstIndex,  int32_t  vertexOffset,
                                     uint32_t firstInstance) {
    vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDispatch(m_cmd, x, y, z);
}

void VulkanCommandList::TransitionTexture(RHITextureHandle tex,
                                           RHIResourceState from,
                                           RHIResourceState to) {
    VkImage image = m_device->GetVkImage(tex);
    if (image == VK_NULL_HANDLE) {
        SA_LOG_ERROR("TransitionTexture: invalid handle {}", tex.index);
        return;
    }
    CmdTransitionImage(m_cmd, image,
                       ToVkImageLayout(from),
                       ToVkImageLayout(to));
}

void VulkanCommandList::CopyBuffer(RHIBufferHandle src, RHIBufferHandle dst,
                                    uint64_t srcOff, uint64_t dstOff,
                                    uint64_t size) {
    VkBuffer vkSrc = m_device->GetVkBuffer(src);
    VkBuffer vkDst = m_device->GetVkBuffer(dst);
    if (vkSrc == VK_NULL_HANDLE || vkDst == VK_NULL_HANDLE) return;
    VkBufferCopy region{srcOff, dstOff, size};
    vkCmdCopyBuffer(m_cmd, vkSrc, vkDst, 1, &region);
}

void VulkanCommandList::CopyBufferToTexture(RHIBufferHandle src,
                                             RHITextureHandle dst,
                                             uint32_t mipLevel, uint32_t layer) {
    VkBuffer    vkSrc = m_device->GetVkBuffer(src);
    VkImage     vkDst = m_device->GetVkImage(dst);
    if (vkSrc == VK_NULL_HANDLE || vkDst == VK_NULL_HANDLE) return;

    const RHITextureDesc* texDesc = m_device->GetTextureDesc(dst);
    if (!texDesc) return;

    // Mip dimensions (halved per level, minimum 1)
    const uint32_t mipWidth  = (texDesc->width  >> mipLevel) > 0 ? (texDesc->width  >> mipLevel) : 1;
    const uint32_t mipHeight = (texDesc->height >> mipLevel) > 0 ? (texDesc->height >> mipLevel) : 1;

    // Assume the image is already transitioned to TRANSFER_DST_OPTIMAL by the caller.
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = mipLevel;
    region.imageSubresource.baseArrayLayer = layer;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent                     = {mipWidth, mipHeight, 1};
    vkCmdCopyBufferToImage(m_cmd, vkSrc, vkDst,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

} // namespace StellarAlia::RHI
