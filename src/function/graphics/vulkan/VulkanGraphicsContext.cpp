#include "function/graphics/vulkan/VulkanGraphicsContext.hpp"
#include "function/graphics/window/Window.hpp"
#include "core/logs/Log.hpp"
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cstring>

namespace StellarAlia::Function::Graphics {

    // Validation layers
    #ifdef _DEBUG
        const std::vector<const char*> VALIDATION_LAYERS = {
            "VK_LAYER_KHRONOS_validation"
        };
    #else
        const std::vector<const char*> VALIDATION_LAYERS = {};
    #endif

    // Required device extensions
    const std::vector<const char*> DEVICE_EXTENSIONS = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VulkanGraphicsContext::VulkanGraphicsContext() = default;

    VulkanGraphicsContext::~VulkanGraphicsContext() {
        Shutdown();
    }

    bool VulkanGraphicsContext::Initialize(const GraphicsContextCreateInfo& createInfo) {
        if (m_initialized) {
            SA_LOG_WARN("VulkanGraphicsContext already initialized");
            return false;
        }

        m_width = createInfo.width;
        m_height = createInfo.height;
        m_enableValidation = createInfo.enableValidation;
        m_window = createInfo.window;

        SA_LOG_INFO("Initializing Vulkan graphics context...");
        SA_LOG_INFO("  API: Vulkan");
        SA_LOG_INFO("  Resolution: {}x{}", m_width, m_height);
        SA_LOG_INFO("  Validation: {}", m_enableValidation ? "Enabled" : "Disabled");

        // Initialize volk (Vulkan meta-loader)
        VkResult volkResult = volkInitialize();
        if (volkResult != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to initialize volk: VkResult = {}", static_cast<int>(volkResult));
            return false;
        }
        SA_LOG_INFO("volk initialized successfully");

        if (!CreateInstance(createInfo)) {
            SA_LOG_ERROR("Failed to create Vulkan instance");
            return false;
        }

        if (m_enableValidation && !SetupDebugMessenger()) {
            SA_LOG_ERROR("Failed to setup debug messenger");
            return false;
        }

        if (!CreateSurface(createInfo)) {
            SA_LOG_ERROR("Failed to create surface");
            return false;
        }

        if (!PickPhysicalDevice()) {
            SA_LOG_ERROR("Failed to pick physical device");
            return false;
        }

        if (!CreateLogicalDevice()) {
            SA_LOG_ERROR("Failed to create logical device");
            return false;
        }

        if (!CreateSwapchain()) {
            SA_LOG_ERROR("Failed to create swapchain");
            return false;
        }

        if (!CreateImageViews()) {
            SA_LOG_ERROR("Failed to create image views");
            return false;
        }

        if (!CreateCommandPool()) {
            SA_LOG_ERROR("Failed to create command pool");
            return false;
        }

        if (!CreateCommandBuffers()) {
            SA_LOG_ERROR("Failed to create command buffers");
            return false;
        }

        if (!CreateSyncObjects()) {
            SA_LOG_ERROR("Failed to create sync objects");
            return false;
        }

        if (!CreateVMAAllocator()) {
            SA_LOG_ERROR("Failed to create VMA allocator");
            return false;
        }

        m_initialized = true;
        SA_LOG_INFO("Vulkan graphics context initialized successfully");
        return true;
    }

    void VulkanGraphicsContext::Shutdown() {
        if (!m_initialized) {
            return;
        }

        WaitIdle();

        // Cleanup VMA allocator
        if (m_allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }

        // Cleanup swapchain
        DestroySwapchain();

        // Cleanup sync objects
        for (size_t i = 0; i < m_inFlightFences.size(); i++) {
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
        }

        // Cleanup command pool
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }

        // Cleanup device
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }

        // Cleanup debug messenger
        if (m_debugMessenger != VK_NULL_HANDLE && m_enableValidation) {
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                m_instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func != nullptr) {
                func(m_instance, m_debugMessenger, nullptr);
            }
            m_debugMessenger = VK_NULL_HANDLE;
        }

        // Cleanup surface
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }

        // Cleanup instance
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }

        m_initialized = false;
        SA_LOG_INFO("Vulkan graphics context shut down");
    }

    void VulkanGraphicsContext::BeginFrame() {
        if (!m_initialized) {
            return;
        }

        // Check if window was resized
        if (m_window) {
            uint32_t newWidth = m_window->GetWidth();
            uint32_t newHeight = m_window->GetHeight();
            if (newWidth > 0 && newHeight > 0 && (newWidth != m_width || newHeight != m_height)) {
                Resize(newWidth, newHeight);
            }
        }

        // Wait for the frame to be finished
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

        // Acquire next image from swapchain
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(
            m_device, m_swapchain, UINT64_MAX,
            m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            // Swapchain is out of date, need to recreate
            if (m_window) {
                Resize(m_window->GetWidth(), m_window->GetHeight());
            } else {
                Resize(m_width, m_height);
            }
            return;
        } else if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to acquire swapchain image");
            return;
        }

        // Reset fence
        vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

        // Reset command buffer
        vkResetCommandBuffer(m_commandBuffers[imageIndex], 0);
    }

    void VulkanGraphicsContext::EndFrame() {
        // This is where you would record commands
        // For now, it's a placeholder
    }

    void VulkanGraphicsContext::Present() {
        if (!m_initialized) {
            return;
        }

        uint32_t imageIndex = static_cast<uint32_t>(m_currentFrame);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffers[imageIndex];

        VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to submit draw command buffer");
            return;
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain;
        presentInfo.pImageIndices = &imageIndex;

        VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            if (m_window) {
                Resize(m_window->GetWidth(), m_window->GetHeight());
            } else {
                Resize(m_width, m_height);
            }
        } else if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to present swapchain image");
        }

        m_currentFrame = (m_currentFrame + 1) % m_inFlightFences.size();
    }

    void VulkanGraphicsContext::WaitIdle() {
        if (m_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device);
        }
    }

    void VulkanGraphicsContext::Resize(uint32_t width, uint32_t height) {
        if (!m_initialized || width == 0 || height == 0) {
            return;
        }

        if (width == m_width && height == m_height) {
            return;
        }

        WaitIdle();

        m_width = width;
        m_height = height;

        DestroySwapchain();
        if (!CreateSwapchain()) {
            SA_LOG_ERROR("Failed to recreate swapchain after resize");
            return;
        }
        if (!CreateImageViews()) {
            SA_LOG_ERROR("Failed to recreate image views after resize");
            return;
        }
        if (!CreateCommandBuffers()) {
            SA_LOG_ERROR("Failed to recreate command buffers after resize");
            return;
        }

        SA_LOG_INFO("Swapchain resized to {}x{}", width, height);
    }

    bool VulkanGraphicsContext::CreateInstance(const GraphicsContextCreateInfo& createInfo) {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = createInfo.applicationName;
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "StellarAlia";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInstanceInfo{};
        createInstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInstanceInfo.pApplicationInfo = &appInfo;

        auto extensions = GetRequiredExtensions(createInfo);
        if (extensions.empty()) {
            SA_LOG_ERROR("No Vulkan instance extensions available");
            return false;
        }

        createInstanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInstanceInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (m_enableValidation && VALIDATION_LAYERS.size() > 0) {
            if (CheckValidationLayerSupport()) {
                createInstanceInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
                createInstanceInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
                PopulateDebugMessengerCreateInfo(debugCreateInfo);
                createInstanceInfo.pNext = &debugCreateInfo;
            } else {
                SA_LOG_WARN("Validation layers requested but not available");
                createInstanceInfo.enabledLayerCount = 0;
                createInstanceInfo.pNext = nullptr;
            }
        } else {
            createInstanceInfo.enabledLayerCount = 0;
            createInstanceInfo.pNext = nullptr;
        }

        VkResult result = vkCreateInstance(&createInstanceInfo, nullptr, &m_instance);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("vkCreateInstance failed with VkResult: {}", static_cast<int>(result));
            return false;
        }

        // Load instance functions using volk
        volkLoadInstance(m_instance);
        SA_LOG_INFO("Vulkan instance created and loaded successfully");
        return true;
    }

    bool VulkanGraphicsContext::SetupDebugMessenger() {
        // Stub - full implementation would set up validation layer callbacks
        return true;
    }

    bool VulkanGraphicsContext::CreateSurface(const GraphicsContextCreateInfo& createInfo) {
        if (!createInfo.window) {
            SA_LOG_ERROR("Window is required for Vulkan surface creation");
            return false;
        }

        if (!createInfo.window->CreateVulkanSurface(m_instance, nullptr, &m_surface)) {
            SA_LOG_ERROR("Failed to create Vulkan surface from window");
            return false;
        }

        SA_LOG_INFO("Vulkan surface created successfully using {}", 
            createInfo.window->GetBackend() == Window::WindowBackend::SDL2 ? "SDL2" : "GLFW");
        return true;
    }

    bool VulkanGraphicsContext::PickPhysicalDevice() {
        uint32_t deviceCount = 0;
        VkResult result = vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to enumerate physical devices: VkResult = {}", static_cast<int>(result));
            return false;
        }

        if (deviceCount == 0) {
            SA_LOG_ERROR("No Vulkan physical devices found");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        result = vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to get physical devices: VkResult = {}", static_cast<int>(result));
            return false;
        }

        // Pick the first suitable device
        for (const auto& device : devices) {
            if (FindQueueFamilies(device).IsComplete()) {
                m_physicalDevice = device;
                return true;
            }
        }

        SA_LOG_ERROR("No suitable physical device found");
        return false;
    }

    bool VulkanGraphicsContext::CreateLogicalDevice() {
        QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = { indices.graphics, indices.present };

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
        createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();

        if (m_enableValidation) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
            createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to create logical device: VkResult = {}", static_cast<int>(result));
            return false;
        }

        // Load device functions using volk
        volkLoadDevice(m_device);

        vkGetDeviceQueue(m_device, indices.graphics, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, indices.present, 0, &m_presentQueue);
        m_graphicsQueueFamily = indices.graphics;
        m_presentQueueFamily = indices.present;

        if (m_graphicsQueue == VK_NULL_HANDLE || m_presentQueue == VK_NULL_HANDLE) {
            SA_LOG_ERROR("Failed to get device queues");
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
            return false;
        }

        SA_LOG_INFO("Vulkan logical device created and loaded successfully");
        return true;
    }

    bool VulkanGraphicsContext::CreateSwapchain() {
        if (m_physicalDevice == VK_NULL_HANDLE || m_surface == VK_NULL_HANDLE) {
            SA_LOG_ERROR("Cannot create swapchain: physical device or surface is invalid");
            return false;
        }

        // Get surface capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to get surface capabilities: VkResult = {}", static_cast<int>(result));
            return false;
        }

        // Choose swapchain format
        uint32_t formatCount = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to get surface format count: VkResult = {}", static_cast<int>(result));
            return false;
        }

        if (formatCount == 0) {
            SA_LOG_ERROR("No surface formats available");
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to get surface formats: VkResult = {}", static_cast<int>(result));
            return false;
        }

        // Choose the first available format as default
        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = format;
                break;
            }
        }
        m_swapchainImageFormat = surfaceFormat.format;

        // Choose swapchain extent
        if (capabilities.currentExtent.width != UINT32_MAX) {
            m_swapchainExtent = capabilities.currentExtent;
        } else {
            m_swapchainExtent.width = std::clamp(m_width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            m_swapchainExtent.height = std::clamp(m_height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        // Choose image count
        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        // Create swapchain
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = m_swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = { m_graphicsQueueFamily, m_presentQueueFamily };
        if (m_graphicsQueueFamily != m_presentQueueFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }

        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        result = vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to create swapchain: VkResult = {}", static_cast<int>(result));
            return false;
        }

        // Get swapchain images
        result = vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to get swapchain image count: VkResult = {}", static_cast<int>(result));
            return false;
        }

        if (imageCount == 0) {
            SA_LOG_ERROR("Swapchain has no images");
            return false;
        }

        m_swapchainImages.resize(imageCount);
        result = vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to get swapchain images: VkResult = {}", static_cast<int>(result));
            return false;
        }

        return true;
    }

    void VulkanGraphicsContext::DestroySwapchain() {
        for (auto imageView : m_swapchainImageViews) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
        m_swapchainImageViews.clear();

        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    bool VulkanGraphicsContext::CreateImageViews() {
        if (m_swapchainImages.empty()) {
            SA_LOG_ERROR("Cannot create image views: no swapchain images");
            return false;
        }

        m_swapchainImageViews.resize(m_swapchainImages.size());

        for (size_t i = 0; i < m_swapchainImages.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_swapchainImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkResult result = vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]);
            if (result != VK_SUCCESS) {
                SA_LOG_ERROR("Failed to create image view {}: VkResult = {}", i, static_cast<int>(result));
                // Cleanup already created image views
                for (size_t j = 0; j < i; j++) {
                    vkDestroyImageView(m_device, m_swapchainImageViews[j], nullptr);
                }
                m_swapchainImageViews.clear();
                return false;
            }
        }

        return true;
    }

    bool VulkanGraphicsContext::CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

        VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to create command pool: VkResult = {}", static_cast<int>(result));
            return false;
        }

        return true;
    }

    bool VulkanGraphicsContext::CreateCommandBuffers() {
        if (m_swapchainImages.empty()) {
            SA_LOG_ERROR("Cannot create command buffers: no swapchain images");
            return false;
        }

        m_commandBuffers.resize(m_swapchainImages.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

        VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data());
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to allocate command buffers: VkResult = {}", static_cast<int>(result));
            m_commandBuffers.clear();
            return false;
        }

        return true;
    }

    bool VulkanGraphicsContext::CreateSyncObjects() {
        const size_t maxFramesInFlight = 2;
        m_imageAvailableSemaphores.resize(maxFramesInFlight);
        m_renderFinishedSemaphores.resize(maxFramesInFlight);
        m_inFlightFences.resize(maxFramesInFlight);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < maxFramesInFlight; i++) {
            VkResult result1 = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
            if (result1 != VK_SUCCESS) {
                SA_LOG_ERROR("Failed to create image available semaphore {}: VkResult = {}", i, static_cast<int>(result1));
                // Cleanup already created semaphores
                for (size_t j = 0; j < i; j++) {
                    vkDestroySemaphore(m_device, m_imageAvailableSemaphores[j], nullptr);
                    vkDestroySemaphore(m_device, m_renderFinishedSemaphores[j], nullptr);
                    vkDestroyFence(m_device, m_inFlightFences[j], nullptr);
                }
                m_imageAvailableSemaphores.clear();
                m_renderFinishedSemaphores.clear();
                m_inFlightFences.clear();
                return false;
            }

            VkResult result2 = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
            if (result2 != VK_SUCCESS) {
                SA_LOG_ERROR("Failed to create render finished semaphore {}: VkResult = {}", i, static_cast<int>(result2));
                // Cleanup
                vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
                for (size_t j = 0; j < i; j++) {
                    vkDestroySemaphore(m_device, m_imageAvailableSemaphores[j], nullptr);
                    vkDestroySemaphore(m_device, m_renderFinishedSemaphores[j], nullptr);
                    vkDestroyFence(m_device, m_inFlightFences[j], nullptr);
                }
                m_imageAvailableSemaphores.clear();
                m_renderFinishedSemaphores.clear();
                m_inFlightFences.clear();
                return false;
            }

            VkResult result3 = vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]);
            if (result3 != VK_SUCCESS) {
                SA_LOG_ERROR("Failed to create fence {}: VkResult = {}", i, static_cast<int>(result3));
                // Cleanup
                vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
                vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
                for (size_t j = 0; j < i; j++) {
                    vkDestroySemaphore(m_device, m_imageAvailableSemaphores[j], nullptr);
                    vkDestroySemaphore(m_device, m_renderFinishedSemaphores[j], nullptr);
                    vkDestroyFence(m_device, m_inFlightFences[j], nullptr);
                }
                m_imageAvailableSemaphores.clear();
                m_renderFinishedSemaphores.clear();
                m_inFlightFences.clear();
                return false;
            }
        }

        return true;
    }

    bool VulkanGraphicsContext::CheckValidationLayerSupport() {
        // vkEnumerateInstanceLayerProperties is a global function available from the loader
        // It doesn't require an instance, but we need to make sure the loader is loaded
        uint32_t layerCount = 0;
        VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        if (result != VK_SUCCESS) {
            SA_LOG_WARN("Failed to enumerate instance layer properties: VkResult = {}", static_cast<int>(result));
            return false;
        }

        if (layerCount == 0) {
            SA_LOG_INFO("No validation layers available");
            return false;
        }

        std::vector<VkLayerProperties> availableLayers(layerCount);
        result = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        if (result != VK_SUCCESS) {
            SA_LOG_WARN("Failed to get instance layer properties: VkResult = {}", static_cast<int>(result));
            return false;
        }

        for (const char* layerName : VALIDATION_LAYERS) {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
            if (!layerFound) {
                return false;
            }
        }
        return true;
    }

    std::vector<const char*> VulkanGraphicsContext::GetRequiredExtensions(const GraphicsContextCreateInfo& createInfo) {
        std::vector<const char*> extensions;

        // Get required extensions from window
        if (!createInfo.window) {
            SA_LOG_ERROR("Window is required to get Vulkan instance extensions");
            return extensions;  // Return empty, will fail later with better error message
        }

        uint32_t extensionCount = 0;
        const char* const* windowExtensions = createInfo.window->GetVulkanInstanceExtensions(&extensionCount);
        
        if (!windowExtensions || extensionCount == 0) {
            SA_LOG_ERROR("Failed to get Vulkan instance extensions from window (windowExtensions is {}, count={})", 
                windowExtensions ? "valid" : "null", extensionCount);
            return extensions;  // Return empty, will fail later with better error message
        }

        extensions.reserve(extensionCount + (m_enableValidation ? 1 : 0));
        for (uint32_t i = 0; i < extensionCount; i++) {
            if (windowExtensions[i]) {
                extensions.push_back(windowExtensions[i]);
            }
        }

        if (m_enableValidation) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void VulkanGraphicsContext::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = nullptr;
    }

    VulkanGraphicsContext::QueueFamilyIndices VulkanGraphicsContext::FindQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphics = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
            if (presentSupport) {
                indices.present = i;
            }

            if (indices.IsComplete()) {
                break;
            }
        }

        return indices;
    }

    bool VulkanGraphicsContext::CreateVMAAllocator() {
        // Validate that all required handles are valid
        if (m_physicalDevice == VK_NULL_HANDLE) {
            SA_LOG_ERROR("Cannot create VMA allocator: physical device is invalid");
            return false;
        }
        if (m_device == VK_NULL_HANDLE) {
            SA_LOG_ERROR("Cannot create VMA allocator: logical device is invalid");
            return false;
        }
        if (m_instance == VK_NULL_HANDLE) {
            SA_LOG_ERROR("Cannot create VMA allocator: instance is invalid");
            return false;
        }

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = m_physicalDevice;
        allocatorInfo.device = m_device;
        allocatorInfo.instance = m_instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;

        // Provide explicit function pointers for volk compatibility
        // VMA needs vkGetInstanceProcAddr and vkGetDeviceProcAddr to load other functions
        // With volk, these are already loaded and available
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        // Leave other function pointers as nullptr - VMA will load them using the proc addr functions
        
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;

        // Optional: Enable VMA features
        // allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        VkResult result = vmaCreateAllocator(&allocatorInfo, &m_allocator);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to create VMA allocator: VkResult = {}", static_cast<int>(result));
            return false;
        }

        if (m_allocator == VK_NULL_HANDLE) {
            SA_LOG_ERROR("VMA allocator creation returned success but allocator is null");
            return false;
        }

        return true;
    }

} // namespace StellarAlia::Function::Graphics

