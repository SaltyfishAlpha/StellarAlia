/**
 * @file VMA.cpp
 * @brief VMA (Vulkan Memory Allocator) implementation
 * 
 * This file contains the VMA implementation. VMA is a header-only library,
 * but requires VMA_IMPLEMENTATION to be defined in exactly one source file.
 * 
 * IMPORTANT: Include volk.h before vma/vk_mem_alloc.h to ensure VMA uses volk's
 * function loading mechanism when using volk as the Vulkan loader.
 */

// Include volk.h first to ensure VMA uses volk's function declarations
#include <volk.h>

// Define VMA_IMPLEMENTATION in exactly one source file to include the implementation
#define VMA_IMPLEMENTATION
// Disable newer Vulkan features that require extensions not available in pre-built loader
#define VMA_VULKAN_VERSION 1000000  // Vulkan 1.0 - limits VMA to Vulkan 1.0 features
#define VMA_KHR_MAINTENANCE4 0       // Disable VK_KHR_maintenance4 extension features

#include <vma/vk_mem_alloc.h>

