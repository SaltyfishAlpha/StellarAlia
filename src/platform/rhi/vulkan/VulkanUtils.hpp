#pragma once

#include <volk.h>
#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia::RHI {

VkFormat      ToVkFormat(RHIFormat format) noexcept;
RHIFormat     FromVkFormat(VkFormat format) noexcept;
VkImageLayout ToVkImageLayout(RHIResourceState state) noexcept;

// Full-barrier image layout transition via synchronization2 (Vulkan 1.3 core).
// Uses ALL_COMMANDS + MEMORY_READ|WRITE on both sides — safe for correctness,
// optimise stage masks later when needed.
void CmdTransitionImage(VkCommandBuffer  cmd,
                        VkImage          image,
                        VkImageLayout    from,
                        VkImageLayout    to,
                        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

} // namespace StellarAlia::RHI
