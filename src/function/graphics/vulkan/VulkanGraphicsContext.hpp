#pragma once

/**
 * @file VulkanGraphicsContext.hpp
 * @brief Vulkan implementation of GraphicsContext
 * 
 * This implementation uses the window system for window and surface management (GLFW-only).
 */

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <volk.h>

#include "function/graphics/GraphicsContext.hpp"
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <string>

namespace StellarAlia::Function::Graphics {

    /**
     * @brief Vulkan graphics context implementation
     */
    class VulkanGraphicsContext : public GraphicsContext {
    public:
        VulkanGraphicsContext();
        ~VulkanGraphicsContext() override;

        bool Initialize(const GraphicsContextCreateInfo& createInfo) override;
        void Shutdown() override;
        void BeginFrame() override;
        void EndFrame() override;
        void Present() override;
        void WaitIdle() override;
        GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }
        bool IsInitialized() const override { return m_initialized; }
        uint32_t GetWidth() const override { return m_width; }
        uint32_t GetHeight() const override { return m_height; }
        void Resize(uint32_t width, uint32_t height) override;

        /**
         * @brief Get the VMA allocator
         * @return VMA allocator handle
         */
        VmaAllocator GetAllocator() const { return m_allocator; }

        /**
         * @brief Get the Vulkan device
         * @return VkDevice handle
         */
        VkDevice GetDevice() const { return m_device; }

        /**
         * @brief Get the graphics queue family index
         * @return Queue family index
         */
        uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }

        /**
         * @brief Get the graphics queue
         * @return VkQueue handle
         */
        VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }

    private:
        // Vulkan instance and device
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_presentQueue = VK_NULL_HANDLE;
        uint32_t m_graphicsQueueFamily = UINT32_MAX;
        uint32_t m_presentQueueFamily = UINT32_MAX;

        // Swapchain
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> m_swapchainImages;
        std::vector<VkImageView> m_swapchainImageViews;
        VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D m_swapchainExtent = {};

        // Command buffers
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_commandBuffers;

        // Synchronization
        std::vector<VkSemaphore> m_imageAvailableSemaphores;
        std::vector<VkSemaphore> m_renderFinishedSemaphores;
        std::vector<VkFence> m_inFlightFences;
        size_t m_currentFrame = 0;
        uint32_t m_currentImageIndex = 0;
        bool m_hasAcquiredImage = false;

        // Surface (platform-specific)
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;

        // Debug messenger (for validation layers)
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

        // Vulkan Memory Allocator
        VmaAllocator m_allocator = VK_NULL_HANDLE;

        // Window reference (for checking resize)
        WindowSystem* m_window = nullptr;

        // State
        bool m_initialized = false;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        bool m_enableValidation = false;

        // Helper functions
        bool CreateInstance(const GraphicsContextCreateInfo& createInfo);
        bool SetupDebugMessenger();
        bool CreateSurface(const GraphicsContextCreateInfo& createInfo);
        bool CreateSurfaceFromWindow(const GraphicsContextCreateInfo& createInfo);
        bool PickPhysicalDevice();
        bool CreateLogicalDevice();
        bool CreateSwapchain();
        void DestroySwapchain();
        bool CreateImageViews();
        bool CreateCommandPool();
        bool CreateCommandBuffers();
        bool CreateSyncObjects();
        bool CreateVMAAllocator();

        // Validation layer support
        bool CheckValidationLayerSupport();
        std::vector<const char*> GetRequiredExtensions(const GraphicsContextCreateInfo& createInfo);
        void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

        // Queue family helpers
        struct QueueFamilyIndices {
            uint32_t graphics = UINT32_MAX;
            uint32_t present = UINT32_MAX;
            bool IsComplete() const { return graphics != UINT32_MAX && present != UINT32_MAX; }
        };
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    };

} // namespace StellarAlia::Function::Graphics

