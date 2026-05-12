#define NOMINMAX
#include "platform/rhi/vulkan/VulkanCommandList.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/vulkan/VulkanUtils.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>

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
    m_boundPipeline         = pipeline;
    m_boundPipelineIsCompute = false;
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
}

void VulkanCommandList::SetComputePipeline(RHIPipelineHandle pipeline) {
    VkPipeline       vkPipeline = m_device->GetVkPipeline(pipeline);
    VkPipelineLayout layout     = m_device->GetVkPipelineLayout(pipeline);
    if (vkPipeline == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) {
        SA_LOG_WARN("VulkanCommandList::SetComputePipeline — invalid pipeline handle");
        return;
    }
    m_boundPipeline          = pipeline;
    m_boundPipelineIsCompute = true;
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline);
}

void VulkanCommandList::SetDescriptorSet(uint32_t set, RHIDescSetHandle ds,
                                          std::span<const uint32_t> dynamicOffsets) {
    VkPipelineLayout layout = m_device->GetVkPipelineLayout(m_boundPipeline);
    VkDescriptorSet  vkDs   = m_device->GetVkDescriptorSet(ds);
    if (layout == VK_NULL_HANDLE || vkDs == VK_NULL_HANDLE) return;
    const VkPipelineBindPoint bp = m_boundPipelineIsCompute
                                       ? VK_PIPELINE_BIND_POINT_COMPUTE
                                       : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindDescriptorSets(m_cmd, bp, layout, set, 1, &vkDs,
                            static_cast<uint32_t>(dynamicOffsets.size()),
                            dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());
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

    // Pick the correct aspect based on the texture's format.
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (const RHITextureDesc* desc = m_device->GetTextureDesc(tex)) {
        switch (desc->format) {
            case RHIFormat::D32F:
            case RHIFormat::D16_UNORM:
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                break;
            case RHIFormat::D24_S8:
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                break;
            default:
                break;
        }
    }

    CmdTransitionImage(m_cmd, image,
                       ToVkImageLayout(from),
                       ToVkImageLayout(to),
                       aspect);
}

void VulkanCommandList::GenerateMipmaps(RHITextureHandle texture) {
    const RHITextureDesc* desc = m_device->GetTextureDesc(texture);
    if (!desc || desc->mipLevels <= 1) return;

    VkImage        img       = m_device->GetVkImage(texture);
    const uint32_t mipLevels = desc->mipLevels;
    const uint32_t layers    = desc->arrayLayers; // 6 for cubemaps (normalized in CreateTexture)

    auto barrier2 = [&](VkImageLayout oldL, VkImageLayout newL,
                        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                        VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                        uint32_t baseMip, uint32_t levelCount) {
        VkImageMemoryBarrier2 b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask        = srcStage;
        b.srcAccessMask       = srcAccess;
        b.dstStageMask        = dstStage;
        b.dstAccessMask       = dstAccess;
        b.oldLayout           = oldL;
        b.newLayout           = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = img;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, baseMip, levelCount, 0, layers };
        VkDependencyInfo di{};
        di.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers    = &b;
        vkCmdPipelineBarrier2(m_cmd, &di);
    };

    // Mip 0: SHADER_READ_ONLY → TRANSFER_SRC
    barrier2(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,     VK_ACCESS_2_SHADER_READ_BIT,
             VK_PIPELINE_STAGE_2_BLIT_BIT,             VK_ACCESS_2_TRANSFER_READ_BIT,
             0, 1);

    for (uint32_t m = 1; m < mipLevels; ++m) {
        const int32_t srcW = (int32_t)std::max(1u, desc->width  >> (m - 1));
        const int32_t srcH = (int32_t)std::max(1u, desc->height >> (m - 1));
        const int32_t dstW = (int32_t)std::max(1u, desc->width  >> m);
        const int32_t dstH = (int32_t)std::max(1u, desc->height >> m);

        // Mip m: SHADER_READ_ONLY → TRANSFER_DST
        barrier2(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_NONE,                  VK_ACCESS_2_NONE,
                 VK_PIPELINE_STAGE_2_BLIT_BIT,              VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 m, 1);

        VkImageBlit2 blit{};
        blit.sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, layers };
        blit.srcOffsets[0]  = { 0, 0, 0 };
        blit.srcOffsets[1]  = { srcW, srcH, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m,     0, layers };
        blit.dstOffsets[0]  = { 0, 0, 0 };
        blit.dstOffsets[1]  = { dstW, dstH, 1 };

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage       = img;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage       = img;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount    = 1;
        blitInfo.pRegions       = &blit;
        blitInfo.filter         = VK_FILTER_LINEAR;
        vkCmdBlitImage2(m_cmd, &blitInfo);

        // Mip m: TRANSFER_DST → TRANSFER_SRC (for next iteration)
        barrier2(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_BLIT_BIT,         VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_BLIT_BIT,         VK_ACCESS_2_TRANSFER_READ_BIT,
                 m, 1);
    }

    // All mips in TRANSFER_SRC → SHADER_READ_ONLY
    CmdTransitionImage(m_cmd, img,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

static std::pair<VkPipelineStageFlags2, VkAccessFlags2>
ToVkBufferStageAccess(RHIBufferState state) {
    switch (state) {
        case RHIBufferState::StorageRead:
            return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
        case RHIBufferState::StorageWrite:
            return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
        case RHIBufferState::IndirectRead:
            return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT};
        case RHIBufferState::CopySrc:
            return {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
        case RHIBufferState::CopyDst:
            return {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
        case RHIBufferState::VertexRead:
            return {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                    VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
        default:
            return {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE};
    }
}

void VulkanCommandList::FillBuffer(RHIBufferHandle buffer,
                                    uint64_t offset, uint64_t size,
                                    uint32_t value) {
    VkBuffer vkBuf = m_device->GetVkBuffer(buffer);
    if (vkBuf == VK_NULL_HANDLE) return;
    vkCmdFillBuffer(m_cmd, vkBuf, offset, size, value);
}

void VulkanCommandList::BufferBarrier(RHIBufferHandle buffer,
                                       RHIBufferState from,
                                       RHIBufferState to) {
    VkBuffer vkBuf = m_device->GetVkBuffer(buffer);
    if (vkBuf == VK_NULL_HANDLE) return;

    const auto [srcStage, srcAccess] = ToVkBufferStageAccess(from);
    const auto [dstStage, dstAccess] = ToVkBufferStageAccess(to);

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask        = srcStage;
    barrier.srcAccessMask       = srcAccess;
    barrier.dstStageMask        = dstStage;
    barrier.dstAccessMask       = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer              = vkBuf;
    barrier.offset              = 0;
    barrier.size                = VK_WHOLE_SIZE;

    VkDependencyInfo di{};
    di.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.bufferMemoryBarrierCount = 1;
    di.pBufferMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(m_cmd, &di);
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
