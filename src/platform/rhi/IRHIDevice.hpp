#pragma once

#include <cstdint>
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

    // Layout built from MergeReflections(vert, frag).
    // May be invalid for pipelines with no descriptor sets.
    RHIDescLayoutHandle descriptorLayout;

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

    // ── Descriptor Set ────────────────────────────────────────────────────────

    [[nodiscard]] virtual RHIDescSetHandle AllocateDescriptorSet(
        RHIDescLayoutHandle layout) = 0;

    virtual void WriteDescriptorTexture(RHIDescSetHandle ds,
                                        uint32_t         binding,
                                        RHITextureHandle texture) = 0;

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
    virtual void UploadTextureData(RHITextureHandle handle,
                                   const void*      data,
                                   uint64_t         size) = 0;

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
};

} // namespace StellarAlia::RHI
