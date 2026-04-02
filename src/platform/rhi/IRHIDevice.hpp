#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "platform/rhi/RHITypes.hpp"
#include "platform/rhi/ShaderReflection.hpp"

namespace StellarAlia::RHI {

class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline creation descriptor
// Populated by the Resource Layer once shader reflection is available.
// ─────────────────────────────────────────────────────────────────────────────

enum class RHICullMode   : uint8_t { None, Back, Front };
enum class RHIBlendMode  : uint8_t { Opaque, AlphaBlend, Additive };
enum class RHITopology   : uint8_t { TriangleList, TriangleStrip, LineList };

struct RHIPipelineDesc {
    RHIShaderHandle     vertShader;
    RHIShaderHandle     fragShader;

    // Ordered list of descriptor set layouts.
    // Index i = set i in the pipeline layout (set=0, set=1, …).
    // Set to invalid handles for unused slots; trailing invalid handles are ignored.
    RHIDescLayoutHandle descriptorLayouts[4] = {};
    uint32_t            descriptorLayoutCount = 0;

    uint32_t            pushConstantSize   = 0;
    RHIShaderStage      pushConstantStages = RHIShaderStage::None;

    // Render target formats (dynamic rendering — must match the pass attachment formats)
    RHIFormat           colorFormats[8]  = {};
    uint32_t            colorFormatCount = 0;
    RHIFormat           depthFormat      = RHIFormat::Undefined;

    // Render state
    RHICullMode         cullMode         = RHICullMode::Back;
    RHIBlendMode        blendMode        = RHIBlendMode::Opaque;
    RHITopology         topology         = RHITopology::TriangleList;
    bool                depthTest        = true;
    bool                depthWrite       = true;

    // When true, vertex input is skipped (gl_VertexIndex fullscreen-tri trick)
    bool                noVertexInput    = false;

    const char*         debugName        = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Compute pipeline creation descriptor
// ─────────────────────────────────────────────────────────────────────────────
struct RHIComputePipelineDesc {
    RHIShaderHandle     computeShader;

    RHIDescLayoutHandle descriptorLayouts[4] = {};
    uint32_t            descriptorLayoutCount = 0;

    uint32_t            pushConstantSize   = 0;

    const char*         debugName          = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// IRHIDevice
//
// Central resource factory and frame driver.
// The concrete implementation (VulkanDevice, D3D12Device...) is the only place
// in the engine that sees VkImage / ID3D12Resource / VmaAllocation etc.
//
// Ownership convention:
//   All handles are owned by the device.
//   Destroy*() must be called before the device is destroyed.
//   The device does NOT reference-count handles automatically.
// ─────────────────────────────────────────────────────────────────────────────
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // ── Resource Creation ─────────────────────────────────────────────────────

    [[nodiscard]] virtual RHITextureHandle CreateTexture(
        const RHITextureDesc& desc) = 0;

    [[nodiscard]] virtual RHIBufferHandle CreateBuffer(
        const RHIBufferDesc& desc) = 0;

    // spirv:      raw SPIR-V bytecode loaded from a .spv file
    // reflection: pre-parsed binding metadata loaded from a companion .refl file
    [[nodiscard]] virtual RHIShaderHandle CreateShader(
        std::span<const uint8_t> spirv,
        const ShaderReflection&  reflection) = 0;

    // ── Reflection-Driven Layout & Pipeline ───────────────────────────────────

    // Build a descriptor set layout for a single set index from merged reflection.
    // Cached internally by the implementation — calling twice with equal inputs
    // returns the same handle.
    [[nodiscard]] virtual RHIDescLayoutHandle CreateDescriptorSetLayout(
        const ShaderReflection& merged,
        uint32_t                set) = 0;

    [[nodiscard]] virtual RHIPipelineHandle CreatePipeline(
        const RHIPipelineDesc& desc) = 0;

    // ── Compute Pipeline ──────────────────────────────────────────────────────
    [[nodiscard]] virtual RHIPipelineHandle CreateComputePipeline(
        const RHIComputePipelineDesc& desc) = 0;

    // ── Descriptor Set ────────────────────────────────────────────────────────

    [[nodiscard]] virtual RHIDescSetHandle AllocateDescriptorSet(
        RHIDescLayoutHandle layout) = 0;

    virtual void WriteDescriptorTexture(RHIDescSetHandle ds,
                                        uint32_t         binding,
                                        RHITextureHandle texture) = 0;

    // Writes a texture as a storage image (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE /
    // VK_IMAGE_LAYOUT_GENERAL). Used for compute UAV bindings.
    virtual void WriteDescriptorStorageImage(RHIDescSetHandle ds,
                                             uint32_t         binding,
                                             RHITextureHandle texture) = 0;

    // Writes a single mip level of a texture as a storage image UAV.
    // Used for per-mip compute writes (e.g. prefiltered env mip chain).
    virtual void WriteDescriptorStorageImageMip(RHIDescSetHandle ds,
                                                uint32_t         binding,
                                                RHITextureHandle texture,
                                                uint32_t         mipLevel) = 0;

    virtual void WriteDescriptorBuffer(RHIDescSetHandle ds,
                                       uint32_t         binding,
                                       RHIBufferHandle  buffer,
                                       uint64_t         offset = 0,
                                       uint64_t         range  = ~0ull) = 0;

    // ── Data Upload ───────────────────────────────────────────────────────────

    // Convenience upload for CPU-visible buffers.
    // For GPU-only buffers use CopyBuffer via a staging buffer.
    virtual void UploadBufferData(RHIBufferHandle buffer,
                                  const void*     data,
                                  uint64_t        size,
                                  uint64_t        offset = 0) = 0;

    // Upload initial pixel data to a GPU-only texture via an internal staging buffer.
    // Allocs staging → memcpy → one-shot submit → transitions layout to ShaderRead → frees staging.
    // Only uploads mip level 0. For multi-mip textures use UploadTextureMips.
    virtual void UploadTextureData(RHITextureHandle handle,
                                   const void*      data,
                                   uint64_t         size) = 0;

    // Per-mip data span passed to UploadTextureMips.
    struct MipUpload { const void* data; uint64_t size; };

    // Upload all mip levels in a single command submit.
    // mips[i] corresponds to mip level i; mips.size() must equal the texture's mipLevels.
    virtual void UploadTextureMips(RHITextureHandle          handle,
                                   std::span<const MipUpload> mips) = 0;

    // ── Resource Destruction ──────────────────────────────────────────────────
    // Safe to call with an invalid handle (no-op).

    virtual void DestroyTexture(RHITextureHandle  handle) = 0;
    virtual void DestroyBuffer(RHIBufferHandle   handle) = 0;
    virtual void DestroyShader(RHIShaderHandle   handle) = 0;
    virtual void DestroyPipeline(RHIPipelineHandle handle) = 0;

    // ── Frame Control ─────────────────────────────────────────────────────────
    // BeginFrame: acquires swapchain image, resets command buffer, begins recording.
    // EndFrame:   ends recording.
    // Present:    submits + presents.

    [[nodiscard]] virtual IRHICommandList* BeginFrame() = 0;
    virtual void EndFrame()  = 0;
    virtual void Present()   = 0;

    // Flush all GPU work. Use for shutdown or resource streaming.
    virtual void WaitIdle()  = 0;

    // Submit one-shot compute work outside the frame loop.
    // Records commands into an immediate command buffer, submits to the
    // GPU queue, and waits for completion before returning.
    // Use for startup initialization (IBL bake, asset streaming, etc.)
    // that must complete before the first frame begins.
    virtual void ImmediateCompute(std::function<void(IRHICommandList*)> fn) = 0;

    // ── Swapchain ─────────────────────────────────────────────────────────────
    // GetSwapchainTexture(): valid only between BeginFrame() and EndFrame().
    // Returns the back-buffer texture for the current in-flight frame.
    // RenderGraph imports this as an external texture for the final blit / UI pass.

    [[nodiscard]] virtual RHITextureHandle GetSwapchainTexture() = 0;
    [[nodiscard]] virtual RHIFormat        GetSwapchainFormat()  = 0;
    [[nodiscard]] virtual uint32_t         GetSwapchainWidth()   = 0;
    [[nodiscard]] virtual uint32_t         GetSwapchainHeight()  = 0;

    // Called when the OS window is resized (e.g. from GLFWWindow resize callback).
    // The implementation recreates the swapchain and any size-dependent resources.
    virtual void ResizeSwapchain(uint32_t width, uint32_t height) = 0;

    // Returns the current in-flight frame slot index [0..MAX_FRAMES-1].
    // Valid after BeginFrame() returns a non-null command list.
    [[nodiscard]] virtual uint32_t GetCurrentFrameIndex() const = 0;
};

} // namespace StellarAlia::RHI
