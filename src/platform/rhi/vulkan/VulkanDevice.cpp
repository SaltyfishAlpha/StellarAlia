// Prevent Windows.h min/max macros from shadowing std::min / std::max
#define NOMINMAX

// VMA implementation — must appear in exactly one translation unit
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <vma/vk_mem_alloc.h>
#pragma GCC diagnostic pop

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/vulkan/VulkanUtils.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace StellarAlia::RHI {

// ─────────────────────────────────────────────────────────────────────────────
// Validation debug callback
// ─────────────────────────────────────────────────────────────────────────────
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       /*user*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        SA_LOG_ERROR("[Vulkan] {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        SA_LOG_WARN("[Vulkan] {}", data->pMessage);
    else
        SA_LOG_DEBUG("[Vulkan] {}", data->pMessage);
    return VK_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<VulkanDevice> VulkanDevice::Create(const RHIDeviceDesc& desc) {
    if (!desc.windowHandle.IsValid()) {
        SA_LOG_CRITICAL("VulkanDevice::Create — windowHandle is null");
        return nullptr;
    }

    if (volkInitialize() != VK_SUCCESS) {
        SA_LOG_CRITICAL("VulkanDevice::Create — volkInitialize() failed (is Vulkan installed?)");
        return nullptr;
    }

    auto dev = std::unique_ptr<VulkanDevice>(new VulkanDevice());
    dev->m_vsync = desc.vsync;

    try {
        dev->InitInstance(desc.enableValidation);
        volkLoadInstance(dev->m_instance);

        dev->InitSurface(desc.windowHandle.ptr);
        dev->InitPhysicalDevice();
        dev->InitDevice();
        volkLoadDevice(dev->m_device);

        dev->InitAllocator();
        dev->CreateSwapchain(desc.swapchainWidth, desc.swapchainHeight, desc.vsync);
        dev->CreateFrameData();
        dev->InitDescriptorPool();
        dev->InitDefaultSamplers();
    } catch (const std::exception& e) {
        SA_LOG_CRITICAL("VulkanDevice::Create — {}", e.what());
        return nullptr;
    }

    SA_LOG_INFO("VulkanDevice: initialized successfully");
    return dev;
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
VulkanDevice::~VulkanDevice() {
    // Issue #72 Step 7.5: drain deferred-destroy queues while the pool and
    // allocator are still alive. Use the C++ WaitIdle() (which also flushes)
    // rather than the raw vkDeviceWaitIdle.
    if (m_device) WaitIdle();

    // Destroy all user resources before pools/allocator
    for (auto& e : m_pipelines) {
        if (!e.valid) continue;
        if (e.pipeline) vkDestroyPipeline(m_device, e.pipeline, nullptr);
        if (e.layout)   vkDestroyPipelineLayout(m_device, e.layout, nullptr);
    }
    for (auto& e : m_shaders)
        if (e.valid && e.module) vkDestroyShaderModule(m_device, e.module, nullptr);
    for (auto& e : m_descLayouts)
        if (e.valid && e.layout) vkDestroyDescriptorSetLayout(m_device, e.layout, nullptr);
    // DescSets are freed when the pool is destroyed — no individual free needed.
    for (auto& e : m_buffers)
        if (e.valid) vmaDestroyBuffer(m_allocator, e.buffer, e.alloc);
    for (auto& e : m_textures) {
        if (!e.valid || e.swapchain) continue;
        for (auto v : e.mipViews)
            if (v) vkDestroyImageView(m_device, v, nullptr);
        if (e.view)             vkDestroyImageView(m_device, e.view, nullptr);
        if (e.sampledDepthView) vkDestroyImageView(m_device, e.sampledDepthView, nullptr);
        if (e.alloc)            vmaDestroyImage(m_allocator, e.image, e.alloc);
    }

    if (m_samplerLinearRepeat)  vkDestroySampler(m_device, m_samplerLinearRepeat,  nullptr);
    if (m_samplerNearestRepeat) vkDestroySampler(m_device, m_samplerNearestRepeat, nullptr);
    if (m_emptyDescLayout) vkDestroyDescriptorSetLayout(m_device, m_emptyDescLayout, nullptr);
    if (m_descPool)             vkDestroyDescriptorPool(m_device, m_descPool, nullptr);

    // Immediate submit infrastructure
    if (m_immFence)   vkDestroyFence(m_device, m_immFence, nullptr);
    if (m_immCmdPool) vkDestroyCommandPool(m_device, m_immCmdPool, nullptr);

    DestroyFrameData();
    DestroySwapchain();

    if (m_allocator) vmaDestroyAllocator(m_allocator);
    if (m_surface)   vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_device)    vkDestroyDevice(m_device, nullptr);

    if (m_debugMessenger) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, m_debugMessenger, nullptr);
    }

    if (m_instance) vkDestroyInstance(m_instance, nullptr);
    SA_LOG_INFO("VulkanDevice: destroyed");
}

// ─────────────────────────────────────────────────────────────────────────────
// InitInstance
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::InitInstance(bool validation) {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "StellarAlia";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "StellarAlia";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    // Extensions from GLFW + optional debug utils
    uint32_t    glfwExtCount = 0;
    const char** glfwExts   = glfwGetRequiredInstanceExtensions(&glfwExtCount);

    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);
    if (validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // Validation layers
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    std::vector<const char*> layers;
    if (validation) {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());

        bool found = false;
        for (auto& lp : available)
            if (strcmp(lp.layerName, validationLayer) == 0) { found = true; break; }

        if (found) {
            layers.push_back(validationLayer);
            SA_LOG_INFO("VulkanDevice: validation layer enabled");
        } else {
            SA_LOG_WARN("VulkanDevice: validation layer not available, skipping");
        }
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount       = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames     = layers.data();

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");

    // Debug messenger
    if (validation && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
        dbgCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCI.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = DebugCallback;

        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, &dbgCI, nullptr, &m_debugMessenger);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// InitSurface
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::InitSurface(void* glfwWindow) {
    if (glfwCreateWindowSurface(m_instance,
                                static_cast<GLFWwindow*>(glfwWindow),
                                nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("glfwCreateWindowSurface failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// InitPhysicalDevice
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::InitPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Score each candidate; pick the highest.  Discrete GPU always beats integrated.
    auto deviceTypeScore = [](VkPhysicalDeviceType t) -> int {
        switch (t) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 1000;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return  100;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return   50;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            return   10;
            default:                                     return    1;
        }
    };

    VkPhysicalDevice bestDevice   = VK_NULL_HANDLE;
    uint32_t         bestFamily   = 0;
    int              bestScore    = -1;
    std::string      bestName;

    for (auto pd : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);

        // Find a queue family supporting graphics + present
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, qfs.data());

        int32_t gfxFamily = -1;
        for (uint32_t i = 0; i < qfCount; i++) {
            if (!(qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, m_surface, &presentSupport);
            if (presentSupport) { gfxFamily = static_cast<int32_t>(i); break; }
        }
        if (gfxFamily < 0) continue;

        // Check VK_KHR_swapchain support
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, exts.data());
        bool hasSwapchain = false;
        for (auto& e : exts)
            if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                { hasSwapchain = true; break; }
        if (!hasSwapchain) continue;

        // Check Vulkan 1.3 features (dynamic rendering + synchronization2)
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(pd, &features2);
        if (!features13.dynamicRendering || !features13.synchronization2) continue;

        const int score = deviceTypeScore(props.deviceType);
        SA_LOG_INFO("VulkanDevice: candidate GPU '{}' (score {})", props.deviceName, score);
        if (score > bestScore) {
            bestScore  = score;
            bestDevice = pd;
            bestFamily = static_cast<uint32_t>(gfxFamily);
            bestName   = props.deviceName;
        }
    }

    if (bestDevice == VK_NULL_HANDLE)
        throw std::runtime_error("No suitable GPU found (need graphics+present, swapchain, Vulkan 1.3)");

    m_physDevice     = bestDevice;
    m_graphicsFamily = bestFamily;
    m_gpuName        = bestName;
    SA_LOG_INFO("VulkanDevice: selected GPU '{}'", m_gpuName);
}

// ─────────────────────────────────────────────────────────────────────────────
// InitDevice
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::InitDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_graphicsFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType                                          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.descriptorBindingUniformBufferUpdateAfterBind  = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind   = VK_TRUE;
    features12.descriptorBindingStorageBufferUpdateAfterBind  = VK_TRUE;
    features12.descriptorBindingStorageImageUpdateAfterBind   = VK_TRUE;
    // Bindless: large sampled-image array indexed by SSBO-loaded uint at runtime.
    features12.runtimeDescriptorArray                         = VK_TRUE;
    features12.descriptorBindingPartiallyBound                = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing      = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext              = &features12;
    features13.dynamicRendering   = VK_TRUE;
    features13.synchronization2   = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    const char* deviceExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext                   = &features2;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = deviceExts;

    if (vkCreateDevice(m_physDevice, &ci, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physDevice, &props);
    m_minStorageBufferOffsetAlignment = props.limits.minStorageBufferOffsetAlignment;
    SA_LOG_INFO("VulkanDevice: minStorageBufferOffsetAlignment = {} B",
                static_cast<uint64_t>(m_minStorageBufferOffsetAlignment));
}

// ─────────────────────────────────────────────────────────────────────────────
// InitAllocator
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::InitAllocator() {
    VmaVulkanFunctions vmaFuncs{};
    vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo ci{};
    ci.vulkanApiVersion  = VK_API_VERSION_1_3;
    ci.physicalDevice    = m_physDevice;
    ci.device            = m_device;
    ci.instance          = m_instance;
    ci.pVulkanFunctions  = &vmaFuncs;

    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS)
        throw std::runtime_error("vmaCreateAllocator failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateSwapchain
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::CreateSwapchain(uint32_t width, uint32_t height, bool vsync) {
    // Surface capabilities
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physDevice, m_surface, &caps);

    // Format selection: prefer B8G8R8A8_SRGB, fallback to first available
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { chosen = f; break; }

    // Present mode: MAILBOX (no vsync) > FIFO (vsync)
    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &pmCount, presentModes.data());

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
    if (!vsync)
        for (auto pm : presentModes)
            if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = pm; break; }

    // Extent
    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width  = std::clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);
    m_swapMinImageCount = imageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = m_surface;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = presentMode;
    ci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");

    m_swapchainVkFormat = chosen.format;
    m_swapchainExtent   = extent;

    // Retrieve swapchain images
    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, nullptr);
    m_swapImages.resize(imgCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, m_swapImages.data());

    // Create image views + register handles
    m_swapImageViews.resize(imgCount);
    m_swapHandles.resize(imgCount);

    RHITextureDesc scDesc{};
    scDesc.width  = extent.width;
    scDesc.height = extent.height;
    scDesc.format = FromVkFormat(chosen.format);
    scDesc.usage  = RHITextureUsage::RenderTarget;

    for (uint32_t i = 0; i < imgCount; i++) {
        VkImageViewCreateInfo viewCI{};
        viewCI.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image    = m_swapImages[i];
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format   = chosen.format;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(m_device, &viewCI, nullptr, &m_swapImageViews[i]);

        m_swapHandles[i] = AllocTextureSlot(
            m_swapImages[i], m_swapImageViews[i], VK_NULL_HANDLE, scDesc, /*swapchain=*/true);
    }

    // Create one renderDone semaphore per swapchain image.
    // Indexed by image (not frame slot) so the presentation engine never
    // sees a semaphore reused while it still holds a reference to it.
    VkSemaphoreCreateInfo rdSemCI{};
    rdSemCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    m_renderDoneSems.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; i++)
        vkCreateSemaphore(m_device, &rdSemCI, nullptr, &m_renderDoneSems[i]);

    SA_LOG_INFO("VulkanDevice: swapchain {}x{} {} images",
                extent.width, extent.height, imgCount);
}

void VulkanDevice::DestroySwapchain() {
    for (auto sem : m_renderDoneSems)
        if (sem) vkDestroySemaphore(m_device, sem, nullptr);
    m_renderDoneSems.clear();

    for (auto& view : m_swapImageViews)
        if (view) vkDestroyImageView(m_device, view, nullptr);
    m_swapImageViews.clear();

    // Invalidate swapchain handle slots (images are owned by the swapchain)
    for (auto h : m_swapHandles)
        if (h.IsValid() && h.index < m_textures.size())
            m_textures[h.index].valid = false;
    m_swapImages.clear();
    m_swapHandles.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VulkanDevice::RecreateSwapchain() {
    // On Windows, minimizing the window causes the surface to report currentExtent={0,0}.
    // vkCreateSwapchainKHR requires a non-zero extent, so bail out and keep the existing
    // swapchain intact. The next frame's vkAcquireNextImageKHR will return OUT_OF_DATE
    // again, retriggering this path until the window is restored.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physDevice, m_surface, &caps);
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
        return;

    vkDeviceWaitIdle(m_device);
    DestroySwapchain();
    CreateSwapchain(m_swapchainExtent.width, m_swapchainExtent.height, m_vsync);
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateFrameData
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::CreateFrameData() {
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = m_graphicsFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandBufferAllocateInfo cmdAI{};
    cmdAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAI.commandBufferCount = 1;

    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenCI{};
    fenCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenCI.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so first wait doesn't hang

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        FrameData& f = m_frames[i];
        vkCreateCommandPool(m_device, &poolCI, nullptr, &f.pool);
        cmdAI.commandPool = f.pool;
        vkAllocateCommandBuffers(m_device, &cmdAI, &f.cmd);
        vkCreateSemaphore(m_device, &semCI, nullptr, &f.imgReady);
        vkCreateFence(m_device, &fenCI, nullptr, &f.fence);
    }
}

void VulkanDevice::DestroyFrameData() {
    // Issue #72 Step 7.5: drain anything still queued for deferred destruction
    // before tearing down per-frame primitives. Caller (VulkanDevice dtor) must
    // have already called vkDeviceWaitIdle, so all slots are safe to flush.
    for (uint32_t i = 0; i < MAX_FRAMES; ++i)
        FlushPendingFree(i);

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        FrameData& f = m_frames[i];
        if (f.fence)    vkDestroyFence(m_device, f.fence, nullptr);
        if (f.imgReady) vkDestroySemaphore(m_device, f.imgReady, nullptr);
        if (f.pool)       vkDestroyCommandPool(m_device, f.pool, nullptr);
        f = {};
    }
}

void VulkanDevice::FlushPendingFree(uint32_t slot) {
    if (slot >= MAX_FRAMES) return;
    PendingFree& pf = m_pendingFree[slot];
    if (!pf.descSets.empty()) {
        vkFreeDescriptorSets(m_device, m_descPool,
                             static_cast<uint32_t>(pf.descSets.size()),
                             pf.descSets.data());
        pf.descSets.clear();
    }
    for (auto& [buf, alloc] : pf.buffers)
        vmaDestroyBuffer(m_allocator, buf, alloc);
    pf.buffers.clear();
    for (auto& img : pf.images) {
        for (auto v : img.mipViews)
            if (v) vkDestroyImageView(m_device, v, nullptr);
        if (img.view)             vkDestroyImageView(m_device, img.view, nullptr);
        if (img.sampledDepthView) vkDestroyImageView(m_device, img.sampledDepthView, nullptr);
        if (img.alloc)            vmaDestroyImage(m_allocator, img.image, img.alloc);
    }
    pf.images.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Loop
// ─────────────────────────────────────────────────────────────────────────────
IRHICommandList* VulkanDevice::BeginFrame() {
    if (m_needResize) {
        RecreateSwapchain();
        m_needResize = false;
    }

    FrameData& f = m_frames[m_frameIdx];

    // Wait for this frame slot to become available
    vkWaitForFences(m_device, 1, &f.fence, VK_TRUE, UINT64_MAX);

    // Issue #72 Step 7.5: GPU has finished with this slot's previous submit,
    // so any Vulkan objects we queued for deferred destruction back then are
    // safe to free now.
    FlushPendingFree(m_frameIdx);

    // Acquire swapchain image
    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX, f.imgReady, VK_NULL_HANDLE, &m_imageIdx);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return nullptr; // caller should skip this frame
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR failed");

    vkResetFences(m_device, 1, &f.fence);

    // Reset and begin command buffer
    vkResetCommandPool(m_device, f.pool, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &beginInfo);

    // Transition swapchain image: UNDEFINED → TRANSFER_DST (for clear)
    CmdTransitionImage(f.cmd, m_swapImages[m_imageIdx],
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Clear to a dark charcoal-blue background
    VkClearColorValue clearColor{};
    clearColor.float32[0] = 0.08f;
    clearColor.float32[1] = 0.09f;
    clearColor.float32[2] = 0.12f;
    clearColor.float32[3] = 1.0f;
    VkImageSubresourceRange range{};
    range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount     = 1;
    range.layerCount     = 1;
    vkCmdClearColorImage(f.cmd, m_swapImages[m_imageIdx],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clearColor, 1, &range);

    // Transition TRANSFER_DST → COLOR_ATTACHMENT (caller may render on top)
    CmdTransitionImage(f.cmd, m_swapImages[m_imageIdx],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    m_cmdList.Bind(f.cmd, this);
    return &m_cmdList;
}

void VulkanDevice::EndFrame() {
    FrameData& f = m_frames[m_frameIdx];

    // Transition swapchain image: COLOR_ATTACHMENT → PRESENT_SRC
    CmdTransitionImage(f.cmd, m_swapImages[m_imageIdx],
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(f.cmd);
}

void VulkanDevice::Present() {
    FrameData& f = m_frames[m_frameIdx];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &f.imgReady;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &m_renderDoneSems[m_imageIdx];

    vkQueueSubmit(m_graphicsQueue, 1, &si, f.fence);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_renderDoneSems[m_imageIdx];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &m_imageIdx;

    VkResult result = vkQueuePresentKHR(m_graphicsQueue, &pi);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        m_needResize = true;

    m_frameIdx = (m_frameIdx + 1) % MAX_FRAMES;
}

void VulkanDevice::WaitIdle() {
    if (!m_device) return;
    vkDeviceWaitIdle(m_device);
    // Issue #72 Step 7.5: WaitIdle means all GPU work is done — flush every
    // slot's deferred-destroy queue so callers (resize / scene switch) don't
    // accumulate ghost resources held alive across the wait.
    for (uint32_t i = 0; i < MAX_FRAMES; ++i)
        FlushPendingFree(i);
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain accessors
// ─────────────────────────────────────────────────────────────────────────────
RHITextureHandle VulkanDevice::GetSwapchainTexture() {
    if (m_imageIdx < m_swapHandles.size()) return m_swapHandles[m_imageIdx];
    return {};
}

RHIFormat VulkanDevice::GetSwapchainFormat() {
    return FromVkFormat(m_swapchainVkFormat);
}

uint32_t VulkanDevice::GetSwapchainWidth()  { return m_swapchainExtent.width; }
uint32_t VulkanDevice::GetSwapchainHeight() { return m_swapchainExtent.height; }

VulkanDevice::ImGuiVulkanContext VulkanDevice::GetImGuiContext() const {
    return ImGuiVulkanContext{
        m_instance,
        m_physDevice,
        m_device,
        m_graphicsQueue,
        m_graphicsFamily,
        static_cast<uint32_t>(m_swapImages.size()),
        m_swapMinImageCount,
        m_swapchainVkFormat,
    };
}

void VulkanDevice::ResizeSwapchain(uint32_t width, uint32_t height) {
    m_swapchainExtent = {width, height};
    m_needResize      = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
RHITextureHandle VulkanDevice::AllocTextureSlot(VkImage image, VkImageView view,
                                                 VmaAllocation alloc,
                                                 const RHITextureDesc& desc,
                                                 bool isSwapchain) {
    RHITextureHandle h{static_cast<uint32_t>(m_textures.size())};
    TextureEntry entry{};
    entry.image     = image;
    entry.view      = view;
    entry.alloc     = alloc;
    entry.desc      = desc;
    entry.valid     = true;
    entry.swapchain = isSwapchain;
    m_textures.push_back(std::move(entry));
    return h;
}

VkImage VulkanDevice::GetVkImage(RHITextureHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_textures.size()) return VK_NULL_HANDLE;
    return m_textures[handle.index].valid ? m_textures[handle.index].image : VK_NULL_HANDLE;
}

VkImageView VulkanDevice::GetVkImageView(RHITextureHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_textures.size()) return VK_NULL_HANDLE;
    return m_textures[handle.index].valid ? m_textures[handle.index].view : VK_NULL_HANDLE;
}

// ─────────────────────────────────────────────────────────────────────────────
// InitDescriptorPool
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::InitDescriptorPool() {
    // Large pre-allocated pool — sufficient for early stages.
    // Upgrade to dynamic pool chains in a later optimisation pass.
    // COMBINED_IMAGE_SAMPLER must cover BindlessTextureHeap (4096 slots) + the
    // per-material legacy bindings; STORAGE_BUFFER_DYNAMIC reserves room for
    // the MaterialParamRing descriptor + future per-frame SSBO rings.
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  4608},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,           256},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,           256},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,    64},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            128},
    };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                       VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    ci.maxSets       = 1024;
    ci.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    ci.pPoolSizes    = sizes;

    if (vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descPool) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDescriptorPool failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// InitDefaultSamplers + ImmediateSubmit
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::InitDefaultSamplers() {
    // Empty descriptor set layout — 0 bindings, used as a placeholder to preserve
    // set-index positions in pipeline layouts when set=0 is absent but set=1+ are needed.
    VkDescriptorSetLayoutCreateInfo emptyCI{};
    emptyCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    emptyCI.bindingCount = 0;
    vkCreateDescriptorSetLayout(m_device, &emptyCI, nullptr, &m_emptyDescLayout);

    auto makeSampler = [&](VkFilter filter, VkSamplerAddressMode wrap) -> VkSampler {
        VkSamplerCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter    = filter;
        ci.minFilter    = filter;
        ci.mipmapMode   = (filter == VK_FILTER_LINEAR)
                              ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                              : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU = wrap;
        ci.addressModeV = wrap;
        ci.addressModeW = wrap;
        ci.maxLod       = VK_LOD_CLAMP_NONE;
        VkSampler s = VK_NULL_HANDLE;
        vkCreateSampler(m_device, &ci, nullptr, &s);
        return s;
    };
    m_samplerLinearRepeat  = makeSampler(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_REPEAT);
    m_samplerNearestRepeat = makeSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // Allocate the immediate-submit command pool + buffer + fence.
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = m_graphicsFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_device, &poolCI, nullptr, &m_immCmdPool);

    VkCommandBufferAllocateInfo cmdAI{};
    cmdAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAI.commandPool        = m_immCmdPool;
    cmdAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAI.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &cmdAI, &m_immCmd);

    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(m_device, &fenceCI, nullptr, &m_immFence);
}

void VulkanDevice::ImmediateCompute(std::function<void(IRHICommandList*)> fn) {
    // Save the current binding — ImmediateCompute may be called from uiPass
    // while m_cmdList is already bound to the frame command buffer.
    // Restore it afterward so subsequent recording (e.g. ImGui) still works.
    const VkCommandBuffer savedCmd = m_cmdList.GetVkCommandBuffer();
    ImmediateSubmit([&](VkCommandBuffer cmd) {
        m_cmdList.Bind(cmd, this);
        fn(&m_cmdList);
    });
    m_cmdList.Bind(savedCmd, savedCmd != VK_NULL_HANDLE ? this : nullptr);
}

void VulkanDevice::ImmediateSubmit(std::function<void(VkCommandBuffer)>&& fn) {
    vkResetCommandBuffer(m_immCmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_immCmd, &beginInfo);

    fn(m_immCmd);

    vkEndCommandBuffer(m_immCmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &m_immCmd;
    vkQueueSubmit(m_graphicsQueue, 1, &si, m_immFence);
    vkWaitForFences(m_device, 1, &m_immFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_immFence);
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateBuffer / UploadBufferData / DestroyBuffer
// ─────────────────────────────────────────────────────────────────────────────
static VkBufferUsageFlags ToVkBufferUsage(RHIBufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    auto u = static_cast<uint32_t>(usage);
    if (u & static_cast<uint32_t>(RHIBufferUsage::Vertex))       flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (u & static_cast<uint32_t>(RHIBufferUsage::Index))        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (u & static_cast<uint32_t>(RHIBufferUsage::Uniform))      flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (u & static_cast<uint32_t>(RHIBufferUsage::Storage))      flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (u & static_cast<uint32_t>(RHIBufferUsage::IndirectArgs)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (u & static_cast<uint32_t>(RHIBufferUsage::CopySrc))      flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (u & static_cast<uint32_t>(RHIBufferUsage::CopyDst))      flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

RHIBufferHandle VulkanDevice::CreateBuffer(const RHIBufferDesc& desc) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = desc.size;
    bci.usage = ToVkBufferUsage(desc.usage);
    // Staging buffers also need TRANSFER_SRC; GPU-only vertex/index need TRANSFER_DST.
    if (!desc.cpuVisible) bci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = desc.cpuVisible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
    if (desc.cpuVisible) aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer      buf   = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    if (vmaCreateBuffer(m_allocator, &bci, &aci, &buf, &alloc, nullptr) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreateBuffer — vmaCreateBuffer failed (size={})", desc.size);
        return {};
    }

    if (desc.debugName) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType   = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(buf);
        nameInfo.pObjectName  = desc.debugName;
        auto fn = (PFN_vkSetDebugUtilsObjectNameEXT)
            vkGetDeviceProcAddr(m_device, "vkSetDebugUtilsObjectNameEXT");
        if (fn) fn(m_device, &nameInfo);
    }

    RHIBufferHandle h{static_cast<uint32_t>(m_buffers.size())};
    m_buffers.push_back({buf, alloc, desc, true});
    return h;
}

void VulkanDevice::UploadBufferData(RHIBufferHandle handle,
                                    const void* data, uint64_t size, uint64_t offset) {
    if (!handle.IsValid() || handle.index >= m_buffers.size()) return;
    auto& entry = m_buffers[handle.index];
    if (!entry.valid) return;

    if (entry.desc.cpuVisible) {
        // Direct map — VMA already keeps it persistently mapped.
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(m_allocator, entry.alloc, &info);
        memcpy(static_cast<uint8_t*>(info.pMappedData) + offset, data, size);
        vmaFlushAllocation(m_allocator, entry.alloc, offset, size);
    } else {
        // Stage through a temporary CPU-visible buffer then copy.
        RHIBufferDesc stagingDesc{};
        stagingDesc.size       = size;
        stagingDesc.usage      = RHIBufferUsage::CopySrc;
        stagingDesc.cpuVisible = true;
        RHIBufferHandle stagingH = CreateBuffer(stagingDesc);
        UploadBufferData(stagingH, data, size, 0);

        VkBuffer srcBuf = GetVkBuffer(stagingH);
        VkBuffer dstBuf = entry.buffer;

        ImmediateSubmit([=](VkCommandBuffer cmd) {
            VkBufferCopy region{0, offset, size};
            vkCmdCopyBuffer(cmd, srcBuf, dstBuf, 1, &region);
        });

        DestroyBuffer(stagingH);
    }
}

void VulkanDevice::ReadBufferData(RHIBufferHandle handle,
                                   void* data, uint64_t size, uint64_t offset) {
    if (!handle.IsValid() || handle.index >= m_buffers.size()) return;
    const auto& entry = m_buffers[handle.index];
    if (!entry.valid || !entry.desc.cpuVisible) return;

    vmaInvalidateAllocation(m_allocator, entry.alloc, offset, size);
    VmaAllocationInfo info{};
    vmaGetAllocationInfo(m_allocator, entry.alloc, &info);
    memcpy(data, static_cast<const uint8_t*>(info.pMappedData) + offset, size);
}

void VulkanDevice::DestroyBuffer(RHIBufferHandle handle) {
    if (!handle.IsValid() || handle.index >= m_buffers.size()) return;
    auto& entry = m_buffers[handle.index];
    if (!entry.valid) return;
    // Issue #72 Step 7.5: defer the actual vmaDestroyBuffer until we cycle
    // back to this frame slot (its fence will be waited then, guaranteeing the
    // GPU has finished using this buffer).
    m_pendingFree[m_frameIdx].buffers.emplace_back(entry.buffer, entry.alloc);
    entry.buffer = VK_NULL_HANDLE;
    entry.alloc  = VK_NULL_HANDLE;
    entry.valid  = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateTexture / UploadTextureData / DestroyTexture
// ─────────────────────────────────────────────────────────────────────────────
RHITextureHandle VulkanDevice::CreateTexture(const RHITextureDesc& desc) {
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    const bool is3D   = (desc.depth > 1 && !desc.cubemap);
    ici.imageType     = is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ici.format        = ToVkFormat(desc.format);
    ici.extent        = {desc.width, desc.height, desc.depth};
    ici.mipLevels     = desc.mipLevels;
    ici.arrayLayers   = desc.cubemap ? 6u : (is3D ? 1u : desc.arrayLayers);
    if (desc.cubemap) ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;

    auto u = static_cast<uint32_t>(desc.usage);
    if (u & static_cast<uint32_t>(RHITextureUsage::Sampled))         ici.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (u & static_cast<uint32_t>(RHITextureUsage::RenderTarget))    ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (u & static_cast<uint32_t>(RHITextureUsage::DepthStencil))    ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (u & static_cast<uint32_t>(RHITextureUsage::UnorderedAccess)) ici.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (u & static_cast<uint32_t>(RHITextureUsage::CopySrc))         ici.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (u & static_cast<uint32_t>(RHITextureUsage::CopyDst))         ici.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // Sampled textures always need TRANSFER_DST so UploadTextureData can copy into them.
    if (u & static_cast<uint32_t>(RHITextureUsage::Sampled))         ici.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage       img   = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    if (vmaCreateImage(m_allocator, &ici, &aci, &img, &alloc, nullptr) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreateTexture — vmaCreateImage failed ({}x{})",
                     desc.width, desc.height);
        return {};
    }

    // Determine aspect
    const VkFormat vkFmt = ToVkFormat(desc.format);
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (vkFmt == VK_FORMAT_D32_SFLOAT || vkFmt == VK_FORMAT_D16_UNORM)
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    else if (vkFmt == VK_FORMAT_D24_UNORM_S8_UINT)
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    VkImageViewCreateInfo viewCI{};
    viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image                           = img;
    const uint32_t layers = desc.cubemap ? 6u : (is3D ? 1u : desc.arrayLayers);
    if (desc.cubemap)
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    else if (is3D)
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_3D;
    else if (layers > 1)
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    else
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format                          = vkFmt;
    viewCI.subresourceRange.aspectMask     = aspect;
    viewCI.subresourceRange.levelCount     = desc.mipLevels;
    viewCI.subresourceRange.layerCount     = layers;

    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(m_device, &viewCI, nullptr, &view);

    // Issue #56: depth+stencil main view is attachment-only (sampling a
    // DEPTH|STENCIL view is invalid) — create a DEPTH-only sibling for
    // sampled-image descriptors.
    VkImageView sampledDepthView = VK_NULL_HANDLE;
    if (aspect & VK_IMAGE_ASPECT_STENCIL_BIT) {
        VkImageViewCreateInfo depthViewCI = viewCI;
        depthViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCreateImageView(m_device, &depthViewCI, nullptr, &sampledDepthView);
    }

    // Normalize: store arrayLayers=6 for cubemaps so Upload/Readback helpers
    // reading entry.desc.arrayLayers always see the real GPU layer count.
    RHITextureDesc storedDesc = desc;
    if (desc.cubemap) storedDesc.arrayLayers = 6;
    const RHITextureHandle h = AllocTextureSlot(img, view, alloc, storedDesc, /*swapchain=*/false);
    if (h.IsValid()) m_textures[h.index].sampledDepthView = sampledDepthView;
    return h;
}

void VulkanDevice::UploadTextureData(RHITextureHandle handle,
                                     const void* data, uint64_t size) {
    if (!handle.IsValid() || handle.index >= m_textures.size()) return;
    auto& entry = m_textures[handle.index];
    if (!entry.valid || entry.swapchain) return;

    // Create staging buffer
    RHIBufferDesc stagingDesc{};
    stagingDesc.size       = size;
    stagingDesc.usage      = RHIBufferUsage::CopySrc;
    stagingDesc.cpuVisible = true;
    RHIBufferHandle stagingH = CreateBuffer(stagingDesc);
    UploadBufferData(stagingH, data, size, 0);
    VkBuffer stagingBuf = GetVkBuffer(stagingH);

    VkImage img              = entry.image;
    const auto& desc         = entry.desc;
    const uint32_t width     = desc.width;
    const uint32_t height    = desc.height;
    const uint32_t layers    = desc.arrayLayers;

    ImmediateSubmit([=](VkCommandBuffer cmd) {
        // Transition UNDEFINED → TRANSFER_DST
        CmdTransitionImage(cmd, img,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = layers;
        region.imageExtent                     = {width, height, desc.depth};
        vkCmdCopyBufferToImage(cmd, stagingBuf, img,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition TRANSFER_DST → SHADER_READ
        CmdTransitionImage(cmd, img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    DestroyBuffer(stagingH);
}

void VulkanDevice::UploadTextureMips(RHITextureHandle           handle,
                                      std::span<const MipUpload> mips) {
    if (!handle.IsValid() || handle.index >= m_textures.size() || mips.empty()) return;
    auto& entry = m_textures[handle.index];
    if (!entry.valid || entry.swapchain) return;

    // One staging buffer for all mip data concatenated.
    uint64_t totalSize = 0;
    for (const auto& m : mips) totalSize += m.size;

    RHIBufferDesc stagingDesc{};
    stagingDesc.size       = totalSize;
    stagingDesc.usage      = RHIBufferUsage::CopySrc;
    stagingDesc.cpuVisible = true;
    RHIBufferHandle stagingH = CreateBuffer(stagingDesc);

    // Copy each mip into the staging buffer at the appropriate offset.
    uint64_t offset = 0;
    for (const auto& m : mips) {
        UploadBufferData(stagingH, m.data, m.size, offset);
        offset += m.size;
    }
    VkBuffer stagingBuf = GetVkBuffer(stagingH);

    VkImage         img    = entry.image;
    const uint32_t  layers = entry.desc.arrayLayers;

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        // Transition entire image UNDEFINED → TRANSFER_DST
        CmdTransitionImage(cmd, img,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        uint64_t bufOffset = 0;
        for (uint32_t m = 0; m < static_cast<uint32_t>(mips.size()); ++m) {
            const uint32_t mipW = std::max(1u, entry.desc.width  >> m);
            const uint32_t mipH = std::max(1u, entry.desc.height >> m);

            VkBufferImageCopy region{};
            region.bufferOffset                    = bufOffset;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = m;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = layers;
            region.imageExtent                     = {mipW, mipH, 1};
            vkCmdCopyBufferToImage(cmd, stagingBuf, img,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            bufOffset += mips[m].size;
        }

        // Transition entire image TRANSFER_DST → SHADER_READ
        CmdTransitionImage(cmd, img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    DestroyBuffer(stagingH);
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadbackTextureMips — GPU → CPU (mirror of UploadTextureMips)
// ─────────────────────────────────────────────────────────────────────────────
void VulkanDevice::ReadbackTextureMips(RHITextureHandle              handle,
                                       std::span<IRHIDevice::MipReadback> mips) {
    if (!handle.IsValid() || handle.index >= m_textures.size() || mips.empty()) return;
    auto& entry = m_textures[handle.index];
    if (!entry.valid || entry.swapchain) return;

    // One host-visible (GPU_TO_CPU) staging buffer for all mip data.
    uint64_t totalSize = 0;
    for (const auto& m : mips) totalSize += m.size;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = totalSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer      stagingBuf   = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    vmaCreateBuffer(m_allocator, &bci, &aci, &stagingBuf, &stagingAlloc, nullptr);

    VkImage        img    = entry.image;
    const uint32_t layers = entry.desc.arrayLayers;

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        // Transition SHADER_READ_ONLY → TRANSFER_SRC
        CmdTransitionImage(cmd, img,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        uint64_t bufOffset = 0;
        for (uint32_t m = 0; m < static_cast<uint32_t>(mips.size()); ++m) {
            const uint32_t mipW = std::max(1u, entry.desc.width  >> m);
            const uint32_t mipH = std::max(1u, entry.desc.height >> m);

            VkBufferImageCopy region{};
            region.bufferOffset                    = bufOffset;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = m;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = layers;
            region.imageExtent                     = { mipW, mipH, 1 };
            vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   stagingBuf, 1, &region);
            bufOffset += mips[m].size;
        }

        // Transition back to SHADER_READ_ONLY
        CmdTransitionImage(cmd, img,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    // Copy from the persistently-mapped staging buffer to caller's buffers.
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(m_allocator, stagingAlloc, &allocInfo);
    uint64_t offset = 0;
    for (auto& m : mips) {
        std::memcpy(m.data,
                    static_cast<const uint8_t*>(allocInfo.pMappedData) + offset,
                    m.size);
        offset += m.size;
    }

    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);
}

void VulkanDevice::DestroyTexture(RHITextureHandle handle) {
    if (!handle.IsValid() || handle.index >= m_textures.size()) return;
    auto& entry = m_textures[handle.index];
    if (!entry.valid || entry.swapchain) return;
    // Issue #72 Step 7.5: defer until fence for current slot fires.
    PendingImage pi;
    pi.image            = entry.image;
    pi.alloc            = entry.alloc;
    pi.view             = entry.view;
    pi.sampledDepthView = entry.sampledDepthView;
    pi.mipViews         = std::move(entry.mipViews);
    m_pendingFree[m_frameIdx].images.push_back(std::move(pi));
    entry.image            = VK_NULL_HANDLE;
    entry.alloc            = VK_NULL_HANDLE;
    entry.view             = VK_NULL_HANDLE;
    entry.sampledDepthView = VK_NULL_HANDLE;
    entry.mipViews.clear();
    entry.valid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateShader / DestroyShader
// ─────────────────────────────────────────────────────────────────────────────
RHIShaderHandle VulkanDevice::CreateShader(std::span<const uint8_t> spirv,
                                            const ShaderReflection& reflection) {
    if (spirv.empty()) {
        SA_LOG_ERROR("VulkanDevice::CreateShader — empty SPIR-V");
        return {};
    }

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(spirv.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &ci, nullptr, &mod) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreateShader — vkCreateShaderModule failed");
        return {};
    }

    RHIShaderHandle h{static_cast<uint32_t>(m_shaders.size())};
    m_shaders.push_back({mod, reflection, true});
    return h;
}

void VulkanDevice::DestroyShader(RHIShaderHandle handle) {
    if (!handle.IsValid() || handle.index >= m_shaders.size()) return;
    auto& entry = m_shaders[handle.index];
    if (!entry.valid) return;
    vkDestroyShaderModule(m_device, entry.module, nullptr);
    entry.valid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateDescriptorSetLayout / AllocateDescriptorSet / WriteDescriptor*
// ─────────────────────────────────────────────────────────────────────────────
static VkDescriptorType ToVkDescriptorType(RHIDescriptorType type) {
    switch (type) {
        case RHIDescriptorType::Texture2D:
        case RHIDescriptorType::TextureCube:        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case RHIDescriptorType::Sampler:            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case RHIDescriptorType::UniformBuffer:      return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case RHIDescriptorType::StorageBuffer:      return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case RHIDescriptorType::StorageBufferDynamic: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case RHIDescriptorType::StorageImage:       return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        default:                                    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
}

static VkShaderStageFlags ToVkShaderStages(RHIShaderStage stages) {
    VkShaderStageFlags flags = 0;
    if (HasStage(stages, RHIShaderStage::Vertex))   flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (HasStage(stages, RHIShaderStage::Fragment))  flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (HasStage(stages, RHIShaderStage::Compute))   flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags;
}

RHIDescLayoutHandle VulkanDevice::CreateDescriptorSetLayout(const ShaderReflection& merged,
                                                             uint32_t set) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto& b : merged.bindings) {
        if (b.set != set) continue;
        VkDescriptorSetLayoutBinding vkb{};
        vkb.binding            = b.binding;
        // Issue #72: SPIR-V reflection emits `StorageBuffer` for the MaterialParams
        // SSBO; promote it to STORAGE_BUFFER_DYNAMIC so per-draw dynamic offsets
        // can index into the MaterialParamRing. Convention is anchored on
        // (set=2, binding=0, name=="MaterialParams") (Step 6.5 set layout).
        VkDescriptorType vkType = ToVkDescriptorType(b.type);
        if (vkType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
            b.set == 2 && b.binding == 0 && b.name == "MaterialParams") {
            vkType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        }
        vkb.descriptorType     = vkType;
        vkb.descriptorCount    = b.arraySize;
        vkb.stageFlags         = ToVkShaderStages(b.stages);
        bindings.push_back(vkb);
    }

    // Issue #72: DYNAMIC descriptors (UBO_DYNAMIC / SSBO_DYNAMIC) cannot coexist
    // with UPDATE_AFTER_BIND in the same layout (VUID-03001 / VUID-03011).
    // If any binding is dynamic, drop UAB for the whole layout — they update
    // implicitly via dynamic offsets at bind time, so UAB semantics aren't needed.
    const bool hasDynamic = std::any_of(bindings.begin(), bindings.end(),
        [](const VkDescriptorSetLayoutBinding& vkb) {
            return vkb.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                   vkb.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        });

    std::vector<VkDescriptorBindingFlags> bindingFlags(bindings.size(),
        hasDynamic ? 0u : VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsCI{};
    flagsCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsCI.bindingCount  = static_cast<uint32_t>(bindingFlags.size());
    flagsCI.pBindingFlags = bindingFlags.empty() ? nullptr : bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.pNext        = &flagsCI;
    ci.flags        = hasDynamic ? 0u : VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &layout) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreateDescriptorSetLayout — failed for set {}", set);
        return {};
    }

    RHIDescLayoutHandle h{static_cast<uint32_t>(m_descLayouts.size())};
    m_descLayouts.push_back({layout, true});
    return h;
}

RHIDescLayoutHandle VulkanDevice::CreateBindlessTextureLayout(uint32_t capacity) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = capacity;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    // PARTIALLY_BOUND lets the heap leave slots unwritten — shaders only ever
    // index slots the engine has registered.
    const VkDescriptorBindingFlags bindingFlag =
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsCI{};
    flagsCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsCI.bindingCount  = 1;
    flagsCI.pBindingFlags = &bindingFlag;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.pNext        = &flagsCI;
    ci.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    ci.bindingCount = 1;
    ci.pBindings    = &binding;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &layout) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreateBindlessTextureLayout — failed (capacity={})", capacity);
        return {};
    }
    RHIDescLayoutHandle h{static_cast<uint32_t>(m_descLayouts.size())};
    m_descLayouts.push_back({layout, true});
    return h;
}

RHIDescSetHandle VulkanDevice::AllocateDescriptorSet(RHIDescLayoutHandle layoutHandle) {
    if (!layoutHandle.IsValid() || layoutHandle.index >= m_descLayouts.size()) return {};
    VkDescriptorSetLayout layout = m_descLayouts[layoutHandle.index].layout;

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;

    VkDescriptorSet ds = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &ai, &ds) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::AllocateDescriptorSet — vkAllocateDescriptorSets failed");
        return {};
    }

    RHIDescSetHandle h{static_cast<uint32_t>(m_descSets.size())};
    m_descSets.push_back({ds, true});
    return h;
}

void VulkanDevice::FreeDescriptorSet(RHIDescSetHandle handle) {
    if (!handle.IsValid() || handle.index >= m_descSets.size()) return;
    DescSetEntry& entry = m_descSets[handle.index];
    if (!entry.valid || entry.set == VK_NULL_HANDLE) return;
    // Issue #72 Step 7.5: defer the actual vkFreeDescriptorSets until we cycle
    // back to this slot. Solves "destroyed without UPDATE_AFTER_BIND" validation
    // when materials are released during scene transitions.
    m_pendingFree[m_frameIdx].descSets.push_back(entry.set);
    entry.set   = VK_NULL_HANDLE;
    entry.valid = false;
}

void VulkanDevice::WriteDescriptorTexture(RHIDescSetHandle dsHandle,
                                           uint32_t binding,
                                           RHITextureHandle textureHandle,
                                           bool depthStencilReadLayout) {
    if (!dsHandle.IsValid()      || dsHandle.index      >= m_descSets.size())      return;
    if (!textureHandle.IsValid() || textureHandle.index >= m_textures.size())      return;
    if (!m_textures[textureHandle.index].valid)                                    return;

    const auto& texEntry = m_textures[textureHandle.index];
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler     = m_samplerLinearRepeat;  // default sampler; allow per-binding override later
    imgInfo.imageView   = texEntry.sampledDepthView ? texEntry.sampledDepthView
                                                    : texEntry.view;
    imgInfo.imageLayout = depthStencilReadLayout
                              ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descSets[dsHandle.index].set;
    write.dstBinding      = binding;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanDevice::WriteDescriptorTextureArray(RHIDescSetHandle dsHandle,
                                                uint32_t binding,
                                                uint32_t arrayElement,
                                                RHITextureHandle textureHandle) {
    if (!dsHandle.IsValid()      || dsHandle.index      >= m_descSets.size()) return;
    if (!textureHandle.IsValid() || textureHandle.index >= m_textures.size()) return;
    if (!m_textures[textureHandle.index].valid)                               return;

    const auto& texEntry = m_textures[textureHandle.index];
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler     = m_samplerLinearRepeat;
    imgInfo.imageView   = texEntry.sampledDepthView ? texEntry.sampledDepthView
                                                    : texEntry.view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet           = m_descSets[dsHandle.index].set;
    write.dstBinding       = binding;
    write.dstArrayElement  = arrayElement;
    write.descriptorCount  = 1;
    write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo       = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanDevice::WriteDescriptorStorageImage(RHIDescSetHandle dsHandle,
                                               uint32_t binding,
                                               RHITextureHandle textureHandle) {
    if (!dsHandle.IsValid()      || dsHandle.index      >= m_descSets.size())  return;
    if (!textureHandle.IsValid() || textureHandle.index >= m_textures.size())  return;

    auto& entry = m_textures[textureHandle.index];
    if (!entry.valid) return;

    // Cube image views cannot be used as storage images.
    // For cubemap textures, lazily create a VK_IMAGE_VIEW_TYPE_2D_ARRAY view
    // covering mip 0 / all 6 layers, and use that for the UAV binding.
    VkImageView storageView = entry.view;
    if (entry.desc.cubemap) {
        if (entry.mipViews.empty()) entry.mipViews.resize(1, VK_NULL_HANDLE);
        if (entry.mipViews[0] == VK_NULL_HANDLE) {
            VkImageViewCreateInfo viewCI{};
            viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCI.image                           = entry.image;
            viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewCI.format                          = ToVkFormat(entry.desc.format);
            viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCI.subresourceRange.baseMipLevel   = 0;
            viewCI.subresourceRange.levelCount     = 1;
            viewCI.subresourceRange.baseArrayLayer = 0;
            viewCI.subresourceRange.layerCount     = 6;
            vkCreateImageView(m_device, &viewCI, nullptr, &entry.mipViews[0]);
        }
        storageView = entry.mipViews[0];
    }

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler     = VK_NULL_HANDLE;
    imgInfo.imageView   = storageView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descSets[dsHandle.index].set;
    write.dstBinding      = binding;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanDevice::WriteDescriptorStorageImageMip(RHIDescSetHandle dsHandle,
                                                   uint32_t         binding,
                                                   RHITextureHandle textureHandle,
                                                   uint32_t         mipLevel) {
    WriteDescriptorStorageImageArrayMip(dsHandle, binding, 0, textureHandle, mipLevel);
}

void VulkanDevice::WriteDescriptorStorageImageArrayMip(RHIDescSetHandle dsHandle,
                                                       uint32_t         binding,
                                                       uint32_t         arrayElement,
                                                       RHITextureHandle textureHandle,
                                                       uint32_t         mipLevel) {
    if (!dsHandle.IsValid()      || dsHandle.index      >= m_descSets.size())  return;
    if (!textureHandle.IsValid() || textureHandle.index >= m_textures.size())  return;

    auto& entry = m_textures[textureHandle.index];
    if (!entry.valid) return;

    // Lazily create the single-mip image view for the requested level.
    if (entry.mipViews.size() <= mipLevel)
        entry.mipViews.resize(mipLevel + 1, VK_NULL_HANDLE);

    if (entry.mipViews[mipLevel] == VK_NULL_HANDLE) {
        // For cubemap textures, use a 2D_ARRAY view covering all 6 faces at
        // the requested mip (cube image views cannot be used as storage images).
        const uint32_t numLayers = entry.desc.cubemap ? 6u : entry.desc.arrayLayers;
        VkImageViewCreateInfo viewCI{};
        viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image                           = entry.image;
        viewCI.viewType                        = (numLayers > 1)
                                                     ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                     : VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format                          = ToVkFormat(entry.desc.format);
        viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel   = mipLevel;
        viewCI.subresourceRange.levelCount     = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount     = numLayers;
        vkCreateImageView(m_device, &viewCI, nullptr, &entry.mipViews[mipLevel]);
    }

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler     = VK_NULL_HANDLE;
    imgInfo.imageView   = entry.mipViews[mipLevel];
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descSets[dsHandle.index].set;
    write.dstBinding      = binding;
    write.dstArrayElement = arrayElement;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanDevice::WriteDescriptorBuffer(RHIDescSetHandle dsHandle,
                                          uint32_t binding,
                                          RHIBufferHandle bufferHandle,
                                          uint64_t offset, uint64_t range,
                                          bool dynamic) {
    if (!dsHandle.IsValid()     || dsHandle.index     >= m_descSets.size())  return;
    if (!bufferHandle.IsValid() || bufferHandle.index >= m_buffers.size())   return;

    auto& buf = m_buffers[bufferHandle.index];
    const bool isStorage = (static_cast<uint32_t>(buf.desc.usage) &
                            static_cast<uint32_t>(RHIBufferUsage::Storage)) != 0;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = buf.buffer;
    bufInfo.offset = offset;
    bufInfo.range  = (range == ~0ull) ? VK_WHOLE_SIZE : range;

    VkDescriptorType descType;
    if (dynamic)        descType = isStorage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                                             : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    else                descType = isStorage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                             : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descSets[dsHandle.index].set;
    write.dstBinding      = binding;
    write.descriptorCount = 1;
    write.descriptorType  = descType;
    write.pBufferInfo     = &bufInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// CreatePipeline / DestroyPipeline
// ─────────────────────────────────────────────────────────────────────────────
RHIPipelineHandle VulkanDevice::CreatePipeline(const RHIPipelineDesc& desc) {
    // Collect descriptor set layouts into a VkPipelineLayout, preserving slot indices.
    // Invalid slots are filled with m_emptyDescLayout so that valid layouts land at the
    // correct set index (e.g., set=1 must be pSetLayouts[1], not pSetLayouts[0]).
    std::vector<VkDescriptorSetLayout> setLayouts;
    for (uint32_t i = 0; i < desc.descriptorLayoutCount; ++i) {
        const auto& h = desc.descriptorLayouts[i];
        if (h.IsValid() && h.index < m_descLayouts.size() && m_descLayouts[h.index].valid)
            setLayouts.push_back(m_descLayouts[h.index].layout);
        else
            setLayouts.push_back(m_emptyDescLayout);
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = ToVkShaderStages(desc.pushConstantStages);
    pushRange.size       = desc.pushConstantSize;

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.setLayoutCount         = static_cast<uint32_t>(setLayouts.size());
    layoutCI.pSetLayouts            = setLayouts.data();
    layoutCI.pushConstantRangeCount = (desc.pushConstantSize > 0) ? 1 : 0;
    layoutCI.pPushConstantRanges    = (desc.pushConstantSize > 0) ? &pushRange : nullptr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreatePipeline — vkCreatePipelineLayout failed");
        return {};
    }

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    auto addStage = [&](RHIShaderHandle h, VkShaderStageFlagBits stage) {
        if (!h.IsValid() || h.index >= m_shaders.size()) return;
        VkPipelineShaderStageCreateInfo si{};
        si.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        si.stage  = stage;
        si.module = m_shaders[h.index].module;
        si.pName  = "main";
        stages.push_back(si);
    };
    addStage(desc.vertShader, VK_SHADER_STAGE_VERTEX_BIT);
    addStage(desc.fragShader, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Vertex input
    // The mesh data on disk is always 48-byte interleaved (pos+normal+tangent+uv),
    // so binding stride is fixed at 48. Which attributes the pipeline declares,
    // however, comes from shader reflection — the pipeline emits one attrib per
    // location the SPIR-V actually consumes, eliminating "attribute at location N
    // not consumed" validation warnings.
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = 48;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // location → offset within the 48-byte interleaved CookedMesh::Vertex.
    // Keep this table in sync with the mesh data convention (CookedMesh.hpp).
    auto LocationToOffset = [](uint32_t loc) -> int32_t {
        switch (loc) {
            case 0: return  0;  // a_Position  (vec3)
            case 1: return 12;  // a_Normal    (vec3)
            case 2: return 24;  // a_Tangent   (vec4)
            case 3: return 40;  // a_TexCoord0 (vec2)
            default: return -1; // not in mesh layout — caller error
        }
    };
    auto ToVkVertexFormat = [](RHIVertexFormat f) {
        switch (f) {
            case RHIVertexFormat::R32_SFLOAT:           return VK_FORMAT_R32_SFLOAT;
            case RHIVertexFormat::R32G32_SFLOAT:        return VK_FORMAT_R32G32_SFLOAT;
            case RHIVertexFormat::R32G32B32_SFLOAT:     return VK_FORMAT_R32G32B32_SFLOAT;
            case RHIVertexFormat::R32G32B32A32_SFLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
            default:                                    return VK_FORMAT_UNDEFINED;
        }
    };

    // Legacy fallback layout for .refl files predating v6 (vertexInputCount==0).
    static const VkVertexInputAttributeDescription kLegacyAttribs[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT,   12},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24},
        {3, 0, VK_FORMAT_R32G32_SFLOAT,       40},
    };

    VkVertexInputAttributeDescription attribs[RHIPipelineDesc::kMaxVertexAttribs]{};
    uint32_t attribCount = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (!desc.noVertexInput) {
        if (desc.vertexInputCount > 0) {
            for (uint32_t i = 0; i < desc.vertexInputCount; ++i) {
                const auto& vi = desc.vertexInputs[i];
                const int32_t offset = LocationToOffset(vi.location);
                if (offset < 0) {
                    SA_LOG_ERROR("VulkanDevice::CreatePipeline — vertex location {} not in mesh layout (pipeline '{}')",
                                 vi.location, desc.debugName ? desc.debugName : "?");
                    continue;
                }
                attribs[attribCount].location = vi.location;
                attribs[attribCount].binding  = 0;
                attribs[attribCount].format   = ToVkVertexFormat(vi.format);
                attribs[attribCount].offset   = static_cast<uint32_t>(offset);
                ++attribCount;
            }
            vertexInput.vertexBindingDescriptionCount   = 1;
            vertexInput.pVertexBindingDescriptions      = &binding;
            vertexInput.vertexAttributeDescriptionCount = attribCount;
            vertexInput.pVertexAttributeDescriptions    = attribs;
        } else {
            vertexInput.vertexBindingDescriptionCount   = 1;
            vertexInput.pVertexBindingDescriptions      = &binding;
            vertexInput.vertexAttributeDescriptionCount = 4;
            vertexInput.pVertexAttributeDescriptions    = kLegacyAttribs;
        }
    }

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAsm{};
    inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    switch (desc.topology) {
        case RHITopology::TriangleStrip: inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
        case RHITopology::LineList:      inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;      break;
        default:                         inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  break;
    }

    // Dynamic viewport/scissor
    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount  = 1;

    // Rasterisation
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth   = 1.f;
    switch (desc.cullMode) {
        case RHICullMode::None:  raster.cullMode = VK_CULL_MODE_NONE;       break;
        case RHICullMode::Front: raster.cullMode = VK_CULL_MODE_FRONT_BIT;  break;
        default:                 raster.cullMode = VK_CULL_MODE_BACK_BIT;   break;
    }
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // Multisample
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil (Issue #56: compareOp + fixed-function stencil from desc)
    const auto toVkCompareOp = [](RHICompareOp op) {
        switch (op) {
            case RHICompareOp::Never:          return VK_COMPARE_OP_NEVER;
            case RHICompareOp::Less:           return VK_COMPARE_OP_LESS;
            case RHICompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
            case RHICompareOp::Greater:        return VK_COMPARE_OP_GREATER;
            case RHICompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
            case RHICompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case RHICompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
            default:                           return VK_COMPARE_OP_LESS_OR_EQUAL;
        }
    };
    const auto toVkStencilOp = [](RHIStencilOp op) {
        switch (op) {
            case RHIStencilOp::Zero:      return VK_STENCIL_OP_ZERO;
            case RHIStencilOp::Replace:   return VK_STENCIL_OP_REPLACE;
            case RHIStencilOp::IncrClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case RHIStencilOp::DecrClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case RHIStencilOp::Invert:    return VK_STENCIL_OP_INVERT;
            case RHIStencilOp::IncrWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case RHIStencilOp::DecrWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
            default:                      return VK_STENCIL_OP_KEEP;
        }
    };
    const auto toVkStencilState = [&](const RHIStencilOpState& s) {
        VkStencilOpState o{};
        o.failOp      = toVkStencilOp(s.failOp);
        o.passOp      = toVkStencilOp(s.passOp);
        o.depthFailOp = toVkStencilOp(s.failOp); // RHI has no separate depth-fail op
        o.compareOp   = toVkCompareOp(s.compareOp);
        o.compareMask = s.compareMask;
        // Vulkan has no stencilWriteEnable flag — write off = writeMask 0.
        o.writeMask   = desc.stencilWriteEnable ? s.writeMask : 0u;
        o.reference   = s.reference;
        return o;
    };

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = desc.depthTest  ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp   = toVkCompareOp(desc.depthCompareOp);
    if (desc.stencilTestEnable || desc.stencilWriteEnable) {
        ds.stencilTestEnable = VK_TRUE;
        ds.front             = toVkStencilState(desc.stencilFront);
        ds.back              = toVkStencilState(desc.stencilBack);
    }

    // Blend
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        std::max(1u, desc.colorFormatCount));
    for (auto& att : blendAttachments) {
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (desc.blendMode == RHIBlendMode::AlphaBlend) {
            att.blendEnable         = VK_TRUE;
            att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att.colorBlendOp        = VK_BLEND_OP_ADD;
            att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            att.alphaBlendOp        = VK_BLEND_OP_ADD;
        } else if (desc.blendMode == RHIBlendMode::Additive) {
            att.blendEnable         = VK_TRUE;
            att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            att.colorBlendOp        = VK_BLEND_OP_ADD;
            att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
    }

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blend.pAttachments    = blendAttachments.data();

    // Dynamic state
    const VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = static_cast<uint32_t>(std::size(dynStates));
    dynState.pDynamicStates    = dynStates;

    // Dynamic rendering — no VkRenderPass
    std::vector<VkFormat> colorFmts(desc.colorFormatCount);
    for (uint32_t i = 0; i < desc.colorFormatCount; i++)
        colorFmts[i] = ToVkFormat(desc.colorFormats[i]);

    VkPipelineRenderingCreateInfo renderingCI{};
    renderingCI.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCI.colorAttachmentCount    = static_cast<uint32_t>(colorFmts.size());
    renderingCI.pColorAttachmentFormats = colorFmts.data();
    renderingCI.depthAttachmentFormat   = ToVkFormat(desc.depthFormat);
    // Issue #56: stencil-bearing depth formats always declare the stencil
    // attachment — must match BeginRenderPass, which attaches stencil iff the
    // depth image has a stencil aspect (render-pass compatibility for ALL
    // pipelines sharing the attachment, stencil users or not).
    if (desc.depthFormat == RHIFormat::D24_S8)
        renderingCI.stencilAttachmentFormat = renderingCI.depthAttachmentFormat;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.pNext               = &renderingCI;
    pipelineCI.stageCount          = static_cast<uint32_t>(stages.size());
    pipelineCI.pStages             = stages.data();
    pipelineCI.pVertexInputState   = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAsm;
    pipelineCI.pViewportState      = &vpState;
    pipelineCI.pRasterizationState = &raster;
    pipelineCI.pMultisampleState   = &ms;
    pipelineCI.pDepthStencilState  = &ds;
    pipelineCI.pColorBlendState    = &blend;
    pipelineCI.pDynamicState       = &dynState;
    pipelineCI.layout              = layout;
    pipelineCI.renderPass          = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI,
                                   nullptr, &pipeline) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreatePipeline — vkCreateGraphicsPipelines failed ({})",
                     desc.debugName ? desc.debugName : "unnamed");
        vkDestroyPipelineLayout(m_device, layout, nullptr);
        return {};
    }

    RHIPipelineHandle h{static_cast<uint32_t>(m_pipelines.size())};
    m_pipelines.push_back({pipeline, layout,
                           desc.pushConstantSize, desc.pushConstantStages,
                           /*isCompute=*/false, /*valid=*/true});
    return h;
}

void VulkanDevice::DestroyPipeline(RHIPipelineHandle handle) {
    if (!handle.IsValid() || handle.index >= m_pipelines.size()) return;
    auto& entry = m_pipelines[handle.index];
    if (!entry.valid) return;
    if (entry.pipeline) vkDestroyPipeline(m_device, entry.pipeline, nullptr);
    if (entry.layout)   vkDestroyPipelineLayout(m_device, entry.layout, nullptr);
    entry.valid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateComputePipeline
// ─────────────────────────────────────────────────────────────────────────────
RHIPipelineHandle VulkanDevice::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    // Pipeline layout (descriptor sets + push constants).
    // Preserve slot indices: fill invalid entries with m_emptyDescLayout so that
    // valid layouts land at the correct set index (e.g. set=1 → pSetLayouts[1]).
    std::vector<VkDescriptorSetLayout> setLayouts;
    for (uint32_t i = 0; i < desc.descriptorLayoutCount; ++i) {
        const auto& h = desc.descriptorLayouts[i];
        if (h.IsValid() && h.index < m_descLayouts.size() && m_descLayouts[h.index].valid)
            setLayouts.push_back(m_descLayouts[h.index].layout);
        else
            setLayouts.push_back(m_emptyDescLayout);
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size       = desc.pushConstantSize;

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.setLayoutCount         = static_cast<uint32_t>(setLayouts.size());
    layoutCI.pSetLayouts            = setLayouts.data();
    layoutCI.pushConstantRangeCount = (desc.pushConstantSize > 0) ? 1 : 0;
    layoutCI.pPushConstantRanges    = (desc.pushConstantSize > 0) ? &pushRange : nullptr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreateComputePipeline — vkCreatePipelineLayout failed");
        return {};
    }

    if (!desc.computeShader.IsValid() || desc.computeShader.index >= m_shaders.size()) {
        SA_LOG_ERROR("VulkanDevice::CreateComputePipeline — invalid compute shader handle");
        vkDestroyPipelineLayout(m_device, layout, nullptr);
        return {};
    }

    VkPipelineShaderStageCreateInfo stageCI{};
    stageCI.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageCI.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stageCI.module = m_shaders[desc.computeShader.index].module;
    stageCI.pName  = "main";

    VkComputePipelineCreateInfo pipelineCI{};
    pipelineCI.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCI.stage  = stageCI;
    pipelineCI.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI,
                                  nullptr, &pipeline) != VK_SUCCESS) {
        SA_LOG_ERROR("VulkanDevice::CreateComputePipeline — vkCreateComputePipelines failed ({})",
                     desc.debugName ? desc.debugName : "unnamed");
        vkDestroyPipelineLayout(m_device, layout, nullptr);
        return {};
    }

    RHIPipelineHandle h{static_cast<uint32_t>(m_pipelines.size())};
    m_pipelines.push_back({pipeline, layout,
                           desc.pushConstantSize, RHIShaderStage::Compute,
                           /*isCompute=*/true, /*valid=*/true});
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal accessor helpers
// ─────────────────────────────────────────────────────────────────────────────
VkBuffer VulkanDevice::GetVkBuffer(RHIBufferHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_buffers.size()) return VK_NULL_HANDLE;
    return m_buffers[handle.index].valid ? m_buffers[handle.index].buffer : VK_NULL_HANDLE;
}

VkPipeline VulkanDevice::GetVkPipeline(RHIPipelineHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_pipelines.size()) return VK_NULL_HANDLE;
    return m_pipelines[handle.index].valid ? m_pipelines[handle.index].pipeline : VK_NULL_HANDLE;
}

VkPipelineLayout VulkanDevice::GetVkPipelineLayout(RHIPipelineHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_pipelines.size()) return VK_NULL_HANDLE;
    return m_pipelines[handle.index].valid ? m_pipelines[handle.index].layout : VK_NULL_HANDLE;
}

VkDescriptorSet VulkanDevice::GetVkDescriptorSet(RHIDescSetHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_descSets.size()) return VK_NULL_HANDLE;
    return m_descSets[handle.index].valid ? m_descSets[handle.index].set : VK_NULL_HANDLE;
}

const RHITextureDesc* VulkanDevice::GetTextureDesc(RHITextureHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_textures.size()) return nullptr;
    return m_textures[handle.index].valid ? &m_textures[handle.index].desc : nullptr;
}

RHIMemoryStats VulkanDevice::GetMemoryStats() const {
    // Logical sizes computed from stored descriptors (same formula as RenderGraph).
    auto calcTexBytes = [](const RHITextureDesc& d) -> uint64_t {
        uint64_t bpp = 0;
        switch (d.format) {
            case RHIFormat::BC1_UNORM:                            bpp = 0; break; // handled below
            case RHIFormat::BC3_UNORM: case RHIFormat::BC5_UNORM:
            case RHIFormat::BC7_UNORM:                            bpp = 0; break;
            case RHIFormat::RGBA8_UNORM: case RHIFormat::RGBA8_SRGB:
            case RHIFormat::BGRA8_UNORM: case RHIFormat::BGRA8_SRGB:
            case RHIFormat::RG16F:       case RHIFormat::R32F:
            case RHIFormat::R32_UINT:
            case RHIFormat::D32F:        case RHIFormat::D24_S8:  bpp = 4; break;
            case RHIFormat::RGBA16F:     case RHIFormat::RG32F:   bpp = 8; break;
            case RHIFormat::RGBA32F:                              bpp = 16; break;
            case RHIFormat::R8_UNORM:                             bpp = 1; break;
            case RHIFormat::D16_UNORM:                            bpp = 2; break;
            default:                                              bpp = 4; break;
        }
        const uint32_t faces = d.cubemap ? 6u : 1u;
        if (bpp == 0) {
            // Block-compressed: 4×4 texel blocks
            const uint64_t blockBytes = (d.format == RHIFormat::BC1_UNORM) ? 8u : 16u;
            uint64_t total = 0;
            uint32_t w = d.width, h = d.height;
            for (uint32_t m = 0; m < d.mipLevels; ++m) {
                uint32_t bw = std::max(1u, (w + 3) / 4);
                uint32_t bh = std::max(1u, (h + 3) / 4);
                total += static_cast<uint64_t>(bw) * bh * blockBytes * faces;
                w = std::max(1u, w / 2); h = std::max(1u, h / 2);
            }
            return total;
        }
        uint64_t total = 0;
        uint32_t w = d.width, h = d.height;
        for (uint32_t m = 0; m < d.mipLevels; ++m) {
            total += static_cast<uint64_t>(w) * h * bpp * faces;
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        }
        return total;
    };

    RHIMemoryStats s{};
    for (const auto& e : m_textures)
        if (e.valid && !e.swapchain)
            s.gpuTextureBytes += calcTexBytes(e.desc);
    for (const auto& e : m_buffers)
        if (e.valid)
            s.gpuBufferBytes += e.desc.size;

    // Actual VRAM usage from VMA — includes alignment overhead and covers all
    // VMA-managed allocations (textures + buffers). Sum device-local heaps only.
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(m_allocator, budgets);
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            s.gpuUsedBytes   += budgets[i].usage;
            s.gpuBudgetBytes += budgets[i].budget;
        }
    }
    return s;
}

bool VulkanDevice::IsComputePipeline(RHIPipelineHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_pipelines.size()) return false;
    return m_pipelines[handle.index].isCompute;
}

uint32_t VulkanDevice::GetPushConstantSize(RHIPipelineHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_pipelines.size()) return 0;
    return m_pipelines[handle.index].pushConstSize;
}

RHIShaderStage VulkanDevice::GetPushConstantStages(RHIPipelineHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_pipelines.size()) return RHIShaderStage::None;
    return m_pipelines[handle.index].pushConstStages;
}

} // namespace StellarAlia::RHI
