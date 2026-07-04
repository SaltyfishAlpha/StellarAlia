#include "platform/rhi/vulkan/VulkanUtils.hpp"

namespace StellarAlia::RHI {

VkFormat ToVkFormat(RHIFormat format) noexcept {
    switch (format) {
        case RHIFormat::RGBA8_UNORM:  return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8_SRGB:   return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::BGRA8_UNORM:  return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8_SRGB:   return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::RGBA16F:      return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RHIFormat::RGBA32F:      return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RHIFormat::RG16F:        return VK_FORMAT_R16G16_SFLOAT;
        case RHIFormat::RG32F:        return VK_FORMAT_R32G32_SFLOAT;
        case RHIFormat::R8_UNORM:     return VK_FORMAT_R8_UNORM;
        case RHIFormat::R32F:         return VK_FORMAT_R32_SFLOAT;
        case RHIFormat::D32F:         return VK_FORMAT_D32_SFLOAT;
        case RHIFormat::D24_S8:       return VK_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D16_UNORM:    return VK_FORMAT_D16_UNORM;
        case RHIFormat::BC1_UNORM:    return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case RHIFormat::BC3_UNORM:    return VK_FORMAT_BC3_UNORM_BLOCK;
        case RHIFormat::BC5_UNORM:    return VK_FORMAT_BC5_UNORM_BLOCK;
        case RHIFormat::BC7_UNORM:    return VK_FORMAT_BC7_UNORM_BLOCK;
        default:                      return VK_FORMAT_UNDEFINED;
    }
}

RHIFormat FromVkFormat(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:           return RHIFormat::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:            return RHIFormat::RGBA8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:           return RHIFormat::BGRA8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:            return RHIFormat::BGRA8_SRGB;
        case VK_FORMAT_R16G16B16A16_SFLOAT:      return RHIFormat::RGBA16F;
        case VK_FORMAT_R32G32B32A32_SFLOAT:      return RHIFormat::RGBA32F;
        case VK_FORMAT_R16G16_SFLOAT:            return RHIFormat::RG16F;
        case VK_FORMAT_D32_SFLOAT:               return RHIFormat::D32F;
        case VK_FORMAT_D24_UNORM_S8_UINT:        return RHIFormat::D24_S8;
        default:                                 return RHIFormat::Undefined;
    }
}

VkImageLayout ToVkImageLayout(RHIResourceState state) noexcept {
    switch (state) {
        case RHIResourceState::Undefined:       return VK_IMAGE_LAYOUT_UNDEFINED;
        case RHIResourceState::Common:          return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::RenderTarget:    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Issue #56: DEPTH_STENCIL_* variants — valid for depth-only formats too,
        // and required when a D24_S8 barrier covers both aspects.
        case RHIResourceState::DepthWrite:      return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RHIResourceState::DepthRead:       return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case RHIResourceState::ShaderRead:      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RHIResourceState::UnorderedAccess: return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::CopySrc:         return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RHIResourceState::CopyDst:         return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RHIResourceState::Present:         return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default:                                return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

void CmdTransitionImage(VkCommandBuffer    cmd,
                        VkImage            image,
                        VkImageLayout      from,
                        VkImageLayout      to,
                        VkImageAspectFlags aspect) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace StellarAlia::RHI
