#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <span>

#include <volk.h>

// VMA — included here only for the VmaAllocator handle type.
// The implementation (VMA_IMPLEMENTATION) lives in VulkanDevice.cpp.
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vma/vk_mem_alloc.h>

#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/vulkan/VulkanCommandList.hpp"

namespace StellarAlia::RHI {

// ─────────────────────────────────────────────────────────────────────────────
// VulkanDevice
//
// Concrete IRHIDevice backed by Vulkan 1.3.
// Features used from core 1.3: dynamic rendering, synchronization2.
// Memory management: VMA.
// Function loading: volk.
// ─────────────────────────────────────────────────────────────────────────────
class VulkanDevice final : public IRHIDevice {
public:
    ~VulkanDevice() override;

    [[nodiscard]] static std::unique_ptr<VulkanDevice> Create(const RHIDeviceDesc& desc);

    // ── IRHIDevice ────────────────────────────────────────────────────────────
    RHITextureHandle    CreateTexture(const RHITextureDesc& desc) override;
    RHIBufferHandle     CreateBuffer(const RHIBufferDesc& desc) override;
    RHIShaderHandle     CreateShader(std::span<const uint8_t> spirv,
                                     const ShaderReflection& reflection) override;

    RHIDescLayoutHandle CreateDescriptorSetLayout(const ShaderReflection& merged,
                                                  uint32_t set) override;
    RHIDescLayoutHandle CreateBindlessTextureLayout(uint32_t capacity) override;
    RHIPipelineHandle   CreatePipeline(const RHIPipelineDesc& desc) override;
    RHIPipelineHandle   CreateComputePipeline(const RHIComputePipelineDesc& desc) override;

    RHIDescSetHandle AllocateDescriptorSet(RHIDescLayoutHandle layout) override;
    void FreeDescriptorSet(RHIDescSetHandle ds) override;
    void WriteDescriptorTexture(RHIDescSetHandle ds, uint32_t binding,
                                RHITextureHandle texture) override;
    void WriteDescriptorTextureArray(RHIDescSetHandle ds, uint32_t binding,
                                     uint32_t arrayElement,
                                     RHITextureHandle texture) override;
    void WriteDescriptorStorageImage(RHIDescSetHandle ds, uint32_t binding,
                                     RHITextureHandle texture) override;
    void WriteDescriptorStorageImageMip(RHIDescSetHandle ds, uint32_t binding,
                                        RHITextureHandle texture,
                                        uint32_t         mipLevel) override;
    void WriteDescriptorBuffer(RHIDescSetHandle ds, uint32_t binding,
                               RHIBufferHandle buffer,
                               uint64_t offset = 0,
                               uint64_t range  = ~0ull,
                               bool     dynamic = false) override;

    void UploadBufferData(RHIBufferHandle buffer, const void* data,
                          uint64_t size, uint64_t offset = 0) override;
    void ReadBufferData(RHIBufferHandle buffer, void* data,
                        uint64_t size, uint64_t offset = 0) override;
    void UploadTextureData(RHITextureHandle handle,
                           const void* data, uint64_t size) override;
    void UploadTextureMips(RHITextureHandle           handle,
                           std::span<const MipUpload> mips) override;
    void ReadbackTextureMips(RHITextureHandle       handle,
                             std::span<MipReadback> mips) override;
    [[nodiscard]] const RHITextureDesc* GetTextureDesc(
        RHITextureHandle handle) const override;
    [[nodiscard]] RHIMemoryStats   GetMemoryStats()  const override;
    [[nodiscard]] std::string_view GetDeviceName()  const override { return m_gpuName; }

    void DestroyTexture(RHITextureHandle  handle) override;
    void DestroyBuffer(RHIBufferHandle   handle) override;
    void DestroyShader(RHIShaderHandle   handle) override;
    void DestroyPipeline(RHIPipelineHandle handle) override;

    IRHICommandList* BeginFrame() override;
    void             EndFrame()   override;
    void             Present()    override;
    void             WaitIdle()   override;
    void             ImmediateCompute(std::function<void(IRHICommandList*)> fn) override;

    RHITextureHandle GetSwapchainTexture()          override;
    RHIFormat        GetSwapchainFormat()           override;
    uint32_t         GetSwapchainWidth()            override;
    uint32_t         GetSwapchainHeight()           override;
    void             ResizeSwapchain(uint32_t width, uint32_t height) override;
    uint32_t         GetCurrentFrameIndex() const   override { return m_frameIdx; }

    uint32_t         GetMinStorageBufferOffsetAlignment() const override {
        return static_cast<uint32_t>(m_minStorageBufferOffsetAlignment);
    }

    // ── ImGui integration ─────────────────────────────────────────────────────
    // Raw Vulkan handles required to initialise imgui_impl_vulkan.
    // Only the editor layer should call this.
    struct ImGuiVulkanContext {
        VkInstance       instance;
        VkPhysicalDevice physicalDevice;
        VkDevice         device;
        VkQueue          graphicsQueue;
        uint32_t         graphicsFamily;
        uint32_t         swapchainImageCount;    // total images in the swapchain
        uint32_t         swapchainMinImageCount; // VkSurfaceCapabilitiesKHR::minImageCount
        VkFormat         swapchainFormat;        // actual swapchain surface format
    };
    [[nodiscard]] ImGuiVulkanContext GetImGuiContext() const;

    // Editor-only helpers for ImGui texture binding (ImGui_ImplVulkan_AddTexture).
    [[nodiscard]] VkImageView GetTextureImageView(RHITextureHandle handle) const { return GetVkImageView(handle); }
    [[nodiscard]] VkSampler   GetLinearSampler()                           const { return m_samplerLinearRepeat; }

    // ── Internal helpers (used by VulkanCommandList) ──────────────────────────
    VkImage          GetVkImage         (RHITextureHandle  handle) const;
    VkImageView      GetVkImageView     (RHITextureHandle  handle) const;
    VkBuffer         GetVkBuffer        (RHIBufferHandle   handle) const;
    VkPipeline       GetVkPipeline      (RHIPipelineHandle handle) const;
    VkPipelineLayout GetVkPipelineLayout(RHIPipelineHandle handle) const;
    VkDescriptorSet  GetVkDescriptorSet (RHIDescSetHandle  handle) const;

    // Push constant metadata for the currently bound pipeline (used by SetPushConstants).
    uint32_t       GetPushConstantSize  (RHIPipelineHandle handle) const;
    RHIShaderStage GetPushConstantStages(RHIPipelineHandle handle) const;

    // Returns true when the pipeline was created via CreateComputePipeline.
    // Used by VulkanCommandList to select the correct bind point.
    bool IsComputePipeline(RHIPipelineHandle handle) const;

private:
    VulkanDevice() = default;

    void InitInstance(bool validation);
    void InitSurface(void* glfwWindow);
    void InitPhysicalDevice();
    void InitDevice();
    void InitAllocator();
    void InitDescriptorPool();
    void InitDefaultSamplers();
    void CreateSwapchain(uint32_t width, uint32_t height, bool vsync);
    void DestroySwapchain();
    void CreateFrameData();
    void DestroyFrameData();
    void RecreateSwapchain();

    // Issue #72 Step 7.5: drain deferred Vulkan resource destructions queued
    // during the previous use of slot `slot`. Caller must have waited for
    // m_frames[slot].fence already (BeginFrame does this).
    void FlushPendingFree(uint32_t slot);

    // Allocate a slot in m_textures and return its handle.
    RHITextureHandle AllocTextureSlot(VkImage image, VkImageView view,
                                      VmaAllocation alloc,
                                      const RHITextureDesc& desc,
                                      bool isSwapchain = false);

    // One-shot command submission for uploads (staging → GPU, layout transitions).
    // Callback receives a fresh VkCommandBuffer that is submitted and waited on return.
    void ImmediateSubmit(std::function<void(VkCommandBuffer)>&& fn);

    // ── Vulkan core ───────────────────────────────────────────────────────────
    std::string              m_gpuName;
    VkInstance               m_instance        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physDevice      = VK_NULL_HANDLE;
    VkDevice                 m_device          = VK_NULL_HANDLE;
    VkQueue                  m_graphicsQueue   = VK_NULL_HANDLE;
    uint32_t                 m_graphicsFamily  = 0;
    VkDebugUtilsMessengerEXT m_debugMessenger  = VK_NULL_HANDLE;

    // Cached physical-device limits (queried in InitDevice).
    VkDeviceSize             m_minStorageBufferOffsetAlignment = 16;

    // ── Surface + Swapchain ───────────────────────────────────────────────────
    VkSurfaceKHR   m_surface              = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain            = VK_NULL_HANDLE;
    VkFormat       m_swapchainVkFormat    = VK_FORMAT_UNDEFINED;
    VkExtent2D     m_swapchainExtent      = {};
    bool           m_vsync                = true;
    uint32_t       m_swapMinImageCount    = 2;   // caps.minImageCount+1 (requested count)

    std::vector<VkImage>          m_swapImages;
    std::vector<VkImageView>      m_swapImageViews;
    std::vector<RHITextureHandle> m_swapHandles;  // handle per swapchain image

    // ── VMA ───────────────────────────────────────────────────────────────────
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    // ── Texture resource pool ─────────────────────────────────────────────────
    struct TextureEntry {
        VkImage        image      = VK_NULL_HANDLE;
        VkImageView    view       = VK_NULL_HANDLE;  // full-mip view (all levels)
        VmaAllocation  alloc      = VK_NULL_HANDLE;  // null → swapchain-owned
        RHITextureDesc desc       = {};
        bool           valid      = false;
        bool           swapchain  = false;
        // Per-mip image views, lazily created by WriteDescriptorStorageImageMip.
        // mipViews[m] is a single-level view for mip m only.
        std::vector<VkImageView> mipViews = {};
    };
    std::vector<TextureEntry> m_textures;

    // ── Buffer resource pool ──────────────────────────────────────────────────
    struct BufferEntry {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VmaAllocation  alloc  = VK_NULL_HANDLE;
        RHIBufferDesc  desc   = {};
        bool           valid  = false;
    };
    std::vector<BufferEntry> m_buffers;

    // ── Shader resource pool ──────────────────────────────────────────────────
    struct ShaderEntry {
        VkShaderModule   module     = VK_NULL_HANDLE;
        ShaderReflection reflection = {};
        bool             valid      = false;
    };
    std::vector<ShaderEntry> m_shaders;

    // ── Pipeline resource pool ────────────────────────────────────────────────
    struct PipelineEntry {
        VkPipeline       pipeline       = VK_NULL_HANDLE;
        VkPipelineLayout layout         = VK_NULL_HANDLE;
        uint32_t         pushConstSize  = 0;
        RHIShaderStage   pushConstStages= RHIShaderStage::None;
        bool             isCompute      = false;
        bool             valid          = false;
    };
    std::vector<PipelineEntry> m_pipelines;

    // ── Descriptor layout pool (cached by set index hash) ─────────────────────
    struct DescLayoutEntry {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        bool                  valid  = false;
    };
    std::vector<DescLayoutEntry> m_descLayouts;

    // ── Descriptor set pool ───────────────────────────────────────────────────
    struct DescSetEntry {
        VkDescriptorSet set   = VK_NULL_HANDLE;
        bool            valid = false;
    };
    std::vector<DescSetEntry> m_descSets;
    VkDescriptorPool          m_descPool = VK_NULL_HANDLE;

    // ── Default samplers ──────────────────────────────────────────────────────
    VkSampler             m_samplerLinearRepeat  = VK_NULL_HANDLE;
    VkSampler             m_samplerNearestRepeat = VK_NULL_HANDLE;
    // Empty layout (0 bindings) used to fill gaps in pipeline layout slot arrays,
    // so that valid layouts land at the correct set index in pSetLayouts[].
    VkDescriptorSetLayout m_emptyDescLayout      = VK_NULL_HANDLE;

    // ── Immediate submit infrastructure ──────────────────────────────────────
    VkCommandPool   m_immCmdPool = VK_NULL_HANDLE;
    VkCommandBuffer m_immCmd     = VK_NULL_HANDLE;
    VkFence         m_immFence   = VK_NULL_HANDLE;

    // ── Per-frame data (double-buffered) ──────────────────────────────────────
    static constexpr uint32_t MAX_FRAMES = 2;
    struct FrameData {
        VkCommandPool   pool     = VK_NULL_HANDLE;
        VkCommandBuffer cmd      = VK_NULL_HANDLE;
        VkSemaphore     imgReady = VK_NULL_HANDLE; // signaled by acquire
        VkFence         fence    = VK_NULL_HANDLE; // signaled when GPU finishes
    } m_frames[MAX_FRAMES];

    // One renderDone semaphore per swapchain image to avoid semaphore reuse
    // while the presentation engine still holds a reference to it.
    std::vector<VkSemaphore> m_renderDoneSems;

    // Issue #72 Step 7.5: per-frame deferred destruction queues. A resource
    // queued during slot N's recording is freed when we return to slot N (after
    // its fence is waited), guaranteeing the GPU has finished using it.
    struct PendingImage {
        VkImage                  image    = VK_NULL_HANDLE;
        VmaAllocation            alloc    = VK_NULL_HANDLE;
        VkImageView              view     = VK_NULL_HANDLE;
        std::vector<VkImageView> mipViews;
    };
    struct PendingFree {
        std::vector<VkDescriptorSet>                    descSets;
        std::vector<std::pair<VkBuffer, VmaAllocation>> buffers;
        std::vector<PendingImage>                       images;
    };
    PendingFree m_pendingFree[MAX_FRAMES];

    uint32_t m_frameIdx = 0;   // current in-flight slot [0..MAX_FRAMES-1]
    uint32_t m_imageIdx = 0;   // acquired swapchain image index
    bool     m_needResize = false;

    VulkanCommandList m_cmdList;
};

} // namespace StellarAlia::RHI
